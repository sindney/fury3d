#include "Camera.h"
#include "Log.h"
#include "GLLoader.h"
#include "EnumUtil.h"
#include "EntityManager.h"
#include "FileUtil.h"
#include "Joint.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "SceneNode.h"
#include "Shader.h"
#include "Texture.h"
#include "Uniform.h"

namespace fury
{
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

	bool Shader::Load(const void* wrapper)
	{
		EntityManager::Ptr entityMgr = EntityManager::Instance();
		std::string str;

		if (!LoadMemberValue(wrapper, "type", str))
		{
			FURYE << "Shader param 'type' not found!";
			return false;
		}
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

		if (!LoadMemberValue(wrapper, "path", str))
		{
			FURYE << "Shader param 'path' not found!";
			return false;
		}
		LoadAndCompile(FileUtil::GetAbsPath() + str);

		return true;
	}

	bool Shader::Save(void* wrapper)
	{
		StartObject(wrapper);

		SaveKey(wrapper, "name");
		SaveValue(wrapper, m_Name);

		SaveKey(wrapper, "type");
		SaveValue(wrapper, EnumUtil::ShaderTypeToString(m_Type));

		std::vector<ShaderTexture> enums;
		EnumUtil::GetShaderTextures(m_TextureFlags, enums);

		SaveKey(wrapper, "textures");
		SaveArray(wrapper, enums.size(), [&](unsigned int index)
		{
			SaveValue(wrapper, EnumUtil::ShaderTextureToString(enums[index]));
		});

		SaveKey(wrapper, "path");
		SaveValue(wrapper, m_FilePath);

		EndObject(wrapper);

		return true;
	}

	unsigned int Shader::GetProgram() const
	{
		return m_Program;
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

	bool Shader::LoadAndCompile(const std::string &shaderPath)
	{
		std::string dataStr;
		if (FileUtil::LoadString(shaderPath, dataStr))
		{
			m_FilePath = shaderPath;
			return Compile(dataStr, dataStr);
		}
		else
		{
			FURYW << m_Name << " load failed!";
			return false;
		}
	}

	bool Shader::Compile(const std::string &vsData, const std::string &fsData)
	{
		DeleteProgram();

		std::string defines;
		GetDefines(defines);

		std::string vsVersion, vsMain;
		GetVersionInfo(vsData, vsVersion, vsMain);
		vsVersion += "#define VERTEX_SHADER\n";

		// compile vertex shader
		unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
		if (vertexShader != 0)
		{
			const char *sources[3] = { vsVersion.c_str(), defines.c_str(), vsMain.c_str() };
			const int counts[3] = { (int)vsVersion.size(), (int)defines.size(), (int)vsMain.size() };
			glShaderSource(vertexShader, 3, sources, counts);
			glCompileShader(vertexShader);

			GLint status;
			glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
			if (status != GL_TRUE)
			{
				vertexShader = 0;
				FURYE << m_Name << "'s vertex shader compile failed!";
				return false;
			}
		}
		else
		{
			FURYE << "Failed to create vertex shader context!";
			return false;
		}

		std::string fsVersion, fsMain;
		GetVersionInfo(fsData, fsVersion, fsMain);
		fsVersion += "#define FRAGMENT_SHADER\n";

		// compile fragment shader
		unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		if (fragmentShader != 0)
		{
			const char *sources[3] = { fsVersion.c_str(), defines.c_str(), fsMain.c_str() };
			const int counts[3] = { (int)fsVersion.size(), (int)defines.size(), (int)fsMain.size() };
			glShaderSource(fragmentShader, 3, sources, counts);
			glCompileShader(fragmentShader);

			GLint status;
			glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &status);
			if (status != GL_TRUE)
			{
				glDeleteShader(vertexShader);
				vertexShader = fragmentShader = 0;
				FURYE << m_Name << "'s fragment compile failed!";
				return false;
			}
		}
		else
		{
			FURYE << "Failed to create fragment shader context!";
			return false;
		}

		// link to program
		m_Program = glCreateProgram();
		if (m_Program != 0)
		{
			glAttachShader(m_Program, vertexShader);
			glAttachShader(m_Program, fragmentShader);
			glLinkProgram(m_Program);

			glDetachShader(m_Program, vertexShader);
			glDetachShader(m_Program, fragmentShader);
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);

			GLint status;
			glGetProgramiv(m_Program, GL_LINK_STATUS, &status);
			if (status != GL_TRUE)
			{
				glDeleteProgram(m_Program);
				m_Program = fragmentShader = vertexShader = 0;
				FURYE << m_Name << " link failed!";
				return false;
			}
		}
		else
		{
			FURYE << "Failed to create shader program context!";
			return false;
		}
		
		m_Dirty = false;
		FURYD << m_Name << " compile & link success!";
		return true;
	}

	void Shader::DeleteProgram()
	{
		if (m_Program != 0)
		{
			glDeleteProgram(m_Program);
			m_Program = 0;
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

		glUseProgram(m_Program);
		m_TextureID = GL_TEXTURE0;
	}

	void Shader::BindCamera(const std::shared_ptr<SceneNode> &camNode)
	{
		Vector4 camPos = camNode->GetWorldPosition();
		if (auto camera = camNode->GetComponent<Camera>())
		{
			BindFloat("camera_pos", camPos.x, camPos.y, camPos.z);
			BindFloat("camera_far", camera->GetFar());
			BindFloat("camera_near", camera->GetNear());
			BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &camNode->GetInvertWorldMatrix().Raw[0]);
			BindMatrix(Matrix4::PROJECTION_MATRIX, &camera->GetProjectionMatrix().Raw[0]);
		}
	}

	void Shader::BindLight(const std::shared_ptr<SceneNode> &lightNode)
	{
		Vector4 lightPos = lightNode->GetWorldPosition();
		Vector4 lightDir = lightNode->GetWorldMatrix().Multiply(Vector4(0, -1, 0, 0));
		lightDir.Normalize();
		if (auto light = lightNode->GetComponent<Light>())
		{
			Color color = light->GetColor();
			BindFloat("light_pos", lightPos.x, lightPos.y, lightPos.z);
			BindFloat("light_dir", lightDir.x, lightDir.y, lightDir.z);
			BindFloat("light_color", color.r, color.g, color.b);
			BindFloat("light_intensity", light->GetIntensity());
			BindFloat("light_innerangle", light->GetInnerAngle());
			BindFloat("light_outterangle", light->GetOutterAngle());
			BindFloat("light_falloff", light->GetFalloff());
			BindFloat("light_radius", light->GetRadius());
		}
	}

	void Shader::BindTexture(const std::string &name, const std::shared_ptr<Texture> &texture)
	{
		if (texture->GetDirty())
		{
			FURYW << "Binding dirty texture!";
			return;
		}

		int id = GetUniformLocation(name);

		if (id != -1)
		{
			glActiveTexture(m_TextureID);
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glUniform1i(id, m_TextureID - GL_TEXTURE0);

			m_TextureID++;
		}
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
				ptr->Bind(m_Program, it->first.c_str());
		}
	}

	void Shader::BindMeshData(const std::shared_ptr<Mesh> &mesh)
	{
		int posFlag = glGetAttribLocation(m_Program, mesh->Positions.Name.c_str());
		int normalFlag = glGetAttribLocation(m_Program, mesh->Normals.Name.c_str());
		int tangentFlag = glGetAttribLocation(m_Program, mesh->Tangents.Name.c_str());
		int uvFlag = glGetAttribLocation(m_Program, mesh->UVs.Name.c_str());

		glBindVertexArray(mesh->m_VAO);

		if (posFlag != -1)
		{
			if (!mesh->Positions.GetDirty())
			{
				glBindBuffer(GL_ARRAY_BUFFER, mesh->Positions.GetID());
				glVertexAttribPointer(posFlag, 3, GL_FLOAT, GL_FALSE, 0, 0);
				glEnableVertexAttribArray(posFlag);
			}
			else
			{
				FURYW << "Mesh " + mesh->GetName() + " Position data dirty!";
			}
		}
		if (normalFlag != -1)
		{
			if (!mesh->Normals.GetDirty())
			{
				glBindBuffer(GL_ARRAY_BUFFER, mesh->Normals.GetID());
				glVertexAttribPointer(normalFlag, 3, GL_FLOAT, GL_FALSE, 0, 0);
				glEnableVertexAttribArray(normalFlag);
			}
			else
			{
				FURYW << "Mesh " + mesh->GetName() + " Normal data dirty!";
			}
		}
		if (tangentFlag != -1)
		{
			if (!mesh->Tangents.GetDirty())
			{
				glBindBuffer(GL_ARRAY_BUFFER, mesh->Tangents.GetID());
				glVertexAttribPointer(tangentFlag, 3, GL_FLOAT, GL_FALSE, 0, 0);
				glEnableVertexAttribArray(tangentFlag);
			}
			else
			{
				FURYW << "Mesh" + mesh->GetName() + " Tangent data dirty!";
			}
		}
		if (uvFlag != -1)
		{
			if (!mesh->UVs.GetDirty())
			{
				glBindBuffer(GL_ARRAY_BUFFER, mesh->UVs.GetID());
				glVertexAttribPointer(uvFlag, 2, GL_FLOAT, GL_FALSE, 0, 0);
				glEnableVertexAttribArray(uvFlag);
			}
			else
			{
				FURYW << "Mesh " + mesh->GetName() + " UV data dirty!";
			}
		}

		if (mesh->IsSkinnedMesh())
		{
			int idFlag = glGetAttribLocation(m_Program, mesh->IDs.Name.c_str());
			int weightFlag = glGetAttribLocation(m_Program, mesh->Weights.Name.c_str());

			if (idFlag != -1)
			{
				if (!mesh->IDs.GetDirty())
				{
					glBindBuffer(GL_ARRAY_BUFFER, mesh->IDs.GetID());
					glVertexAttribIPointer(idFlag, 4, GL_UNSIGNED_INT, 0, 0);
					glEnableVertexAttribArray(idFlag);
				}
				else
				{
					FURYW << "Mesh " + mesh->GetName() + " ID data dirty!";
				}
			}
			else
			{
				FURYW << "Can't find " << mesh->IDs.Name << " in " << m_Name;
			}

			if (weightFlag != -1)
			{
				if (!mesh->Weights.GetDirty())
				{
					glBindBuffer(GL_ARRAY_BUFFER, mesh->Weights.GetID());
					glVertexAttribPointer(weightFlag, 3, GL_FLOAT, GL_FALSE, 0, 0);
					glEnableVertexAttribArray(weightFlag);
				}
				else
				{
					FURYW << "Mesh " + mesh->GetName() + " Weight data dirty!";
				}
			}
			else
			{
				FURYW << "Can't find " << mesh->Weights.Name << " in " << m_Name;
			}

			if (idFlag != -1 && weightFlag != -1)
			{
				int jointCount = (int)mesh->GetJointCount();
				if (jointCount > 35)
				{
					FURYW << "Max joint count 35!";
					jointCount = 35;
				}

				std::vector<float> raw(jointCount * 16);

				for (int i = 0; i < jointCount; i++)
				{
					auto joint = mesh->GetJointAt(i);
					auto matrix = joint->GetFinalMatrix();
					int index = i * 16;

					for (int j = 0; j < 16; j++)
					{
						raw[index + j] = matrix.Raw[j];
					}
				}

				BindMatrices("bone_matrices", jointCount, &raw[0]);
			}
		}
		
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void Shader::BindMesh(const std::shared_ptr<Mesh> &mesh)
	{
		if (mesh->GetDirty())
			mesh->UpdateBuffer();

		if (m_Dirty || mesh->GetDirty() || mesh->Indices.GetDirty())
			return;

		BindMeshData(mesh);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->Indices.GetID());
	}

	void Shader::BindSubMesh(const std::shared_ptr<Mesh> &mesh, unsigned int index)
	{
		auto subMesh = mesh->GetSubMeshAt(index);
		if (subMesh == nullptr)
		{
			FURYW << "SubMesh out of range!";
			return;
		}

		if (mesh->GetDirty())
			mesh->UpdateBuffer();

		if (subMesh->GetDirty())
			subMesh->UpdateBuffer();

		if (m_Dirty || mesh->GetDirty() || subMesh->GetDirty() || subMesh->Indices.GetDirty())
			return;

		BindMeshData(mesh);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, subMesh->Indices.GetID());
	}

	void Shader::BindMatrix(const std::string &name, const Matrix4 &matrix)
	{
		BindMatrix(name, &matrix.Raw[0]);
	}

	void Shader::BindMatrix(const std::string &name, const float *raw)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniformMatrix4fv(id, 1, false, raw);
	}

	void Shader::BindMatrices(const std::string &name, int count, const float *raw)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniformMatrix4fv(id, count, false, raw);
	}

	void Shader::BindFloat(const std::string &name, float v0)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform1f(id, v0);
	}

	void Shader::BindFloat(const std::string &name, float v0, float v1)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform2f(id, v0, v1);
	}

	void Shader::BindFloat(const std::string &name, float v0, float v1, float v2)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform3f(id, v0, v1, v2);
	}

	void Shader::BindFloat(const std::string &name, float v0, float v1, float v2, float v3)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform4f(id, v0, v1, v2, v3);
	}

	void Shader::BindFloat(const std::string &name, int size, int count, const float *value)
	{
		int id = GetUniformLocation(name);
		if (id == -1) return;

		switch (size)
		{
		case 1:
			glUniform1fv(id, count, value);
			break;
		case 2:
			glUniform2fv(id, count, value);
			break;
		case 3:
			glUniform3fv(id, count, value);
			break;
		case 4:
			glUniform4fv(id, count, value);
			break;
		default:
			FURYW << "Incorrect unfirom size!";
			break;
		}
	}

	void Shader::BindInt(const std::string &name, int v0)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform1i(id, v0);
	}

	void Shader::BindInt(const std::string &name, int v0, int v1)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform2i(id, v0, v1);
	}

	void Shader::BindInt(const std::string &name, int v0, int v1, int v2)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform3i(id, v0, v1, v2);
	}

	void Shader::BindInt(const std::string &name, int v0, int v1, int v2, int v3)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform4i(id, v0, v1, v2, v3);
	}

	void Shader::BindInt(const std::string &name, int size, int count, const int *value)
	{
		int id = GetUniformLocation(name);
		if (id == -1) return;

		switch (size)
		{
		case 1:
			glUniform1iv(id, count, value);
			break;
		case 2:
			glUniform2iv(id, count, value);
			break;
		case 3:
			glUniform3iv(id, count, value);
			break;
		case 4:
			glUniform4iv(id, count, value);
			break;
		default:
			FURYW << "Incorrect unfirom size!";
			break;
		}
	}

	void Shader::BindUInt(const std::string &name, unsigned int v0)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform1ui(id, v0);
	}

	void Shader::BindUInt(const std::string &name, unsigned int v0, unsigned int v1)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform2ui(id, v0, v1);
	}

	void Shader::BindUInt(const std::string &name, unsigned int v0, unsigned int v1, unsigned int v2)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform3ui(id, v0, v1, v2);
	}

	void Shader::BindUInt(const std::string &name, unsigned int v0, unsigned int v1, unsigned int v2, unsigned int v3)
	{
		int id = GetUniformLocation(name);
		if (id != -1)
			glUniform4ui(id, v0, v1, v2, v3);
	}

	void Shader::BindUInt(const std::string &name, int size, int count, const unsigned int *value)
	{
		int id = GetUniformLocation(name);
		if (id == -1) return;

		switch (size)
		{
		case 1:
			glUniform1uiv(id, count, value);
			break;
		case 2:
			glUniform2uiv(id, count, value);
			break;
		case 3:
			glUniform3uiv(id, count, value);
			break;
		case 4:
			glUniform4uiv(id, count, value);
			break;
		default:
			FURYW << "Incorrect unfirom size!";
			break;
		}
	}

	void Shader::UnBind()
	{
		m_TextureID = GL_TEXTURE0;

		glUseProgram(0);

		glBindTexture(GL_TEXTURE_2D, 0);

		glBindVertexArray(0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	int Shader::GetUniformLocation(const std::string &name) const
	{
		if (m_Dirty)
			return -1;

		return glGetUniformLocation(m_Program, name.c_str());
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
			versionStr = "#version 330";
			mainStr = source;
		}
		else
		{
			auto start = index + versionStr.size();
			auto size = source.size() - start;
			mainStr = source.substr(0, index) + source.substr(start, size);
		}
	}

	void Shader::GetDefines(std::string &definesStr)
	{
		switch (m_Type)
		{
		case ShaderType::DIR_LIGHT:
			definesStr = "#define DIR_LIGHT\n";
			break;
		case ShaderType::POINT_LIGHT:
			definesStr = "#define POINT_LIGHT\n";
			break;
		case ShaderType::SPOT_LIGHT:
			definesStr = "#define SPOT_LIGHT\n";
			break;
		case ShaderType::STATIC_MESH:
			definesStr = "#define STATIC_MESH\n";
			break;
		case ShaderType::SKINNED_MESH:
			definesStr = "#define SKINNED_MESH\n";
			break;
		default:
			break;
		}
	}
}