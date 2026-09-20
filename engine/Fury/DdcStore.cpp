#include "DdcStore.h"

#include <cstdlib>
#include <filesystem>

#include "Fury/FileUtil.h"
#include "Fury/HashUtil.h"

namespace fury
{
	std::string DdcStore::ResolveRoot(const std::string& explicitPath)
	{
		if (!explicitPath.empty())
			return explicitPath;

		if (const char* env = std::getenv("FURY_DDC"))
		{
			if (env[0] != '\0')
				return env;
		}

		std::string exe = FileUtil::GetExecutablePath();
		if (exe.empty())
			return "DDC";
		return std::filesystem::path(exe).parent_path().string() + "/DDC";
	}

	DdcStore::DdcStore(std::string root)
		: m_Root(std::move(root))
	{
	}

	std::string DdcStore::MakeKey(const std::string& typeTag, const std::string& settingsString,
		const std::string& sourceSha1Hex)
	{
		return HashUtil::Sha1Hex(typeTag + "|" + settingsString + "|" + sourceSha1Hex);
	}

	std::string DdcStore::PathForKey(const std::string& hexKey, const std::string& ext) const
	{
		if (hexKey.size() < 4)
			return m_Root + "/" + hexKey + ext;
		return m_Root + "/" + hexKey.substr(0, 2) + "/" + hexKey.substr(2, 2) + "/" + hexKey + ext;
	}

	bool DdcStore::Has(const std::string& hexKey) const
	{
		std::error_code ec;
		return std::filesystem::exists(PathForKey(hexKey), ec);
	}

	std::string DdcStore::Store(const std::string& hexKey, const std::string& srcFile)
	{
		std::string dest = PathForKey(hexKey);
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(dest).parent_path(), ec);
		if (ec)
			return "";
		std::filesystem::copy_file(srcFile, dest, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
			return "";
		return dest;
	}

	std::string DdcStore::Lookup(const std::string& hexKey) const
	{
		std::string path = PathForKey(hexKey);
		std::error_code ec;
		return std::filesystem::exists(path, ec) ? path : "";
	}
}
