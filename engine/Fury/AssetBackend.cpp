#include "AssetBackend.h"

#include <fstream>
#include <mutex>

#include "Fury/FileUtil.h"
#include "Fury/AssetLoader.h"
#include "Fury/Log.h"
#include "Fury/PakFile.h"

namespace fury
{
	std::vector<std::shared_ptr<PakFile>> AssetBackend::m_Mounts;
	static std::mutex s_MountMutex;

	bool AssetBackend::ReadAssetBytes(const std::string& resolvedPath, std::vector<unsigned char>& out)
	{
		// Warm bytes from a prefetch win over hitting the source again.
		if (AssetLoader::Get().IsUp() && AssetLoader::Get().TakePrefetched(resolvedPath, out))
			return true;

		std::vector<std::shared_ptr<PakFile>> mounts;
		{
			std::lock_guard<std::mutex> lock(s_MountMutex);
			mounts = m_Mounts;
		}

		if (!mounts.empty())
		{
			std::string key = FileUtil::ToCanonicalAssetKey(resolvedPath);
			if (!key.empty())
			{
				// Later mounts shadow earlier ones.
				for (auto it = mounts.rbegin(); it != mounts.rend(); ++it)
				{
					if (!(*it)->HasEntry(key))
						continue;
					std::string err;
					if ((*it)->ReadEntry(key, out, err))
						return true;
					FURYE << "Pak read failed for " << key << " from " << (*it)->Path() << ": " << err;
					return false;
				}
			}
		}

		std::ifstream stream(resolvedPath, std::ios::binary);
		if (!stream)
			return false;
		stream.seekg(0, std::ios::end);
		size_t size = (size_t)stream.tellg();
		stream.seekg(0, std::ios::beg);
		out.resize(size);
		if (size > 0)
			stream.read((char*)out.data(), size);
		return stream.good() || stream.eof();
	}

	bool AssetBackend::AssetExists(const std::string& resolvedPath)
	{
		std::vector<std::shared_ptr<PakFile>> mounts;
		{
			std::lock_guard<std::mutex> lock(s_MountMutex);
			mounts = m_Mounts;
		}

		if (!mounts.empty())
		{
			std::string key = FileUtil::ToCanonicalAssetKey(resolvedPath);
			if (!key.empty())
			{
				for (auto it = mounts.rbegin(); it != mounts.rend(); ++it)
				{
					if ((*it)->HasEntry(key))
						return true;
				}
			}
		}

		std::ifstream stream(resolvedPath);
		return stream.good();
	}

	bool AssetBackend::MountPak(const std::string& pakPath, std::string& error)
	{
		auto pak = PakFile::Mount(pakPath, error);
		if (pak == nullptr)
			return false;

		// No FURY* logging here: main() mounts before Engine::Initialize
		// brings the Log singleton up. Callers report.
		std::lock_guard<std::mutex> lock(s_MountMutex);
		m_Mounts.push_back(pak);
		return true;
	}

	void AssetBackend::UnmountAll()
	{
		std::lock_guard<std::mutex> lock(s_MountMutex);
		m_Mounts.clear();
	}

	bool AssetBackend::HasMountedPak()
	{
		std::lock_guard<std::mutex> lock(s_MountMutex);
		return !m_Mounts.empty();
	}

	std::string AssetBackend::MountedBootEntry()
	{
		std::lock_guard<std::mutex> lock(s_MountMutex);
		if (m_Mounts.empty())
			return "";
		return m_Mounts.back()->BootEntry();
	}

	std::string AssetBackend::DescribeSource(const std::string& resolvedPath)
	{
		std::vector<std::shared_ptr<PakFile>> mounts;
		{
			std::lock_guard<std::mutex> lock(s_MountMutex);
			mounts = m_Mounts;
		}

		if (!mounts.empty())
		{
			std::string key = FileUtil::ToCanonicalAssetKey(resolvedPath);
			if (!key.empty())
			{
				for (auto it = mounts.rbegin(); it != mounts.rend(); ++it)
				{
					if ((*it)->HasEntry(key))
						return "pak(" + (*it)->Path() + ")";
				}
			}
		}
		return "disk";
	}
}
