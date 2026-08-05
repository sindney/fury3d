#include <SFML/System/Time.hpp>

#include <cmath>

#include "Fury/RenderUtil.h"
#include "Fury/BoxBounds.h"
#include "Fury/FileUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Vector4.h"
#include "Fury/Shader.h"
#include "Fury/SceneNode.h"
#include "Fury/Frustum.h"
#include "Fury/Mesh.h"
#include "Fury/MeshUtil.h"
#include "Fury/Texture.h"

namespace fury
{
	RenderUtil::RenderUtil()
	{
		// blit shader
		{
			const char *blit_vs = 
				"in vec3 vertex_position;\n"
				"out vec2 out_uv;\n"
				"void main() {\n"
				"	out_uv = vertex_position.xy * 0.5 + 0.5;\n"
				"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);\n"
				"}\n";

			const char *blit_fs = 
				"uniform sampler2D src;\n"
				"in vec2 out_uv;\n"
				"out vec4 fragment_output;\n"
				"void main() {\n"
				"	fragment_output = texture(src, out_uv);\n"
				"}\n";

			m_BlitShader = Shader::Create("BlitShader", ShaderType::OTHER);
			if (!m_BlitShader->Compile(blit_vs, blit_fs, ""))
				FURYE << "Failed to compile line shader!";
		}

		// debug shader
		{
			const char *debug_vs =
			"#version 330\n"
			"in vec3 vertex_position;\n"
			"uniform mat4 projection_matrix;\n"
			"uniform mat4 invert_view_matrix;\n"
			"uniform mat4 world_matrix;\n"
			"void main() {\n"
			"    gl_Position = projection_matrix * invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);\n"
			"}\n";

		const char *debug_fs =
			"#version 330\n"
			"uniform vec3 color;\n"
			"out vec4 fragment_output;\n"
			"void main() {\n"
			"    fragment_output = vec4(color, 1.0);\n"
			"}\n";

		m_DebugShader = Shader::Create("DebugShader", ShaderType::OTHER);
		if (!m_DebugShader->Compile(debug_vs, debug_fs, ""))
			FURYE << "Failed to compile line shader!";

		m_DebugShader->Bind();

		auto shaderId = m_DebugShader->GetProgram();

		glBindAttribLocation(shaderId, 0, "vertex_position");

		glGenVertexArrays(1, &m_LineVAO);
		glGenBuffers(1, &m_LineVBO);

		glBindVertexArray(m_LineVAO);

		glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
		glEnableVertexAttribArray(0);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		m_DebugShader->UnBind();
		}

		m_BlitPass = Pass::Create("BlitPass");
		m_BlitPass->SetBlendMode(BlendMode::REPLACE);
		m_BlitPass->SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);
		m_BlitPass->SetCompareMode(CompareMode::LESS);
		m_BlitPass->SetCullMode(CullMode::NONE);
	}

	RenderUtil::~RenderUtil()
	{
		if (m_LineVAO != 0)
			glDeleteVertexArrays(1, &m_LineVAO);

		if (m_LineVBO != 0)
			glDeleteBuffers(1, &m_LineVBO);
	}

	void RenderUtil::Blit(const std::shared_ptr<Texture> &src, const std::shared_ptr<Texture> &dest, 
		ClearMode clearMode, BlendMode blendMode)
	{
		Blit(src, dest, m_BlitShader, clearMode, blendMode);
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
		shader->BindMesh(MeshUtil::GetUnitQuad());

		glDrawElements(GL_TRIANGLES, MeshUtil::GetUnitQuad()->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		m_BlitPass->UnBind();

		m_BlitPass->RemoveAllTextures();

		m_TriangleCount += 2;
		m_DrawCall++;
	}

	void RenderUtil::BeginDrawLines(const std::shared_ptr<SceneNode> &camera)
	{
		if (m_DrawingLine || m_LineVAO == 0 || m_LineVBO == 0 || m_DebugShader->GetDirty())
			return;

		m_DrawingLine = true;

		m_DebugShader->Bind();
		m_DebugShader->BindCamera(camera);
		m_DebugShader->BindMatrix(Matrix4::WORLD_MATRIX, Matrix4());

		glBindVertexArray(m_LineVAO);
		glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
	}

	void RenderUtil::DrawLines(const float* positions, unsigned int size, Color color, LineMode lineMode)
	{
		if (!m_DrawingLine)
		{
			FURYE << "Call BeginDrawLines(camera) before DrawLines(xxx)!";
			return;
		}

		auto dataSize = sizeof(float) * size;
		glBufferData(GL_ARRAY_BUFFER, dataSize, 0, GL_STREAM_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, positions);

		m_DebugShader->BindFloat("color", color.r, color.g, color.b);
		
		glDrawArrays(EnumUtil::LineModeToUnit(lineMode), 0, size / 3);

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

		DrawLines(&lines[0], lines.size(), color);
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

		DrawLines(&lines[0], lines.size(), color);
	}

	void RenderUtil::EndDrawLines()
	{
		m_DrawingLine = false;

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		m_DebugShader->UnBind();
	}

	void RenderUtil::BeginDrawMeshs(const std::shared_ptr<SceneNode> &camera)
	{
		if (m_DebugShader->GetDirty() || m_DrawingMesh)
			return;

		m_DrawingMesh = true;

		m_DebugShader->Bind();
		m_DebugShader->BindCamera(camera);

		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}

	void RenderUtil::DrawMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 &worldMatrix, Color color)
	{
		if (!m_DrawingMesh || mesh->IsSkinnedMesh() || mesh->GetSubMeshCount() > 0)
			return;

		m_DebugShader->BindFloat("color", color.r, color.g, color.b);
		m_DebugShader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);
		m_DebugShader->BindMesh(mesh);

		glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		m_DrawCall++;
	}

	void RenderUtil::EndDrawMeshes()
	{
		m_DrawingMesh = false;
		m_DebugShader->UnBind();

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	void RenderUtil::BeginFrame()
	{
		m_FrameClock.restart();

		OnBeginFrame->Emit();
	}

	void RenderUtil::EndFrame()
	{
		m_LastDrawCall = m_DrawCall;
		m_LastMeshCount = m_MeshCount;
		m_LastTriangleCount = m_TriangleCount;
		m_LastSkinnedMeshCount = m_SkinnedMeshCount;
		m_LastLightCount = m_LightCount;

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
				// "Can't find bone_ids/bone_weights" bind warnings —
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

	std::shared_ptr<Texture> GetDummyCubeTexture()
	{
		static auto tex = Texture::Create("DummyCube");
		if (tex->GetID() == 0)
			tex->CreateEmpty(1, 1, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_CUBE_MAP, false);
		return tex;
	}

	std::shared_ptr<Texture> GetDummyTexture2D()
	{
		static auto tex = Texture::Create("Dummy2D");
		if (tex->GetID() == 0)
			tex->CreateEmpty(1, 1, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D, false);
		return tex;
	}

	std::shared_ptr<Texture> GetDummyTexture2DArray()
	{
		// 1x1x4 DEPTH24 — the sampler-target match is what matters.
		static auto tex = Texture::Create("Dummy2DArray");
		if (tex->GetID() == 0)
			tex->CreateEmpty(1, 1, 4, TextureFormat::DEPTH24, TextureType::TEXTURE_2D_ARRAY, false);
		return tex;
	}

	std::shared_ptr<Shader> GetParticleShader(bool shadow)
	{
		// Particle billboard shader, loaded from
		// Resource/Shader/Lambert/Particle.glsl (artists iterate on GLSL
		// without rebuilding). shadow=true adds the SHADOW define — the
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

	float RenderMeshLambert(const std::shared_ptr<Mesh> &mesh, int w, int h)
	{
		if (!mesh) return 0.0f;

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

		auto submeshCount = mesh->GetSubMeshCount();
		if (submeshCount == 0)
		{
			shader->BindMesh(mesh);
			glDrawElements(GL_TRIANGLES,
				static_cast<GLsizei>(mesh->Indices.Data.size()),
				GL_UNSIGNED_INT, 0);
		}
		else
		{
			// BindMesh binds the mesh's VAO (position/normal/uv attributes);
			// BindSubMesh only swaps the index buffer.
			shader->BindMesh(mesh);
			for (unsigned int i = 0; i < submeshCount; ++i)
			{
				auto sm = mesh->GetSubMeshAt(i);
				if (!sm) continue;
				shader->BindSubMesh(mesh, i);
				glDrawElements(GL_TRIANGLES,
					static_cast<GLsizei>(sm->Indices.Data.size()),
					GL_UNSIGNED_INT, 0);
			}
		}
		return radius;
	}
}