#include "Fury/Heightmap.h"

#include <fstream>

#include <rapidjson/document.h>

#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Scene.h"

namespace fury
{
	Heightmap::Ptr Heightmap::Create(const std::string &name)
	{
		return std::make_shared<Heightmap>(name);
	}

	Heightmap::Heightmap(const std::string &name)
		: Entity(name)
	{
		m_TypeIndex = typeid(Heightmap);
	}

	Heightmap::~Heightmap()
	{
	}

	bool Heightmap::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Heightmap: json node is not an object!";
			return false;
		}
		if (!Entity::Load(wrapper, false))
			return false;

		LoadMemberValue(wrapper, "file_path", m_FilePath);
		LoadMemberValue(wrapper, "resolution", m_Resolution);
		LoadMemberValue(wrapper, "worldSizeX", m_WorldSizeX);
		LoadMemberValue(wrapper, "worldSizeZ", m_WorldSizeZ);
		LoadMemberValue(wrapper, "heightScale", m_HeightScale);

		LoadHeights();
		return true;
	}

	void Heightmap::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "file_path");   SaveValue(wrapper, m_FilePath);
		SaveKey(wrapper, "resolution");  SaveValue(wrapper, m_Resolution);
		SaveKey(wrapper, "worldSizeX");  SaveValue(wrapper, m_WorldSizeX);
		SaveKey(wrapper, "worldSizeZ");  SaveValue(wrapper, m_WorldSizeZ);
		SaveKey(wrapper, "heightScale"); SaveValue(wrapper, m_HeightScale);

		if (object)
			EndObject(wrapper);
	}

	bool Heightmap::LoadHeights()
	{
		if (m_FilePath.empty())
			return false;
		if (HasHeights())
			return true;

		// sidecar: swap .r16 for .json
		std::string jsonPath = m_FilePath;
		auto dot = jsonPath.find_last_of('.');
		if (dot != std::string::npos)
			jsonPath = jsonPath.substr(0, dot);
		jsonPath += ".json";

		std::string jsonText;
		if (!FileUtil::LoadString(Scene::ResolveAsset(jsonPath), jsonText))
		{
			FURYE << "Heightmap: sidecar not found: " << jsonPath;
			return false;
		}

		rapidjson::Document doc;
		doc.Parse(jsonText.c_str());
		if (doc.HasParseError() || !doc.IsObject() ||
			!doc.HasMember("resolution") || !doc.HasMember("worldSizeX") ||
			!doc.HasMember("worldSizeZ") || !doc.HasMember("heightScale"))
		{
			FURYE << "Heightmap: malformed sidecar: " << jsonPath;
			return false;
		}

		m_Resolution = doc["resolution"].GetInt();
		m_WorldSizeX = doc["worldSizeX"].GetFloat();
		m_WorldSizeZ = doc["worldSizeZ"].GetFloat();
		m_HeightScale = doc["heightScale"].GetFloat();

		if (m_Resolution < 2 || (m_Resolution & (m_Resolution - 2)) != 1)
		{
			FURYW << "Heightmap: resolution " << m_Resolution << " is not 2^k+1, loading anyway";
		}

		std::ifstream stream(Scene::ResolveAsset(m_FilePath), std::ios::binary);
		if (!stream.good())
		{
			FURYE << "Heightmap: file not found: " << m_FilePath;
			return false;
		}
		std::vector<unsigned short> raw(static_cast<size_t>(m_Resolution) * m_Resolution);
		stream.read(reinterpret_cast<char*>(raw.data()), raw.size() * 2);
		if (!stream)
		{
			FURYE << "Heightmap: short read on " << m_FilePath;
			return false;
		}

		m_Heights.resize(raw.size());
		const float toCm = m_HeightScale / 65535.0f;
		for (size_t i = 0; i < raw.size(); i++)
			m_Heights[i] = raw[i] * toCm;

		FURYD << "Heightmap: loaded " << m_FilePath << " [" << m_Resolution
			<< "^2, " << m_WorldSizeX << "x" << m_WorldSizeZ << "cm, h " << m_HeightScale << "]";
		return true;
	}
}
