#include "Fury/Camera.h"
#include "Fury/Log.h"
#include "Fury/EnumUtil.h"
#include "Fury/FileUtil.h"
#include "Fury/Joint.h"
#include "Fury/Light.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/ShaderCache.h"

#include "Fury/FramePacket.h"
#include "Fury/RenderThread.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"

#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/RHI/OpenGL/GLInputAssembler.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace fury
{
	namespace
	{
		// Inline quoted relative includes: `#include "Name.glsl"` resolves
		// against the including file's directory, recursively. Included
		// files must not carry a #version line (stripped here). Cycles and
		// deep nesting fail with a logged error, never a hang.
		bool ResolveShaderIncludes(const std::string &src, const std::string &dir,
			std::vector<std::string> &stack, int depth, std::string &out)
		{
			if (depth > 8)
			{
				FURYE << "Shader include depth exceeded at " << dir;
				return false;
			}

			std::stringstream stream(src);
			std::string line;
			while (std::getline(stream, line))
			{
				auto start = line.find_first_not_of(" \t");
				if (start != std::string::npos && line.compare(start, 8, "#include") == 0)
				{
					auto open = line.find('"', start + 8);
					auto close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
					if (open == std::string::npos || close == std::string::npos)
					{
						FURYE << "Malformed shader include (only quoted relative paths): " << line;
						return false;
					}

					std::string path = dir + line.substr(open + 1, close - open - 1);
					if (std::find(stack.begin(), stack.end(), path) != stack.end())
					{
						FURYE << "Shader include cycle: " << stack.back() << " <-> " << path;
						return false;
					}

					std::string incSrc;
					if (!FileUtil::LoadString(path, incSrc))
					{
						FURYE << "Shader include not found: " << path;
						return false;
					}

					stack.push_back(path);
					std::string childDir = path.substr(0, path.find_last_of('/') + 1);
					std::string resolved;
					if (!ResolveShaderIncludes(incSrc, childDir, stack, depth + 1, resolved))
						return false;
					stack.pop_back();

					std::stringstream incStream(resolved);
					std::string incLine;
					while (std::getline(incStream, incLine))
					{
						auto v = incLine.find_first_not_of(" \t");
						if (v != std::string::npos && incLine.compare(v, 8, "#version") == 0)
							continue;
						out += incLine;
						out += '\n';
					}
					continue;
				}

				out += line;
				out += '\n';
			}
			return true;
		}

		RHICommandList *Cmd()
		{
			return GRHIDevice ? GRHIDevice->GetCommandList() : nullptr;
		}

		RHIShaderBlob MakeSourceBlob(const std::string &source)
		{
			RHIShaderBlob blob;
			blob.Data.assign(source.begin(), source.end());
			blob.IsSource = true;
			return blob;
		}
	}

	Shader::Ptr Shader::Create(const std::string &name, ShaderType type, unsigned int textureFlags)
	{
		return std::make_shared<Shader>(name, type, textureFlags);
	}

	Shader::Shader(const std::string &name, ShaderType type, unsigned int textureFlags)
		: Entity(name), m_Type(type), m_TextureFlags(textureFlags)
	{
		m_TypeIndex = typeid(Shader);
	}

	Shader::~Shader()
	{
		DeleteProgram();
	}

	bool Shader::Load(const void* wrapper, bool object)
	{
		std::string str;

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		m_Type = ShaderType::OTHER;
		if (LoadMemberValue(wrapper, "type", str))
			m_Type = EnumUtil::ShaderTypeFromString(str);

		// read shader texture flags

		std::vector<ShaderTexture> textures;

		m_TextureFlags = 0;
		if (LoadArray(wrapper, "textures", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				FURYE << "Shader's texture flag not found!";
				return false;
			}
			textures.push_back(EnumUtil::ShaderTextureFromString(str));
			return true;
		}))
		{
			for (auto texture : textures)
				m_TextureFlags = m_TextureFlags | (unsigned int)texture;
		}

		// end: read shader texture flags

		LoadArray(wrapper, "defines", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				FURYE << "Shader's define must be string!";
				return false;
			}
			m_Defines.push_back(str);
			return true;
		});

		if (!LoadMemberValue(wrapper, "path", str))
		{
			FURYE << "Shader param 'path' not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "geom", m_UseGeomShader))
			m_UseGeomShader = false;

		LoadAndCompile(FileUtil::GetAbsPath() + str, m_UseGeomShader);

		return true;
	}

	void Shader::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "type");
		SaveValue(wrapper, EnumUtil::ShaderTypeToString(m_Type));

		std::vector<ShaderTexture> enums;
		EnumUtil::GetShaderTextures(m_TextureFlags, enums);

		SaveKey(wrapper, "textures");
		SaveArray(wrapper, static_cast<unsigned int>(enums.size()), [&](unsigned int index)
		{
			SaveValue(wrapper, EnumUtil::ShaderTextureToString(enums[index]));
		});

		SaveKey(wrapper, "defines");
		SaveArray(wrapper, static_cast<unsigned int>(m_Defines.size()), [&](unsigned int index)
		{
			SaveValue(wrapper, m_Defines[index]);
		});

		SaveKey(wrapper, "path");
		SaveValue(wrapper, m_FilePath);

		if (m_UseGeomShader)
		{
			SaveKey(wrapper, "geom");
			SaveValue(wrapper, m_UseGeomShader);
		}

		if (object)
			EndObject(wrapper);
	}

	bool Shader::GetDirty() const
	{
		return m_Dirty;
	}

	std::string Shader::GetFilePath() const
	{
		return m_FilePath;
	}

	ShaderType Shader::GetType() const
	{
		return m_Type;
	}

	unsigned int Shader::GetTextureFlags() const
	{
		return m_TextureFlags;
	}

	void Shader::SetTextureFlags(unsigned int flags)
	{
		m_TextureFlags = flags;
	}

	void Shader::AddDefine(std::string define)
	{
		m_Defines.push_back(define);
	}

	bool Shader::LoadAndCompile(const std::string &shaderPath, bool useGeomShader)
	{
		if (!RenderThread::Get().MayUseGL())
		{
			// Deferred compile: report success optimistically; failure
			// surfaces as a missing program on first bind (same as a
			// compile error today) plus the error log from the GL thread.
			DispatchRHI(this, [this, shaderPath, useGeomShader]() {
				LoadAndCompile(shaderPath, useGeomShader);
			});
			return true;
		}
		FURY_RHI_THREAD_GUARD();
		m_UseGeomShader = useGeomShader;

		std::string dataStr;
		if (FileUtil::LoadString(shaderPath, dataStr))
		{
			auto slash = shaderPath.find_last_of('/');
			std::string dir = slash == std::string::npos ? "" : shaderPath.substr(0, slash + 1);

			std::vector<std::string> stack;
			stack.push_back(shaderPath);
			std::string resolved;
			if (!ResolveShaderIncludes(dataStr, dir, stack, 0, resolved))
			{
				FURYE << m_Name << " include resolution failed for " << shaderPath;
				return false;
			}

			m_FilePath = shaderPath;
			return Compile(resolved, resolved, m_UseGeomShader ? resolved : "");
		}
		else
		{
			FURYW << m_Name << " load failed!";
			return false;
		}
	}

	bool Shader::Compile(const std::string &vsData, const std::string &fsData, const std::string &gsData)
	{
		DeleteProgram();

		m_UseGeomShader = gsData.size() > 0;

#if WITH_EDITOR
		constexpr bool withEditor = true;
#else
		constexpr bool withEditor = false;
#endif
#if WITH_DBG_OVERLAY
		constexpr bool withDebugOverlay = true;
#else
		constexpr bool withDebugOverlay = false;
#endif
		const std::string vsSource = BuildShaderStageSource(vsData, "VERTEX_SHADER", m_Defines,
			withEditor, withDebugOverlay);
		const std::string fsSource = BuildShaderStageSource(fsData, "FRAGMENT_SHADER", m_Defines,
			withEditor, withDebugOverlay);
		const std::string gsSource = m_UseGeomShader
			? BuildShaderStageSource(gsData, "GEOMETRY_SHADER", m_Defines, withEditor, withDebugOverlay)
			: std::string{};

		if (GRHIDevice)
		{
			m_Module = GRHIDevice->CreateShaderModule(m_Name,
				MakeSourceBlob(vsSource), MakeSourceBlob(fsSource), MakeSourceBlob(gsSource));
		}

		m_Dirty = m_Module == nullptr || !m_Module->IsValid();
		return !m_Dirty;
	}

	bool Shader::LoadAndCompileCompute(const std::string &shaderPath)
	{
		if (!RenderThread::Get().MayUseGL())
		{
			DispatchRHI(this, [this, shaderPath]() {
				LoadAndCompileCompute(shaderPath);
			});
			return true;
		}

		std::string dataStr;
		if (!FileUtil::LoadString(shaderPath, dataStr))
		{
			FURYW << m_Name << " load failed!";
			return false;
		}

		auto slash = shaderPath.find_last_of('/');
		std::string dir = slash == std::string::npos ? "" : shaderPath.substr(0, slash + 1);

		std::vector<std::string> stack;
		stack.push_back(shaderPath);
		std::string resolved;
		if (!ResolveShaderIncludes(dataStr, dir, stack, 0, resolved))
		{
			FURYE << m_Name << " include resolution failed for " << shaderPath;
			return false;
		}

		m_FilePath = shaderPath;
		return CompileCompute(resolved);
	}

	bool Shader::CompileCompute(const std::string &source)
	{
		DeleteProgram();

		std::stringstream defineStream;
#if WITH_EDITOR
		defineStream << "#define WITH_EDITOR\n";
#endif
#if WITH_DBG_OVERLAY
		defineStream << "#define WITH_DBG_OVERLAY\n";
#endif
		for (auto define : m_Defines)
			defineStream << "#define " << define << "\n";

		std::string version, mainStr;
		GetVersionInfo(source, version, mainStr);
		if (source.find("#version") == std::string::npos)
			version = "#version 430 core\n"; // compute needs 4.3; default 330 won't do

		std::string src = version + "\n#define COMPUTE_SHADER\n" + defineStream.str() + mainStr;

		if (GRHIDevice)
			m_Module = GRHIDevice->CreateComputeModule(m_Name, MakeSourceBlob(src));

		m_Dirty = m_Module == nullptr || !m_Module->IsValid();
		return !m_Dirty;
	}

	void Shader::DispatchCompute(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ)
	{
		if (m_Dirty)
		{
			FURYW << "DispatchCompute on dirty shader " << m_Name;
			return;
		}

		auto *cmd = Cmd();
		if (cmd == nullptr)
			return;

		cmd->BindShaderModule(m_Module);
		cmd->Dispatch(groupsX, groupsY, groupsZ);
	}

	void Shader::ComputeBarrier()
	{
		if (GRHIDevice)
			GRHIDevice->GetCommandList()->Barrier();
	}

	void Shader::DeleteProgram()
	{
		if (m_Module != nullptr)
		{
			RHIShaderModuleRef module = std::move(m_Module);
			m_Module.reset();
			RenderThread::Get().EnqueueJob([module]() mutable { module.reset(); });
		}

		m_Dirty = true;
	}

	void Shader::Bind()
	{
		if (m_Dirty)
		{
			FURYW << "Binding dirty shader!";
			return;
		}

		auto *cmd = Cmd();
		if (cmd)
			cmd->BindShaderModule(m_Module);
	}

	void Shader::BindCamera(const std::shared_ptr<SceneNode> &camNode)
	{
		Vector4 camPos = camNode->GetWorldPosition();
		if (auto camera = camNode->GetComponent<Camera>())
		{
			BindFloat("camera_pos", camPos.x, camPos.y, camPos.z);
			BindFloat("camera_far", camera->GetFar());
			BindFloat("camera_near", camera->GetNear());
			Matrix4 invView = camNode->GetInvertWorldMatrix();
			Matrix4 proj = camera->GetProjectionMatrix();
			BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &invView.Raw[0]);
			BindMatrix(Matrix4::PROJECTION_MATRIX, &proj.Raw[0]);
		}
	}

	void Shader::BindLight(const std::shared_ptr<SceneNode> &lightNode)
	{
		static float pi = 3.141592653f;

		Vector4 lightPos = lightNode->GetWorldPosition();
		Vector4 lightDir = lightNode->GetWorldMatrix().Multiply(Vector4(0, -1, 0, 0));
		lightDir.Normalize();
		if (auto light = lightNode->GetComponent<Light>())
		{
			Color color = light->GetColor();
			BindFloat("light_pos", lightPos.x, lightPos.y, lightPos.z);
			BindFloat("light_dir", lightDir.x, lightDir.y, lightDir.z);
			BindFloat("light_color", color.r / pi, color.g / pi, color.b / pi);
			BindFloat("light_intensity", light->GetIntensity());
			BindFloat("light_innerangle", light->GetInnerAngle());
			BindFloat("light_outterangle", light->GetOutterAngle());
			BindFloat("light_falloff", light->GetFalloff());
			BindFloat("light_radius", light->GetEffectiveRadius());
		}
	}

	void Shader::BindCameraData(const PacketCamera &cam)
	{
		BindFloat("camera_pos", cam.worldPos.x, cam.worldPos.y, cam.worldPos.z);
		BindFloat("camera_far", cam.farClip);
		BindFloat("camera_near", cam.nearClip);
		BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &cam.invertWorldMatrix.Raw[0]);
		BindMatrix(Matrix4::PROJECTION_MATRIX, &cam.projectionMatrix.Raw[0]);
	}

	void Shader::BindLightData(const PacketLight &light)
	{
		static float pi = 3.141592653f;
		BindFloat("light_pos", light.worldPos.x, light.worldPos.y, light.worldPos.z);
		BindFloat("light_dir", light.worldDir.x, light.worldDir.y, light.worldDir.z);
		BindFloat("light_color", light.color.r / pi, light.color.g / pi, light.color.b / pi);
		BindFloat("light_intensity", light.intensity);
		BindFloat("light_innerangle", light.innerAngle);
		BindFloat("light_outterangle", light.outterAngle);
		BindFloat("light_falloff", light.falloff);
		BindFloat("light_radius", light.effectiveRadius);
	}

	void Shader::BindTexture(const std::shared_ptr<Texture> &texture)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->BindTextureUnit(texture ? texture->GetRHITexture() : nullptr);
	}

	void Shader::BindTexture(const std::string &name, const std::shared_ptr<Texture> &texture)
	{
		if (texture->GetDirty())
		{
			FURYW << "Binding dirty texture '" << texture->GetName()
				<< "' (path='" << texture->GetFilePath() << "', id=" << texture->GetID()
				<< ") for uniform '" << name << "'!";
			return;
		}

		auto *cmd = Cmd();
		if (cmd)
			cmd->SetTexture(name, texture->GetRHITexture());
	}

	void Shader::BindMaterial(const std::shared_ptr<Material> &material)
	{
		if (m_Dirty)
			return;

		for (auto it = material->m_Textures.begin(); it != material->m_Textures.end(); ++it)
		{
			BindTexture(it->first, it->second);
		}

		for (auto it = material->m_Uniforms.begin(); it != material->m_Uniforms.end(); ++it)
		{
			UniformBase::Ptr &ptr = it->second;
			if (ptr != nullptr)
				ptr->Bind(it->first);
		}
	}

	void Shader::BindMeshData(const std::shared_ptr<Mesh> &mesh)
	{
		BindMeshData(mesh, nullptr, 0);
	}

	void Shader::BindMeshData(const std::shared_ptr<Mesh> &mesh, const Matrix4 *palette, int paletteCount)
	{
		auto module = m_Module;
		if (module == nullptr)
		{
			fprintf(stderr, "BindMeshData '%s': module null (dirty=%d)\n", m_Name.c_str(), (int)m_Dirty);
			return;
		}

		RHIMeshGeometryDesc desc;
		std::string meshName = mesh->GetName();
		desc.MeshName = meshName.c_str();

		int channel = 0;
		auto addChannel = [&desc, &channel](const std::string &name, int attrib,
			const RHIBufferRef &buffer, bool dirty, bool empty, int components, bool integer)
		{
			auto &ch = desc.Channels[channel++];
			ch.Name = name.c_str();
			ch.Attrib = attrib;
			ch.Buffer = buffer;
			ch.Dirty = dirty;
			ch.Empty = empty;
			ch.Components = components;
			ch.Integer = integer;
		};

		addChannel(mesh->Positions.Name, module->ResolveAttribLocation(mesh->Positions.Name),
			mesh->Positions.GetRHIBuffer(), mesh->Positions.GetDirty(),
			mesh->Positions.Data.empty(), 3, false);
		addChannel(mesh->Normals.Name, module->ResolveAttribLocation(mesh->Normals.Name),
			mesh->Normals.GetRHIBuffer(), mesh->Normals.GetDirty(),
			mesh->Normals.Data.empty(), 3, false);
		addChannel(mesh->Tangents.Name, module->ResolveAttribLocation(mesh->Tangents.Name),
			mesh->Tangents.GetRHIBuffer(), mesh->Tangents.GetDirty(),
			mesh->Tangents.Data.empty(), 3, false);
		addChannel(mesh->UVs.Name, module->ResolveAttribLocation(mesh->UVs.Name),
			mesh->UVs.GetRHIBuffer(), mesh->UVs.GetDirty(),
			mesh->UVs.Data.empty(), 2, false);
		addChannel(mesh->Colors.Name, module->ResolveAttribLocation(mesh->Colors.Name),
			mesh->Colors.GetRHIBuffer(), mesh->Colors.GetDirty(),
			mesh->Colors.Data.empty(), 4, false);

		int idAttrib = -1;
		int weightAttrib = -1;
		if (mesh->IsSkinnedMesh())
		{
			idAttrib = module->ResolveAttribLocation(mesh->IDs.Name);
			weightAttrib = module->ResolveAttribLocation(mesh->Weights.Name);

			addChannel(mesh->IDs.Name, idAttrib,
				mesh->IDs.GetRHIBuffer(), mesh->IDs.GetDirty(),
				mesh->IDs.Data.empty(), 4, true);
			addChannel(mesh->Weights.Name, weightAttrib,
				mesh->Weights.GetRHIBuffer(), mesh->Weights.GetDirty(),
				mesh->Weights.Data.empty(), 3, false);
		}

		desc.ChannelCount = channel;

		GRHIDevice->GetCommandList()->SetMeshGeometry(desc);

		if (mesh->IsSkinnedMesh() && idAttrib != -1 && weightAttrib != -1)
		{
			int jointCount = (int)mesh->GetJointCount();
			if (jointCount > 35)
			{
				FURYW << "Max joint count 35!";
				jointCount = 35;
			}

			if (palette != nullptr && paletteCount > 0)
			{
				// Render-thread path: copied palette from the packet.
				BindMatrices("bone_matrices",
					std::min(jointCount, paletteCount), (const float*)palette);
			}
			else
			{
				std::vector<float> raw(jointCount * 16);

				for (int i = 0; i < jointCount; i++)
				{
					auto joint = mesh->GetJointAt(i);
					// Picking can run mid-reimport when a joint slot is null -- use identity.
					Matrix4 matrix = joint ? joint->GetFinalMatrix() : Matrix4();
					int index = i * 16;

					for (int j = 0; j < 16; j++)
					{
						raw[index + j] = matrix.Raw[j];
					}
				}

				BindMatrices("bone_matrices", jointCount, &raw[0]);
			}
		}
	}

	void Shader::BindMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 *palette, int paletteCount)
	{
		if (mesh->GetDirty())
			mesh->UpdateBuffer();

		const bool parent_indices_relevant = (mesh->GetSubMeshCount() == 0);
		if (m_Dirty || mesh->GetDirty() ||
			(parent_indices_relevant && mesh->Indices.GetDirty()))
			return;

		BindMeshData(mesh, palette, paletteCount);

		auto *cmd = Cmd();
		if (cmd)
			cmd->SetIndexBuffer(mesh->Indices.GetRHIBuffer());
	}

	void Shader::BindMesh(const std::shared_ptr<Mesh> &mesh)
	{
		if (mesh->GetDirty())
			mesh->UpdateBuffer();

		// For meshes with submeshes the parent Indices buffer is unused
		// (each submesh carries its own index list), so an empty parent
		// Indices shouldn't disqualify the bind. Check it only when
		// there are no submeshes.
		const bool parent_indices_relevant = (mesh->GetSubMeshCount() == 0);
		if (m_Dirty || mesh->GetDirty() ||
			(parent_indices_relevant && mesh->Indices.GetDirty()))
			return;

		BindMeshData(mesh);

		auto *cmd = Cmd();
		if (cmd)
			cmd->SetIndexBuffer(mesh->Indices.GetRHIBuffer());
	}

	void Shader::BindSubMesh(const std::shared_ptr<Mesh> &mesh, unsigned int index)
	{
		auto subMesh = mesh->GetSubMeshAt(index);
		if (subMesh == nullptr)
		{
			FURYW << "SubMesh out of range!";
			return;
		}

		if (subMesh->GetDirty())
			subMesh->UpdateBuffer();

		// Same parent-Indices gate as BindMesh above: meshes whose
		// indices live in submeshes have an empty (permanently-dirty)
		// parent Indices buffer that must not block the bind.
		const bool parent_indices_relevant = (mesh->GetSubMeshCount() == 0);
		if (m_Dirty || mesh->GetDirty() || subMesh->GetDirty() ||
			subMesh->Indices.GetDirty() ||
			(parent_indices_relevant && mesh->Indices.GetDirty()))
			return;

		auto *cmd = Cmd();
		if (cmd)
			cmd->SetIndexBuffer(subMesh->Indices.GetRHIBuffer());
	}

	void Shader::BindMatrix(const std::string &name, const Matrix4 &matrix)
	{
		BindMatrix(name, &matrix.Raw[0]);
	}

	void Shader::BindMatrix(const std::string &name, const float *raw)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformMatrix(name, raw);
	}

	void Shader::BindMatrices(const std::string &name, int count, const float *raw)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformMatrices(name, count, raw);
	}

	void Shader::BindMatrices(const std::string &name, const int count, const Matrix4 *matrices)
	{
		std::vector<float> raw(count * 16);
		for (int i = 0; i < count; i++)
		{
			auto &matrix = matrices[i];
			for (int j = 0; j < 16; j++)
				raw[i * 16 + j] = matrix.Raw[j];
		}

		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformMatrices(name, count, &raw[0]);
	}

	void Shader::BindFloat(const std::string &name, float v0)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloat(name, v0);
	}

	void Shader::BindFloat(const std::string &name, float v0, float v1)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloat(name, v0, v1);
	}

	void Shader::BindFloat(const std::string &name, float v0, float v1, float v2)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloat(name, v0, v1, v2);
	}

	void Shader::BindFloat(const std::string &name, float v0, float v1, float v2, float v3)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloat(name, v0, v1, v2, v3);
	}

	void Shader::BindFloat(const std::string &name, int size, int count, const float *value)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloatArray(name, size, count, value);
	}

	void Shader::BindInt(const std::string &name, int v0)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformInt(name, v0);
	}

	void Shader::BindInt(const std::string &name, int v0, int v1)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformInt(name, v0, v1);
	}

	void Shader::BindInt(const std::string &name, int v0, int v1, int v2)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformInt(name, v0, v1, v2);
	}

	void Shader::BindInt(const std::string &name, int v0, int v1, int v2, int v3)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformInt(name, v0, v1, v2, v3);
	}

	void Shader::BindInt(const std::string &name, int size, int count, const int *value)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformIntArray(name, size, count, value);
	}

	void Shader::BindUInt(const std::string &name, unsigned int v0)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformUInt(name, v0);
	}

	void Shader::BindUInt(const std::string &name, unsigned int, unsigned int)
	{
		FURYW << "BindUInt multi-value variant is not supported by the RHI: " << name;
	}

	void Shader::BindUInt(const std::string &name, unsigned int, unsigned int, unsigned int)
	{
		FURYW << "BindUInt multi-value variant is not supported by the RHI: " << name;
	}

	void Shader::BindUInt(const std::string &name, unsigned int, unsigned int, unsigned int, unsigned int)
	{
		FURYW << "BindUInt multi-value variant is not supported by the RHI: " << name;
	}

	void Shader::BindUInt(const std::string &name, int, int, const unsigned int *)
	{
		FURYW << "BindUInt array variant is not supported by the RHI: " << name;
	}

	void Shader::UnBind()
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->BindShaderModule(nullptr);

		GLUnbindGeometry();
	}

	void Shader::BindFloatLocation(int location, float x)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloatAt(location, x);
	}

	void Shader::BindFloatLocation(int location, float x, float y)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloatAt(location, x, y);
	}

	void Shader::BindFloatLocation(int location, float x, float y, float z)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloatAt(location, x, y, z);
	}

	void Shader::BindFloatLocation(int location, float x, float y, float z, float w)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformFloatAt(location, x, y, z, w);
	}

	void Shader::BindIntLocation(int location, int x)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformIntAt(location, x);
	}

	void Shader::BindMatrixLocation(int location, const float *matrix)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformMatrixAt(location, matrix);
	}

	void Shader::BindMatricesLocation(int location, int count, const float *matrices)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetUniformMatricesAt(location, count, matrices);
	}

	int Shader::BindTextureAt(int unitOffset, int location, const std::shared_ptr<Texture> &texture)
	{
		auto *cmd = Cmd();
		if (cmd == nullptr)
			return unitOffset;

		cmd->SetTextureUnitCursor(unitOffset);
		return cmd->SetTextureAt(location, texture ? texture->GetRHITexture() : nullptr);
	}

	void Shader::SetTextureUnitCursor(int unitOffset)
	{
		auto *cmd = Cmd();
		if (cmd)
			cmd->SetTextureUnitCursor(unitOffset);
	}

	int Shader::GetUniformLocation(const std::string &name) const
	{
		return (m_Dirty || !m_Module) ? -1 : m_Module->ResolveUniform(name);
	}

	void Shader::GetVersionInfo(const std::string &source, std::string &versionStr, std::string &mainStr)
	{
		auto index = source.find("#version");
		if (index != std::string::npos)
		{
			auto eol = source.find('\n', index);
			if (eol != std::string::npos)
			{
				versionStr = source.substr(index, eol - index + 1);
			}
		}

		if (versionStr.size() == 0)
		{
			versionStr = "#version 330 core";
			mainStr = source;
		}
		else
		{
			auto start = index + versionStr.size();
			auto size = source.size() - start;
			mainStr = source.substr(0, index) + source.substr(start, size);
		}
	}
}
