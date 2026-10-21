#include <SFML/System/Time.hpp>

#include <cmath>

#include "Fury/RenderThread.h"
#include "Fury/RenderUtil.h"

#include "Fury/Camera.h"
#include "Fury/FramePacket.h"
#include "Fury/BoxBounds.h"
#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Vector4.h"
#include "Fury/Shader.h"
#include "Fury/SceneNode.h"
#include "Fury/Frustum.h"
#include "Fury/Mesh.h"
#include "Fury/MeshUtil.h"
#include "Fury/Texture.h"

#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/RHI/OpenGL/GLInputAssembler.h"

namespace fury
{
	namespace
	{
		void EnsureDummyTextures();

		const char *BLIT_VS =
			"in vec3 vertex_position;\n"
			"out vec2 out_uv;\n"
			"void main() {\n"
			"	out_uv = vertex_position.xy * 0.5 + 0.5;\n"
			"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);\n"
			"}\n";

		const char *BLIT_FS =
			"uniform sampler2D src;\n"
			"in vec2 out_uv;\n"
			"out vec4 fragment_output;\n"
			"void main() {\n"
			"	fragment_output = texture(src, out_uv);\n"
			"}\n";

		const char *DEBUG_VS =
			"#version 330\n"
			"in vec3 vertex_position;\n"
			"uniform mat4 projection_matrix;\n"
			"uniform mat4 invert_view_matrix;\n"
			"uniform mat4 world_matrix;\n"
			"void main() {\n"
			"    gl_Position = projection_matrix * invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);\n"
			"}\n";

		const char *DEBUG_FS =
			"#version 330\n"
			"uniform vec3 color;\n"
			"out vec4 fragment_output;\n"
			"void main() {\n"
			"    fragment_output = vec4(color, 1.0);\n"
			"}\n";

		// GLShaderModule wants complete translation units; inject the default
		// version line the engine shader path prepends to version-less files.
		RHIShaderBlob MakeSourceBlob(const char *source)
		{
			std::string text = source;
			if (text.find("#version") == std::string::npos)
				text.insert(0, "#version 330 core\n");
			RHIShaderBlob blob;
			blob.IsSource = true;
			blob.Data.assign(text.begin(), text.end());
			return blob;
		}

		RHIShaderModuleRef GetBlitShaderModule()
		{
			static RHIShaderModuleRef s_Module;
			static bool s_Attempted = false;
			if (!s_Attempted && GRHIDevice != nullptr && RenderThread::Get().MayUseGL())
			{
				s_Attempted = true;
				s_Module = GRHIDevice->CreateShaderModule("BlitShader",
					MakeSourceBlob(BLIT_VS), MakeSourceBlob(BLIT_FS), RHIShaderBlob());
				if (s_Module == nullptr)
					FURYE << "Failed to create BlitShader module!";
			}
			return s_Module;
		}

		RHIShaderModuleRef GetDebugShaderModule()
		{
			static RHIShaderModuleRef s_Module;
			static bool s_Attempted = false;
			if (!s_Attempted && GRHIDevice != nullptr && RenderThread::Get().MayUseGL())
			{
				s_Attempted = true;
				s_Module = GRHIDevice->CreateShaderModule("DebugShader",
					MakeSourceBlob(DEBUG_VS), MakeSourceBlob(DEBUG_FS), RHIShaderBlob());
				if (s_Module == nullptr)
					FURYE << "Failed to create DebugShader module!";
			}
			return s_Module;
		}

		RHIMeshGeometryDesc MakePositionDesc(const char *meshName, int attrib,
			const RHIBufferRef &buffer, bool dirty, bool empty)
		{
			RHIMeshGeometryDesc desc;
			desc.MeshName = meshName;
			desc.ChannelCount = 1;
			auto &channel = desc.Channels[0];
			channel.Name = "vertex_position";
			channel.Attrib = attrib;
			channel.Buffer = buffer;
			channel.Dirty = dirty;
			channel.Empty = empty;
			channel.Components = 3;
			channel.Integer = false;
			return desc;
		}

		RHIPrimitiveMode LineModeToRHI(LineMode mode)
		{
			switch (mode)
			{
			case LineMode::LINE_LOOP:
				return RHIPrimitiveMode::LINE_LOOP;
			case LineMode::LINE_STRIP:
				return RHIPrimitiveMode::LINE_STRIP;
			case LineMode::LINES:
			default:
				return RHIPrimitiveMode::LINES;
			}
		}
	}

	RenderUtil::RenderUtil()
	{
		m_BlitPass = Pass::Create("BlitPass");
		m_BlitPass->SetBlendMode(BlendMode::REPLACE);
		m_BlitPass->SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);
		m_BlitPass->SetCompareMode(CompareMode::LESS);
		m_BlitPass->SetCullMode(CullMode::NONE);
	}

	RenderUtil::~RenderUtil()
	{
		// GPU handles die on the render thread (direct-exec there).
		RHIBufferRef lineBuffer = m_LineBuffer;
		m_LineBuffer = nullptr;
		RenderThread::Get().EnqueueJob([lineBuffer]() mutable {
			lineBuffer.reset();
		});
	}

	void RenderUtil::Blit(const std::shared_ptr<Texture> &src, const std::shared_ptr<Texture> &dest,
		ClearMode clearMode, BlendMode blendMode)
	{
		auto module = GetBlitShaderModule();
		if (module == nullptr || GRHIDevice == nullptr)
			return;

		if (dest != nullptr)
			m_BlitPass->AddTexture(dest, false);

		m_BlitPass->SetClearMode(clearMode);
		m_BlitPass->SetBlendMode(blendMode);

		m_BlitPass->Bind(true);

		auto quad = MeshUtil::GetUnitQuad();
		if (quad->GetDirty())
			quad->UpdateBuffer();

		auto *cmd = GRHIDevice->GetCommandList();
		cmd->BindShaderModule(module);
		cmd->SetTexture("src", src->GetRHITexture());

		auto desc = MakePositionDesc(quad->GetName().c_str(),
			module->ResolveAttribLocation("vertex_position"),
			quad->Positions.GetRHIBuffer(), quad->Positions.GetDirty(), quad->Positions.Data.empty());
		cmd->SetMeshGeometry(desc);
		cmd->SetIndexBuffer(quad->Indices.GetRHIBuffer());
		cmd->SetPrimitiveMode(RHIPrimitiveMode::TRIANGLES);
		cmd->DrawIndexed(static_cast<unsigned int>(quad->Indices.Data.size()));

		m_BlitPass->UnBind();

		m_BlitPass->RemoveAllTextures();

		m_TriangleCount += 2;
		m_DrawCall++;
	}

	void RenderUtil::Blit(const std::shared_ptr<Texture> &src, const std::shared_ptr<Texture> &dest,
		const std::shared_ptr<Shader> &shader, ClearMode clearMode, BlendMode blendMode)
	{
		if (dest != nullptr)
			m_BlitPass->AddTexture(dest, false);

		m_BlitPass->SetClearMode(clearMode);
		m_BlitPass->SetBlendMode(blendMode);

		m_BlitPass->Bind(true);

		shader->Bind();

		shader->BindTexture(src);
		auto quad = MeshUtil::GetUnitQuad();
		shader->BindMesh(quad);

		if (GRHIDevice != nullptr)
		{
			auto *cmd = GRHIDevice->GetCommandList();
			cmd->SetIndexBuffer(quad->Indices.GetRHIBuffer());
			cmd->SetPrimitiveMode(RHIPrimitiveMode::TRIANGLES);
			cmd->DrawIndexed(static_cast<unsigned int>(quad->Indices.Data.size()));
		}

		shader->UnBind();

		m_BlitPass->UnBind();

		m_BlitPass->RemoveAllTextures();

		m_TriangleCount += 2;
		m_DrawCall++;
	}

	void RenderUtil::BeginDrawLines(const PacketCamera &camera)
	{
		auto module = GetDebugShaderModule();
		if (m_DrawingLine || module == nullptr || GRHIDevice == nullptr)
			return;

		m_DrawingLine = true;

		auto *cmd = GRHIDevice->GetCommandList();
		cmd->BindShaderModule(module);
		cmd->SetUniformMatrix(Matrix4::PROJECTION_MATRIX, camera.projectionMatrix);
		cmd->SetUniformMatrix(Matrix4::INVERT_VIEW_MATRIX, camera.invertWorldMatrix);
		cmd->SetUniformMatrix(Matrix4::WORLD_MATRIX, Matrix4());
	}

	void RenderUtil::BeginDrawLines(const std::shared_ptr<SceneNode> &camera)
	{
		auto module = GetDebugShaderModule();
		if (m_DrawingLine || module == nullptr || GRHIDevice == nullptr)
			return;

		m_DrawingLine = true;

		auto *cmd = GRHIDevice->GetCommandList();
		cmd->BindShaderModule(module);
		if (auto cam = camera->GetComponent<Camera>())
		{
			Matrix4 invView = camera->GetInvertWorldMatrix();
			Matrix4 proj = cam->GetProjectionMatrix();
			cmd->SetUniformMatrix(Matrix4::INVERT_VIEW_MATRIX, invView);
			cmd->SetUniformMatrix(Matrix4::PROJECTION_MATRIX, proj);
		}
		cmd->SetUniformMatrix(Matrix4::WORLD_MATRIX, Matrix4());
	}

	void RenderUtil::DrawLines(const float* positions, unsigned int size, Color color, LineMode lineMode)
	{
		if (!m_DrawingLine)
		{
			FURYE << "Call BeginDrawLines(camera) before DrawLines(xxx)!";
			return;
		}

		if (GRHIDevice == nullptr)
			return;

		auto dataSize = sizeof(float) * size;
		if (m_LineBuffer == nullptr)
		{
			RHIBufferDesc desc;
			desc.Name = "DebugLines";
			desc.Type = RHIBufferType::VERTEX;
			desc.Usage = RHIBufferUsage::DYNAMIC;
			desc.Size = dataSize;
			desc.Stride = sizeof(float);
			m_LineBuffer = GRHIDevice->CreateBuffer(desc, positions);
		}
		else
		{
			GRHIDevice->UpdateBuffer(m_LineBuffer, positions, dataSize, 0);
		}

		if (m_LineBuffer == nullptr)
			return;

		auto module = GetDebugShaderModule();
		auto *cmd = GRHIDevice->GetCommandList();

		auto desc = MakePositionDesc("DebugLines",
			module->ResolveAttribLocation("vertex_position"),
			m_LineBuffer, false, false);
		cmd->SetMeshGeometry(desc);

		cmd->SetUniformFloat("color", color.r, color.g, color.b);
		cmd->SetPrimitiveMode(LineModeToRHI(lineMode));
		cmd->Draw(size / 3);

		m_DrawCall++;
	}

	void RenderUtil::DrawBoxBounds(const BoxBounds &aabb, Color color)
	{
		Vector4 min = aabb.GetMin();
		Vector4 max = aabb.GetMax();

		Vector4 cornors[] = {
			min,
			Vector4(max.x, min.y, min.z, 1.0f),
			Vector4(min.x, max.y, min.z, 1.0f),
			Vector4(max.x, max.y, min.z, 1.0f),
			Vector4(min.x, min.y, max.z, 1.0f),
			Vector4(max.x, min.y, max.z, 1.0f),
			Vector4(min.x, max.y, max.z, 1.0f),
			max
		};

		unsigned int indices[] = { 0, 1, 2, 3, 6, 7, 4, 5, 6, 2, 7, 3, 5, 1, 4, 0, 6, 4, 7, 5, 3, 1, 2, 0 };

		std::vector<float> lines;
		for (unsigned int i : indices)
		{
			Vector4 cornor = cornors[i];
			lines.push_back(cornor.x);
			lines.push_back(cornor.y);
			lines.push_back(cornor.z);
		}

		DrawLines(&lines[0], static_cast<unsigned int>(lines.size()), color);
	}

	void RenderUtil::DrawFrustum(const Frustum &frustum, Color color)
	{
		auto corners = frustum.GetCurrentCorners();

		unsigned int indices[] = { 0, 4, 1, 5, 3, 7, 2, 6, 0, 2, 2, 3, 3, 1, 1, 0, 4, 6, 6, 7, 7, 5, 5, 4 };

		std::vector<float> lines;
		for (unsigned int i : indices)
		{
			Vector4 cornor = corners[i];
			lines.push_back(cornor.x);
			lines.push_back(cornor.y);
			lines.push_back(cornor.z);
		}

		DrawLines(&lines[0], static_cast<unsigned int>(lines.size()), color);
	}

	void RenderUtil::EndDrawLines()
	{
		m_DrawingLine = false;

		if (GRHIDevice != nullptr)
		{
			GLUnbindGeometry();
			GRHIDevice->GetCommandList()->BindShaderModule(nullptr);
		}
	}

	void RenderUtil::BeginDrawMeshs(const PacketCamera &camera)
	{
		auto module = GetDebugShaderModule();
		if (module == nullptr || m_DrawingMesh || GRHIDevice == nullptr)
			return;

		m_DrawingMesh = true;

		auto *cmd = GRHIDevice->GetCommandList();
		cmd->BindShaderModule(module);
		cmd->SetUniformMatrix(Matrix4::PROJECTION_MATRIX, camera.projectionMatrix);
		cmd->SetUniformMatrix(Matrix4::INVERT_VIEW_MATRIX, camera.invertWorldMatrix);
		cmd->SetFillMode(RHIFillMode::WIREFRAME);
	}

	void RenderUtil::BeginDrawMeshs(const std::shared_ptr<SceneNode> &camera)
	{
		auto module = GetDebugShaderModule();
		if (module == nullptr || m_DrawingMesh || GRHIDevice == nullptr)
			return;

		m_DrawingMesh = true;

		auto *cmd = GRHIDevice->GetCommandList();
		cmd->BindShaderModule(module);
		if (auto cam = camera->GetComponent<Camera>())
		{
			Matrix4 invView = camera->GetInvertWorldMatrix();
			Matrix4 proj = cam->GetProjectionMatrix();
			cmd->SetUniformMatrix(Matrix4::INVERT_VIEW_MATRIX, invView);
			cmd->SetUniformMatrix(Matrix4::PROJECTION_MATRIX, proj);
		}
		cmd->SetFillMode(RHIFillMode::WIREFRAME);
	}

	void RenderUtil::DrawMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 &worldMatrix, Color color)
	{
		if (!m_DrawingMesh || mesh->IsSkinnedMesh() || mesh->GetSubMeshCount() > 0)
			return;

		if (GRHIDevice == nullptr)
			return;

		if (mesh->GetDirty())
			mesh->UpdateBuffer();

		auto module = GetDebugShaderModule();
		if (module == nullptr || mesh->GetDirty() || mesh->Indices.GetDirty())
			return;

		auto *cmd = GRHIDevice->GetCommandList();
		cmd->SetUniformFloat("color", color.r, color.g, color.b);
		cmd->SetUniformMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

		auto desc = MakePositionDesc(mesh->GetName().c_str(),
			module->ResolveAttribLocation("vertex_position"),
			mesh->Positions.GetRHIBuffer(), mesh->Positions.GetDirty(), mesh->Positions.Data.empty());
		cmd->SetMeshGeometry(desc);
		cmd->SetIndexBuffer(mesh->Indices.GetRHIBuffer());
		cmd->SetPrimitiveMode(RHIPrimitiveMode::TRIANGLES);
		cmd->DrawIndexed(static_cast<unsigned int>(mesh->Indices.Data.size()));

		m_DrawCall++;
	}

	void RenderUtil::EndDrawMeshes()
	{
		m_DrawingMesh = false;

		if (GRHIDevice != nullptr)
		{
			GRHIDevice->GetCommandList()->SetFillMode(RHIFillMode::SOLID);
			GRHIDevice->GetCommandList()->BindShaderModule(nullptr);
		}
	}

	void RenderUtil::BeginFrame()
	{
		m_FrameClock.restart();

		OnBeginFrame->Emit();
	}

	void RenderUtil::EndFrame()
	{
		m_LastDrawCall = m_DrawCall.load();
		m_LastMeshCount = m_MeshCount.load();
		m_LastTriangleCount = m_TriangleCount.load();
		m_LastSkinnedMeshCount = m_SkinnedMeshCount.load();
		m_LastLightCount = m_LightCount.load();

		m_DrawCall = 0;
		m_MeshCount = 0;
		m_TriangleCount = 0;
		m_SkinnedMeshCount = 0;
		m_LightCount = 0;

		auto frameTime = m_FrameClock.restart().asMilliseconds();
		OnEndFrame->Emit(std::move(frameTime));
	}

	void RenderUtil::IncreaseDrawCall(unsigned int count)
	{
		m_DrawCall += count;
	}

	unsigned int RenderUtil::GetDrawCall()
	{
		return m_LastDrawCall;
	}

	void RenderUtil::IncreaseMeshCount(unsigned int count)
	{
		m_MeshCount += count;
	}

	unsigned int RenderUtil::GetMeshCount()
	{
		return m_LastMeshCount;
	}

	void RenderUtil::IncreaseTriangleCount(unsigned int count)
	{
		m_TriangleCount += count;
	}

	unsigned int RenderUtil::GetTriangleCount()
	{
		return m_LastTriangleCount;
	}

	void RenderUtil::IncreaseSkinnedMeshCount(unsigned int count)
	{
		m_SkinnedMeshCount += count;
	}

	unsigned int RenderUtil::GetSkinnedMeshCount()
	{
		return m_LastSkinnedMeshCount;
	}

	void RenderUtil::IncreaseLightCount(unsigned int count)
	{
		m_LightCount += count;
	}

	unsigned int RenderUtil::GetLightCount()
	{
		return m_LastLightCount;
	}

	std::shared_ptr<Shader> GetSimpleLambertShader()
	{
		static auto shader = Shader::Create("SimpleLambertShader", ShaderType::OTHER);
		if (shader->GetDirty())
		{
			const char *vs =
				"in vec3 vertex_position;"
				"in vec3 vertex_normal;"
				// Referenced (via a zero uniform) so the linker keeps
				// the attributes and skinned meshes don't trip the
				// "Can't find bone_ids/bone_weights" bind warnings --
				// thumbnails render the bind pose.
				"in ivec4 bone_ids;"
				"in vec3 bone_weights;"
				"uniform float u_bone_keep = 0.0;"
				"uniform mat4 _WorldMatrix;"
				"uniform mat4 _ViewMatrix;"
				"uniform mat4 _ProjectionMatrix;"
				"out vec3 v_normal;"
				"void main()"
				"{"
				"	vec4 worldPos = _WorldMatrix * vec4(vertex_position, 1.0);"
				"	v_normal = mat3(_WorldMatrix) * vertex_normal;"
				"	gl_Position = _ProjectionMatrix * _ViewMatrix * worldPos"
				"		+ vec4(u_bone_keep * float(bone_ids.x) * bone_weights.x);"
				"}";
			const char *fs =
				"in vec3 v_normal;"
				"out vec4 fragment_output;"
				"void main()"
				"{"
				"	vec3 n = v_normal;"
				"	float nlen = length(n);"
				"	if (nlen < 0.0001) n = vec3(0.0, 1.0, 0.0);"
				"	else n = n / nlen;"
				"	vec3 lightDir = normalize(vec3(0.4, 0.8, 0.3));"
				"	float ndotl = max(dot(n, lightDir), 0.2);"
				"	fragment_output = vec4(vec3(0.7) * ndotl, 1.0);"
				"}";
			if (!shader->Compile(vs, fs, ""))
				FURYE << "SimpleLambertShader: compile failed (see GLSL error above)";
		}
		return shader;
	}

	namespace
	{
		std::shared_ptr<Texture> s_DummyCube;

		std::shared_ptr<Texture> s_Dummy2D;

		std::shared_ptr<Texture> s_Dummy2DArray;

		std::shared_ptr<Texture> s_Dummy3D;

		// Eagerly built in RenderUtil's ctor (GL is guaranteed current on
		// the main thread there), so the getters below are GL-free and any
		// thread may call them (e.g. Terrain::Load on the game thread).
		void EnsureDummyTextures()
		{
			if (!s_DummyCube)
			{
				s_DummyCube = Texture::Create("DummyCube");
				s_DummyCube->CreateEmpty(1, 1, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_CUBE_MAP, false);
			}
			if (!s_Dummy2D)
			{
				s_Dummy2D = Texture::Create("Dummy2D");
				s_Dummy2D->CreateEmpty(1, 1, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D, false);
			}
			if (!s_Dummy2DArray)
			{
				// 1x1x4 DEPTH24 -- the sampler-target match is what matters.
				s_Dummy2DArray = Texture::Create("Dummy2DArray");
				s_Dummy2DArray->CreateEmpty(1, 1, 4, TextureFormat::DEPTH24, TextureType::TEXTURE_2D_ARRAY, false);
			}
			if (!s_Dummy3D)
			{
				// 1x1x1 RGBA8 -- the sampler-target match is what matters.
				s_Dummy3D = Texture::Create("Dummy3D");
				s_Dummy3D->CreateEmpty(1, 1, 1, TextureFormat::RGBA8, TextureType::TEXTURE_3D, false);
			}
		}
	}

	std::shared_ptr<Texture> GetDummyCubeTexture()
	{
		EnsureDummyTextures();
		return s_DummyCube;
	}

	std::shared_ptr<Texture> GetDummyTexture2D()
	{
		EnsureDummyTextures();
		return s_Dummy2D;
	}

	std::shared_ptr<Texture> GetDummyTexture2DArray()
	{
		EnsureDummyTextures();
		return s_Dummy2DArray;
	}

	std::shared_ptr<Texture> GetDummyTexture3D()
	{
		EnsureDummyTextures();
		return s_Dummy3D;
	}

	std::shared_ptr<Shader> GetParticleShader(bool shadow)
	{
		// Particle billboard shader, loaded from
		// Resource/Shader/Lambert/Particle.glsl (artists iterate on GLSL
		// without rebuilding). shadow=true adds the SHADOW define -- the
		// shadow-receive block compiles only into that variant.
		static auto plain = Shader::Create("ParticleShader", ShaderType::PARTICLE);
		static auto shadowed = [] {
			auto s = Shader::Create("ParticleShaderShadow", ShaderType::PARTICLE);
			s->AddDefine("SHADOW");
			return s;
		}();
		auto &shader = shadow ? shadowed : plain;
		if (shader->GetDirty())
			shader->LoadAndCompile(
				FileUtil::GetAbsPath() + "Resource/Shader/Lambert/Particle.glsl", false);
		return shader;
	}

	float RenderMeshLambert(const std::shared_ptr<Mesh> &mesh, int w, int h,
		const std::vector<Matrix4> *palette)
	{
		if (!mesh) return 0.0f;

		if (GRHIDevice == nullptr)
			return 0.0f;

		if (mesh->GetDirty())
			mesh->UpdateBuffer();
		for (unsigned int s = 0; s < mesh->GetSubMeshCount(); ++s)
		{
			auto sm = mesh->GetSubMeshAt(s);
			if (sm && sm->GetDirty())
				sm->UpdateBuffer();
		}

		auto shader = GetSimpleLambertShader();
		shader->Bind();

		// Frame on the mesh's local AABB (mesh placed at world origin).
		auto aabb = mesh->GetAABB();
		auto mn = aabb.GetMin();
		auto mx = aabb.GetMax();
		Vector4 center((mn.x + mx.x) * 0.5f,
					   (mn.y + mx.y) * 0.5f,
					   (mn.z + mx.z) * 0.5f, 1.0f);
		Vector4 size(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 0);
		float radius = 0.5f * std::sqrt(
			size.x * size.x + size.y * size.y + size.z * size.z);
		// Only fall back for a truly degenerate AABB; tiny-but-valid meshes
		// (e.g. sub-mm glTF assets) must keep their real radius so the camera
		// frames tightly instead of sitting 1.5m away from a sub-pixel mesh.
		if (radius < 1e-6f) radius = 0.5f;

		const float fov = 45.0f * 0.0174532925f;
		const float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.0f;
		const float dist = (radius * 0.6f) /
			(std::tan(fov * 0.5f) * std::min(1.0f, aspect));
		const float az = 30.0f * 0.0174532925f;
		const float el = 20.0f * 0.0174532925f;
		const float cx = std::cos(el) * std::cos(az);
		const float cy = std::sin(el);
		const float cz = std::cos(el) * std::sin(az);
		Vector4 eye(center.x + cx * dist,
					center.y + cy * dist,
					center.z + cz * dist, 1.0f);
		Matrix4 world, view, proj;
		world.Identity();
		view.LookAt(eye, center, Vector4(0, 1, 0, 0));
		proj.PerspectiveFov(fov, aspect,
			std::max(radius * 0.05f, 1e-5f), dist + radius * 5.0f);
		shader->BindMatrix("_WorldMatrix", world);
		shader->BindMatrix("_ViewMatrix", view);
		shader->BindMatrix("_ProjectionMatrix", proj);

		auto *cmd = GRHIDevice->GetCommandList();

		const bool bindPalette = palette != nullptr && !palette->empty();
		auto bindMesh = [&]() {
			if (bindPalette)
				shader->BindMesh(mesh, palette->data(), static_cast<int>(palette->size()));
			else
				shader->BindMesh(mesh);
		};

		auto submeshCount = mesh->GetSubMeshCount();
		if (submeshCount == 0)
		{
			bindMesh();
			cmd->SetIndexBuffer(mesh->Indices.GetRHIBuffer());
			cmd->SetPrimitiveMode(RHIPrimitiveMode::TRIANGLES);
			cmd->DrawIndexed(static_cast<unsigned int>(mesh->Indices.Data.size()));
		}
		else
		{
			// BindMesh binds the mesh's VAO (position/normal/uv attributes);
			// BindSubMesh only swaps the index buffer.
			bindMesh();
			for (unsigned int i = 0; i < submeshCount; ++i)
			{
				auto sm = mesh->GetSubMeshAt(i);
				if (!sm) continue;
				shader->BindSubMesh(mesh, i);
				cmd->SetIndexBuffer(sm->Indices.GetRHIBuffer());
				cmd->SetPrimitiveMode(RHIPrimitiveMode::TRIANGLES);
				cmd->DrawIndexed(static_cast<unsigned int>(sm->Indices.Data.size()));
			}
		}
		return radius;
	}
}
