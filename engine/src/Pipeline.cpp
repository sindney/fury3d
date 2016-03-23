#include "Camera.h"
#include "Debug.h"
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
	bool Pipeline::Load(const void* wrapper)
	{
		EntityUtil::Ptr entityUtil = EntityUtil::Instance();
		EnumUtil::Ptr enumUtil = EnumUtil::Instance();

		std::string str;

		if (!LoadArray(wrapper, "textures", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				LOGE << "Texture param 'name' not found!";
				return false;
			}

			auto texture = Texture::Create(str);
			if (!texture->Load(node))
				return false;

			m_TextureMap.emplace(texture->GetName(), texture);
			entityUtil->AddEntity(texture);

			return true;
		}))
		{
			LOGE << "Error reading texture array!";
			return false;
		}

		if (!LoadArray(wrapper, "shaders", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				LOGE << "Shader param 'name' not found!";
				return false;
			}

			auto shader = Shader::Create(str, ShaderType::OTHER);
			if (!shader->Load(node))
				return false;

			m_ShaderMap.emplace(shader->GetName(), shader);
			entityUtil->AddEntity(shader);

			return true;
		}))
		{
			LOGE << "Error reading shader array!";
			return false;
		}

		if (!LoadArray(wrapper, "passes", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				LOGE << "Pass param 'name' not found!";
				return false;
			}

			auto pass = Pass::Create(str);
			if (!pass->Load(node))
				return false;

			m_PassMap.emplace(str, pass);

			return true;
		}))
		{
			LOGE << "Error reading pass array!";
			return false;
		}

		return true;
	}

	bool Pipeline::Save(void* wrapper)
	{
		EnumUtil::Ptr enumUtil = EnumUtil::Instance();
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
}