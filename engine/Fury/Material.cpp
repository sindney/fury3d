#include <map>
#include <unordered_map>
#include <functional>

#include "Fury/Archive.h"
#include "Fury/EntityManager.h"
#include "Fury/Log.h"
#include "Fury/GLLoader.h"
#include "Fury/Material.h"
#include "Fury/Scene.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"

namespace fury
{
	namespace
	{
		// Uniform type name -> fresh instance; unknown names return nullptr.
		UniformBase::Ptr CreateUniformByType(const std::string &type)
		{
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

			auto it = s_UniformMap.find(type);
			return it == s_UniformMap.end() ? nullptr : it->second();
		}
	}

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

	const std::string Material::METALLIC_FACTOR = "metallic_factor";

	const std::string Material::ROUGHNESS_FACTOR = "roughness_factor";

	const std::string Material::METALLIC_ROUGHNESS_TEXTURE = "metallic_roughness_texture";

	const std::string Material::OCCLUSION_TEXTURE = "occlusion_texture";

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
		// A fresh material has no textures: it is color-only. (The zero
		// default silently matched the pass's first textured shader, which
		// then sampled unit-0's leftover texture - invisible/garbage output.)
		: Entity(name), m_TextureFlags((unsigned int)ShaderTexture::COLOR_ONLY), m_AlphaMode(AlphaMode::OPAQUE), m_ID(GetMaterialID())
	{
		m_TypeIndex = typeid(Material);
		m_Dirty = false;
	}

	Material::~Material()
	{
		DeleteBuffer();
		FURYD << "Material: " << m_Name << " Destoried!";
	}

	void Material::Serialize(Archive& ar)
	{
		Entity::SerializeFields(ar);

		std::string alphaMode = EnumUtil::AlphaModeToString(m_AlphaMode);
		ar << Optional("opaque", m_Opaque);
		ArchiveFieldResult alphaResult = ar.TransferNamed("alpha_mode", alphaMode,
			ArchiveFieldPolicy::Optional, true);
		ar << Optional("alpha_cutoff", m_AlphaCutoff);
		ar << Optional("two_sided", m_TwoSided);
		ar << Optional("wind_enabled", m_WindEnabled);
		ar << Optional("pre_z", m_PreZ);
		ar << Optional("texture_flags", m_TextureFlags);

		if (ar.IsLoading())
		{
			// Files with alpha_mode use it; files without derive the mode
			// from opaque (opaque -> OPAQUE, !opaque -> BLEND).
			if (alphaResult == ArchiveFieldResult::Ok)
				SetAlphaMode(EnumUtil::AlphaModeFromString(alphaMode));
			else
				SetAlphaMode(m_Opaque ? AlphaMode::OPAQUE : AlphaMode::BLEND);
		}

		// shaders: full inline Shader objects, one per pass slot.
		ar << Required("shaders", m_Shaders);

		// textures: {"key", <inline Texture fields>}. Loading binds the
		// EntityManager's canonical entry for the path when one is
		// registered (the top-level array loads first); the fresh instance
		// still registers so its saved uuid resolves.
		if (ar.EnterArray("textures", ArchiveFieldPolicy::Required))
		{
			if (ar.IsLoading())
			{
				for (std::size_t i = 0; i < ar.GetArraySize(); ++i)
				{
					ar.SetArrayIndex(i);
					if (!ar.EnterObject()) continue;
					std::string key;
					ar << Required("key", key);
					auto texture = Texture::Create("temp");
					texture->Serialize(ar);
					if (!texture->GetFilePath().empty() && Scene::Active)
					{
						if (auto em = Scene::Active->GetEntityManager())
						{
							em->Add(texture);
							if (auto canonical = em->Get<Texture>(texture->GetFilePath()))
								texture = canonical;
						}
					}
					SetTexture(key, texture);
					ar.LeaveObject();
				}
			}
			else
			{
				// Sorted by slot key so the document does not depend on
				// the map's hash order.
				std::map<std::string, Texture::Ptr> sorted(m_Textures.begin(), m_Textures.end());
				for (const auto& kv : sorted)
				{
					if (!ar.EnterObject()) continue;
					std::string key = kv.first;
					ar << Required("key", key);
					kv.second->Serialize(ar);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}

		// uniforms: {"key", "type", "data"}; type picks the concrete class.
		if (ar.EnterArray("uniforms", ArchiveFieldPolicy::Required))
		{
			if (ar.IsLoading())
			{
				for (std::size_t i = 0; i < ar.GetArraySize(); ++i)
				{
					ar.SetArrayIndex(i);
					if (!ar.EnterObject()) continue;
					std::string key;
					ar << Required("key", key);
					std::string type;
					ar << Required("type", type);
					auto uniform = CreateUniformByType(type);
					if (uniform)
					{
						uniform->Serialize(ar);
					}
					else
					{
						ar.SetError("unknown uniform type '" + type + "'");
					}
					SetUniform(key, uniform);
					ar.LeaveObject();
				}
			}
			else
			{
				// The material_id uniform holds a runtime counter the loader
				// regenerates; it is not persistent state.
				std::map<std::string, UniformBase::Ptr> sorted;
				for (const auto& kv : m_Uniforms)
				{
					if (kv.first == Material::MATERIAL_ID) continue;
					sorted.emplace(kv.first, kv.second);
				}
				for (const auto& kv : sorted)
				{
					if (!ar.EnterObject()) continue;
					std::string key = kv.first;
					ar << Required("key", key);
					kv.second->Serialize(ar);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}

		if (ar.IsLoading())
			SetUniform(Material::MATERIAL_ID, Uniform1ui::Create({ GetID() }));
	}

	void Material::UpdateBuffer()
	{
		if (!m_Dirty)
			return;

		m_Dirty = false;

		for (auto pair : m_Textures)
			pair.second->UpdateBuffer();
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
		++m_RenderVersion;
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
		// Priority is deterministic (diffuse > specular > normal):
		// iterating the unordered map and taking the first match made
		// the flags nondeterministic for multi-texture materials --
		// a baseColor+normal glTF import could get NORMAL flags and
		// then match no pass shader ("shader not found").
		m_TextureFlags = 0;

		bool hasTexture = true;
		if (m_Textures.find(DIFFUSE_TEXTURE) != m_Textures.end())
			m_TextureFlags = (unsigned int)ShaderTexture::DIFFUSE;
		else if (m_Textures.find(SPECULAR_TEXTURE) != m_Textures.end())
			m_TextureFlags = (unsigned int)ShaderTexture::SPECULAR;
		else if (m_Textures.find(NORMAL_TEXTURE) != m_Textures.end())
			m_TextureFlags = (unsigned int)ShaderTexture::NORMAL;
		else
			hasTexture = false;

		if (!hasTexture)
			m_TextureFlags = (unsigned int)ShaderTexture::COLOR_ONLY;
	}

	unsigned int Material::GetTextureCount() const
	{
		return static_cast<int>(m_Textures.size());
	}

	const Material::TextureMap &Material::GetTextures() const
	{
		return m_Textures;
	}

	void Material::SetUniform(const std::string &name, const std::shared_ptr<UniformBase> &ptr)
	{
		++m_RenderVersion;
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
		return static_cast<int>(m_Uniforms.size());
	}

	void Material::SetShaderForPass(unsigned int index, const std::shared_ptr<Shader> &shader)
	{
		++m_RenderVersion;
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
		++m_RenderVersion;
		m_Opaque = value;
		m_AlphaMode = value ? AlphaMode::OPAQUE : AlphaMode::BLEND;
	}

	AlphaMode Material::GetAlphaMode() const
	{
		return m_AlphaMode;
	}

	void Material::SetAlphaMode(AlphaMode mode)
	{
		++m_RenderVersion;
		m_AlphaMode = mode;
		m_Opaque = (mode != AlphaMode::BLEND);
	}

	float Material::GetAlphaCutoff() const
	{
		return m_AlphaCutoff;
	}

	void Material::SetAlphaCutoff(float value)
	{
		++m_RenderVersion;
		m_AlphaCutoff = value;
	}
}