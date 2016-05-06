#include <algorithm>

#include "Camera.h"
#include "Log.h"
#include "EnumUtil.h"
#include "EntityUtil.h"
#include "FileUtil.h"
#include "Material.h"
#include "Pipeline.h"
#include "Pass.h"
#include "SceneNode.h"
#include "Shader.h"
#include "Texture.h"

namespace fury
{
	Pipeline::~Pipeline()
	{
		FURYD << "Pipeline " << m_Name << " destoried!";
	}

	bool Pipeline::Load(const void* wrapper)
	{
		EntityUtil::Ptr entityMgr = EntityUtil::Instance();
		std::string str;

		if (!LoadArray(wrapper, "textures", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				FURYE << "Texture param 'name' not found!";
				return false;
			}

			auto texture = Texture::Create(str);
			if (!texture->Load(node))
				return false;

			m_TextureMap.emplace(texture->GetName(), texture);
			entityMgr->Add(texture);

			return true;
		}))
		{
			FURYE << "Error reading texture array!";
			return false;
		}

		if (!LoadArray(wrapper, "shaders", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				FURYE << "Shader param 'name' not found!";
				return false;
			}

			auto shader = Shader::Create(str, ShaderType::OTHER);
			if (!shader->Load(node))
				return false;

			m_ShaderMap.emplace(shader->GetName(), shader);
			entityMgr->Add(shader);

			return true;
		}))
		{
			FURYE << "Error reading shader array!";
			return false;
		}

		if (!LoadArray(wrapper, "passes", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				FURYE << "Pass param 'name' not found!";
				return false;
			}

			auto pass = Pass::Create(str);
			if (!pass->Load(node))
				return false;

			m_PassMap.emplace(str, pass);

			return true;
		}))
		{
			FURYE << "Error reading pass array!";
			return false;
		}

		return true;
	}

	bool Pipeline::Save(void* wrapper)
	{
		std::vector<std::string> strs;

		StartObject(wrapper);

		std::vector<Texture::Ptr> textures;
		textures.reserve(m_TextureMap.size());
		for (auto pair : m_TextureMap)
			textures.push_back(pair.second);

		SaveKey(wrapper, "textures");
		SaveArray(wrapper, m_TextureMap.size(), [&](unsigned int index)
		{
			textures[index]->Save(wrapper);
		});
		textures.clear();

		std::vector<Shader::Ptr> shaders;
		shaders.reserve(m_ShaderMap.size());
		for (auto pair : m_ShaderMap)
			shaders.push_back(pair.second);

		SaveKey(wrapper, "shaders");
		SaveArray(wrapper, m_ShaderMap.size(), [&](unsigned int index)
		{
			shaders[index]->Save(wrapper);
		});
		shaders.clear();

		std::vector<Pass::Ptr> passes;
		passes.reserve(m_PassMap.size());
		for (auto pair : m_PassMap)
			passes.push_back(pair.second);

		SaveKey(wrapper, "passes");
		SaveArray(wrapper, m_PassMap.size(), [&](unsigned int index) 
		{
			passes[index]->Save(wrapper);
		});

		EndObject(wrapper);

		return true;
	}

	void Pipeline::SetShadowType(ShadowType type)
	{
		m_ShadowType = type;
	}

	ShadowType Pipeline::GetShadowType()
	{
		return m_ShadowType;
	}

	void Pipeline::SortPassByIndex()
	{
		using DataPair = std::pair<unsigned int, std::string>;

		std::vector<DataPair> wrapper;
		wrapper.reserve(m_PassMap.size());

		for (auto pair : m_PassMap)
			wrapper.push_back(std::make_pair(pair.second->GetRenderIndex(), pair.first));

		std::sort(wrapper.begin(), wrapper.end(), [](const DataPair &a, const DataPair &b)
		{
			return a.first < b.first;
		});

		m_SortedPasses.erase(m_SortedPasses.begin(), m_SortedPasses.end());
		m_SortedPasses.reserve(wrapper.size());
		for (auto pair : wrapper)
			m_SortedPasses.push_back(pair.second);
	}

	void Pipeline::SetDebugParams(bool drawOpaqueBounds, bool drawLightBounds)
	{
		m_DrawOpaqueBounds = drawOpaqueBounds;
		m_DrawLightBounds = drawLightBounds;
	}

	std::shared_ptr<Pass> Pipeline::GetPassByName(const std::string &name)
	{
		auto it = m_PassMap.find(name);
		if (it != m_PassMap.end())
			return it->second;

		return nullptr;
	}

	std::shared_ptr<Texture> Pipeline::GetTextureByName(const std::string &name)
	{
		auto it = m_TextureMap.find(name);
		if (it != m_TextureMap.end())
			return it->second;

		return nullptr;
	}

	std::shared_ptr<Shader> Pipeline::GetShaderByName(const std::string &name)
	{
		auto it = m_ShaderMap.find(name);
		if (it != m_ShaderMap.end())
			return it->second;

		return nullptr;
	}
}