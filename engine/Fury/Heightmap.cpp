#include "Fury/Heightmap.h"

#include <fstream>

#include <rapidjson/document.h>

#include "Fury/AssetBackend.h"
#include "Fury/Archive.h"
#include "Fury/EntityUtil.h"
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

	void Heightmap::SetPath(const std::string &path)
	{
		m_FilePath = path;
		m_Name = PathBasename(path);
	}

	void Heightmap::Serialize(Archive& ar)
	{
		Entity::SerializeFields(ar);

		ar << Optional("file_path", m_FilePath);
		ar << Optional("resolution", m_Resolution, 0);
		ar << Optional("worldSizeX", m_WorldSizeX, 0.0f);
		ar << Optional("worldSizeZ", m_WorldSizeZ, 0.0f);
		ar << Optional("heightScale", m_HeightScale, 0.0f);

		if (ar.IsLoading())
		{
			if (!m_FilePath.empty())
				m_Name = PathBasename(m_FilePath);
			LoadHeights();
		}
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

		std::vector<unsigned char> bytes;
		if (!AssetBackend::ReadAssetBytes(Scene::ResolveAsset(m_FilePath), bytes))
		{
			FURYE << "Heightmap: file not found: " << m_FilePath;
			return false;
		}
		const size_t need = static_cast<size_t>(m_Resolution) * m_Resolution * 2;
		if (bytes.size() < need)
		{
			FURYE << "Heightmap: short read on " << m_FilePath;
			return false;
		}

		std::vector<unsigned short> raw(static_cast<size_t>(m_Resolution) * m_Resolution);
		memcpy(raw.data(), bytes.data(), need);

		m_Heights.resize(raw.size());
		const float toCm = m_HeightScale / 65535.0f;
		for (size_t i = 0; i < raw.size(); i++)
			m_Heights[i] = raw[i] * toCm;

		FURYD << "Heightmap: loaded " << m_FilePath << " [" << m_Resolution
			<< "^2, " << m_WorldSizeX << "x" << m_WorldSizeZ << "cm, h " << m_HeightScale << "]";
		return true;
	}
}
