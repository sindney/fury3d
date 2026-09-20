#ifndef _FURY_ASSET_BACKEND_H_
#define _FURY_ASSET_BACKEND_H_

#include <string>
#include <vector>
#include <memory>

#include "Macros.h"

namespace fury
{
	class PakFile;

	// Byte-source facade for all asset reads. With no pak mounted every
	// call resolves to the filesystem exactly as before; with a pak
	// mounted, paths that canonicalize (FileUtil::ToCanonicalAssetKey)
	// to a pak index key are served from the pak instead. Callers never
	// know which source served a read.
	class FURY_API AssetBackend final
	{
	public:

		// resolvedPath: Scene::ResolveAsset / FileUtil::GetAbsPath output.
		static bool ReadAssetBytes(const std::string& resolvedPath, std::vector<unsigned char>& out);

		static bool AssetExists(const std::string& resolvedPath);

		// Mounts a pak. Multiple mounts: later mounts shadow earlier ones.
		// Returns false with a message on bad footer/index/hash.
		static bool MountPak(const std::string& pakPath, std::string& error);

		static void UnmountAll();

		static bool HasMountedPak();

		// Boot entry key of the most recently mounted pak ("" when none).
		static std::string MountedBootEntry();

		// "pak(<name>)" or "disk" for the last read of resolvedPath - logging.
		static std::string DescribeSource(const std::string& resolvedPath);

	private:

		static std::vector<std::shared_ptr<PakFile>> m_Mounts;
	};
}

#endif // _FURY_ASSET_BACKEND_H_
