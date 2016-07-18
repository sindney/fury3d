#include <unordered_map>
#include <functional>

#include "Log.h"
#include "GLLoader.h"
#include "Material.h"
#include "Shader.h"
#include "Texture.h"
#include "Uniform.h"

namespace fury
{
	const std::string Material::DIFFUSE_TEXTURE = "diffuse_texture";

	const std::string Material::SPECULAR_TEXTURE = "specular_texture";

	const std::string Material::NORMAL_TEXTURE = "normal_texture";

	const std::string Material::SHININESS = "shininess";

	const std::string Material::TRANSPARENCY = "transparency";

	const std::string Material::AMBIENT_FACTOR = "ambient_factor";

	const std::string Material::DIFFUSE_FACTOR = "diffuse_factor";

	const std::string Material::SPECULAR_FACTOR = "specular_factor";

	const std::string Material::EMISSIVE_FACTOR = "emissive_factor";

	const std::string Material::AMBIENT_COLOR = "ambient_color";

	const std::string Material::DIFFUSE_COLOR = "diffuse_color";

	const std::string Material::SPECULAR_COLOR = "specular_color";

	const std::string Material::EMISSIVE_COLOR = "emissive_color";

	const std::string Material::MATERIAL_ID = "material_id";

	Material::Ptr Material::Create(const std::string &name)
	{
		return std::make_shared<Material>(name);
	}

	unsigned int Material::m_GlobalID = 0;

	unsigned int Material::GetMaterialID()
	{
		return ++m_GlobalID;
	}

	unsigned int Material::GetID() const
	{
		return m_ID;
	}

	Material::Material(const std::string &name)
		: Entity(name), m_Opaque(true), m_ID(GetMaterialID()), m_TextureFlags(0)
	{
		m_TypeIndex = typeid(Material);
		m_Dirty = false;
	}

	Material::~Material()
	{
		DeleteBuffer();
		FURYD << "Material: " << m_Name << " Destoried!";
	}

	bool Material::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		LoadMemberValue(wrapper, "opaque", m_Opaque);
		LoadMemberValue(wrapper, "texture_flags", m_TextureFlags);

		// load shaders
		if (!LoadArray(wrapper, "shaders", [&](const void* node) -> bool
		{
			auto shader = Shader::Create("temp", ShaderType::OTHER);
			if (shader->Load(node))
			{
				m_Shaders.push_back(shader);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			return false;
		}

		// load textures
		std::string key;
		if (!LoadArray(wrapper, "textures", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "key", key))
			{
				FURYE << "Texture's key not found!";
				return false;
			}

			auto texture = Texture::Create("temp");
			if (texture->Load(node))
			{
				SetTexture(key, texture);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			return false;
		}

		// load uniforms
		static const std::unordered_map<std::string, std::function<UniformBase::Ptr()>> s_UniformMap =
		{
			{ "Uniform1f", []()-> UniformBase::Ptr { return Uniform1f::Create({}); } },
			{ "Uniform2f", []()-> UniformBase::Ptr { return Uniform2f::Create({}); } },
			{ "Uniform3f", []()-> UniformBase::Ptr { return Uniform3f::Create({}); } },
			{ "Uniform4f", []()-> UniformBase::Ptr { return Uniform4f::Create({}); } },
			{ "UniformMatrix4fv", []()-> UniformBase::Ptr { return UniformMatrix4fv::Create({}); } },
			{ "Uniform1i", []()-> UniformBase::Ptr { return Uniform1i::Create({}); } },
			{ "Uniform2i", []()-> UniformBase::Ptr { return Uniform2i::Create({}); } },
			{ "Uniform3i", []()-> UniformBase::Ptr { return Uniform3i::Create({}); } },
			{ "Uniform4i", []()-> UniformBase::Ptr { return Uniform4i::Create({}); } },
			{ "Uniform1ui", []()-> UniformBase::Ptr { return Uniform1ui::Create({}); } },
			{ "Uniform2ui", []()-> UniformBase::Ptr { return Uniform2ui::Create({}); } },
			{ "Uniform3ui", []()-> UniformBase::Ptr { return Uniform3ui::Create({}); } },
			{ "Uniform4ui", []()-> UniformBase::Ptr { return Uniform4ui::Create({}); } }
		};

		std::string type;
		if (!LoadArray(wrapper, "uniforms", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "key", key))
			{
				FURYE << "Uniform's key not found!";
				return false;
			}

			if (!LoadMemberValue(node, "type", type))
			{
				FURYE << "Uniform's type not found!";
				return false;
			}

			auto it = s_UniformMap.find(type);
			if (it == s_UniformMap.end())
			{
				FURYE << "Uniform's type is incorrect!";
				return false;
			}

			auto uniform = it->second();
			uniform->Load(node);
			SetUniform(key, uniform);
			return true;
		}))
		{
			return false;
		}

		SetUniform(Material::MATERIAL_ID, Uniform1ui::Create({ GetID() }));

		return true;
	}

	void Material::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "opaque");
		SaveValue(wrapper, m_Opaque);
		SaveKey(wrapper, "texture_flags");
		SaveValue(wrapper, m_TextureFlags);

		// save shaders
		SaveKey(wrapper, "shaders");
		SaveArray(wrapper, m_Shaders.size(), [&](unsigned int index)
		{
			m_Shaders[index]->Save(wrapper);
		});

		// save textures
		SaveKey(wrapper, "textures");
		SaveArray<TextureMap>(wrapper, m_Textures, [&](TextureMap::const_iterator it)
		{
			// we inject param 'key' to texture object
			StartObject(wrapper);
			SaveKey(wrapper, "key");
			SaveValue(wrapper, it->first);
			it->second->Save(wrapper, false);
			EndObject(wrapper);
		});

		// save uniforms
		SaveKey(wrapper, "uniforms");
		SaveArray<UniformMap>(wrapper, m_Uniforms, [&](UniformMap::const_iterator it)
		{
			StartObject(wrapper);

			SaveKey(wrapper, "key");
			SaveValue(wrapper, it->first);

			it->second->Save(wrapper, false);

			EndObject(wrapper);
		});

		if (object)
			EndObject(wrapper);
	}

	void Material::DeleteBuffer()
	{
		m_Textures.clear();
		m_Uniforms.clear();
	}

	unsigned int Material::GetTextureFlags() const
	{
		return m_TextureFlags;
	}

	Texture::Ptr Material::GetTexture(const std::string &name) const
	{
		auto it = m_Textures.find(name);
		if (it != m_Textures.end())
			return it->second;
		else
			return nullptr;
	}

	void Material::SetTexture(const std::string &name, const Texture::Ptr &ptr)
	{
		auto it = m_Textures.find(name);
		if (it != m_Textures.end())
		{
			if (ptr != nullptr)
				it->second = ptr;
			else
				m_Textures.erase(it);
		}
		else if (ptr != nullptr)
		{
			m_Textures[name] = ptr;
		}

		// calculate new matching shaderType
		bool hasTexture = false;
		m_TextureFlags = 0;

		for (auto pair : m_Textures)
		{
			if (pair.first == DIFFUSE_TEXTURE)
			{
				hasTexture = true;
				m_TextureFlags = m_TextureFlags | (unsigned int)ShaderTexture::DIFFUSE;
				break;
			}
			if (pair.first == SPECULAR_TEXTURE)
			{
				hasTexture = true;
				m_TextureFlags = m_TextureFlags | (unsigned int)ShaderTexture::SPECULAR;
				break;
			}
			if (pair.first == NORMAL_TEXTURE)
			{
				hasTexture = true;
				m_TextureFlags = m_TextureFlags | (unsigned int)ShaderTexture::NORMAL;
				break;
			}
		}

		if (!hasTexture)
			m_TextureFlags = (unsigned int)ShaderTexture::COLOR_ONLY;
	}

	unsigned int Material::GetTextureCount() const
	{
		return m_Textures.size();
	}

	void Material::SetUniform(const std::string &name, const std::shared_ptr<UniformBase> &ptr)
	{
		auto it = m_Uniforms.find(name);
		if (it != m_Uniforms.end())
		{
			if (ptr != nullptr)
				it->second = ptr;
			else
				m_Uniforms.erase(it);
		}
		else if (ptr != nullptr)
		{
			m_Uniforms[name] = ptr;
		}
	}

	std::shared_ptr<UniformBase> Material::GetUniform(const std::string &name)
	{
		auto it = m_Uniforms.find(name);
		if (it != m_Uniforms.end())
			return it->second;
		else
			return nullptr;
	}

	unsigned int Material::GetUniformCount() const
	{
		return m_Uniforms.size();
	}

	void Material::SetShaderForPass(unsigned int index, const std::shared_ptr<Shader> &shader)
	{
		if (index >= m_Shaders.size())
			m_Shaders.resize(index + 1);

		m_Shaders[index] = shader;
	}

	std::shared_ptr<Shader> Material::GetShaderForPass(unsigned int index)
	{
		if (index < m_Shaders.size())
			return m_Shaders[index];

		return nullptr;
	}

	bool Material::GetOpaque() const
	{
		return m_Opaque;
	}

	void Material::SetOpaque(bool value)
	{
		m_Opaque = value;
	}
}