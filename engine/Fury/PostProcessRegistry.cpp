#include "Fury/PostProcessRegistry.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

#include "Fury/FileUtil.h"
#include "Fury/AssetBackend.h"
#include "Fury/Log.h"
#include "Fury/Scene.h"
#include "Fury/Serializable.h"

#include <rapidjson/document.h>

namespace fury
{
	std::unordered_map<std::string, PostProcessEffect::Ptr> &PostProcessRegistry::Map()
	{
		// Static-local: constructed on first call, lives for the
		// process lifetime. Same pattern as Pipeline::Active.
		static std::unordered_map<std::string, PostProcessEffect::Ptr> registry;
		return registry;
	}

	void PostProcessRegistry::Register(const PostProcessEffect::Ptr &effect)
	{
		if (!effect) return;
		auto &map = Map();
		map[effect->GetName()] = effect;
	}

	PostProcessEffect::Ptr PostProcessRegistry::Get(const std::string &name)
	{
		auto &map = Map();
		auto it = map.find(name);
		if (it == map.end()) return nullptr;
		return it->second;
	}

	std::vector<PostProcessEffect::Ptr> PostProcessRegistry::GetAll()
	{
		std::vector<PostProcessEffect::Ptr> out;
		auto &map = Map();
		out.reserve(map.size());
		for (const auto &kv : map)
			out.push_back(kv.second);
		return out;
	}

	std::vector<PostProcessEffect::Ptr> PostProcessRegistry::GetSortedAll()
	{
		auto out = GetAll();
		std::sort(out.begin(), out.end(), [](const PostProcessEffect::Ptr &a, const PostProcessEffect::Ptr &b)
		{
			if (a->GetStage() != b->GetStage())
				return a->GetStage() < b->GetStage();
			if (a->GetOrder() != b->GetOrder())
				return a->GetOrder() < b->GetOrder();
			return a->GetName() < b->GetName();
		});
		return out;
	}

	void PostProcessRegistry::Clear()
	{
		Map().clear();
	}

	unsigned int PostProcessRegistry::LoadFromDirectory(const std::string &dir)
	{
		namespace fs = std::filesystem;

		// Resolve against the engine's CWD (FileUtil's abs path) so
		// callers can pass either an absolute path or a project-
		// relative path like "Resource/PostProcess". We deliberately
		// DON'T prepend the active scene's working dir -- the scene
		// working dir is already scene-relative ("Resource/Scene/"
		// for example) and would double-prepend on top of a caller-
		// supplied "Resource/PostProcess". FileUtil::GetAbsPath
		// returns absolute paths unchanged and joins relative ones
		// against CWD.
		const std::string absDir = FileUtil::GetAbsPath(dir);

		std::vector<std::string> paths;
		std::string packedPrefix = FileUtil::ToCanonicalAssetKey(absDir);
		if (packedPrefix.empty() && !fs::path(dir).is_absolute())
			packedPrefix = dir;
		std::replace(packedPrefix.begin(), packedPrefix.end(), '\\', '/');
		if (!packedPrefix.empty() && packedPrefix.back() != '/')
			packedPrefix += '/';
		std::unordered_set<std::string> pathKeys;
		for (const auto &key : AssetBackend::ListPackedKeys(packedPrefix))
		{
			const std::string rest = key.substr(packedPrefix.size());
			if (!rest.empty() && rest.find('/') == std::string::npos
				&& fs::path(rest).extension() == ".json")
			{
				paths.push_back(key);
				pathKeys.insert(key);
			}
		}
		{
			std::error_code ec;
			for (const auto &entry : fs::directory_iterator(absDir, ec))
			{
				if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
				std::string key = FileUtil::ToCanonicalAssetKey(entry.path().string());
				if (key.empty()) key = entry.path().string();
				if (pathKeys.insert(key).second)
					paths.push_back(entry.path().string());
			}
		}
		std::sort(paths.begin(), paths.end());
		if (paths.empty())
		{
			FURYW << "PostProcessRegistry: no effects found in '" << absDir << "'";
			return 0;
		}

		unsigned int loaded = 0;
		for (const auto &path : paths)
		{
			std::string text;
			if (!FileUtil::LoadString(path, text))
			{
				FURYW << "PostProcessRegistry: failed to open " << path;
				continue;
			}

			rapidjson::Document dom;
			dom.Parse(text.c_str());
			if (dom.HasParseError())
			{
				FURYE << "PostProcessRegistry: parse error in " << path << ": " << dom.GetParseError();
				continue;
			}
			if (!dom.IsObject())
			{
				FURYW << "PostProcessRegistry: " << path << " is not an object; skipping";
				continue;
			}

			auto effect = PostProcessEffect::Create("");
			if (!effect->Load(&dom))
			{
				FURYW << "PostProcessRegistry: failed to load effect from " << path;
				continue;
			}

			Register(effect);
			++loaded;
			FURYD << "PostProcessRegistry: registered '" << effect->GetName()
				<< "' from " << fs::path(path).filename().string();
		}
		return loaded;
	}
}