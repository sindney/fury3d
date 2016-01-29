#include "Debug.h"
#include "GLLoader.h"
#include "Material.h"
#include "Texture.h"
#include "Uniform.h"

namespace fury
{
	const std::string Material::DIFFUSE_TEXTURE = "diffuse_texture";

	const std::string Material::SPECULAR_TEXTURE = "specular_texture";

	const std::string Material::NORMAL_TEXTURE = "normal_texture";

	const std::string Material::SHININESS = "shininess";

	const std::string Material::AMBIENT_FACTOR = "ambient_factor";

	const std::string Material::DIFFUSE_FACTOR = "diffuse_factor";

	const std::string Material::SPECULAR_FACTOR = "specular_factor";

	const std::string Material::AMBIENT_COLOR = "ambient_color";

	const std::string Material::DIFFUSE_COLOR = "diffuse_color";

	const std::string Material::SPECULAR_COLOR = "specular_color";

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
		: Entity(name), m_Opaque(true), m_ID(GetMaterialID())
	{
		m_TypeIndex = typeid(Material);
		m_Dirty = false;
	}

	Material::~Material()
	{
		DeleteBuffer();
		LOGD << "Material: " << m_Name << " Destoried!";
	}

	void Material::DeleteBuffer()
	{
		m_Textures.clear();
		m_Uniforms.clear();
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