#ifndef _FURY_PAKFILE_H_
#define _FURY_PAKFILE_H_

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Macros.h"

namespace fury
{
	// UE-style pak archive writer. Layout: data region, index region, fixed
	// footer at EOF. Entries buffer in memory until Finish sorts them by key
	// byte-order and serializes data + index + footer in one pass.
	class FURY_API PakWriter
	{
	public:

		static std::shared_ptr<PakWriter> Create(const std::string& outPath);

		// Compresses into 64 KB independent LZ4 blocks when requested, falling
		// back to raw storage when compression does not shrink the payload.
		bool AddEntry(const std::string& key, const std::vector<unsigned char>& bytes, bool compress);

		bool AddEntryFromFile(const std::string& key, const std::string& filePath, bool compress);

		void SetBootEntry(const std::string& key);

		// Writes data + index + footer. On failure the partial file is removed.
		bool Finish(std::string& err);

		// Removes the partial file when Finish was never called.
		~PakWriter();

	private:

		PakWriter(const std::string& outPath);

		struct Entry
		{
			std::string key;
			std::vector<unsigned char> payload;
			std::vector<uint32_t> blockSizes;
			uint8_t sha1[20];
			uint64_t dataOffset = 0;
			uint64_t uncompressedSize = 0;
			uint32_t codec = 0;
		};

		std::string m_OutPath;
		std::vector<Entry> m_Entries;
		std::string m_BootKey;
		std::ofstream m_Stream;
		bool m_Finished = false;
	};

	// Mounted read-only view of a pak archive. Mount validates magic, version
	// and the SHA-1 of the index region; ReadEntry opens its own stream per
	// call, so it is safe to use from worker threads.
	class FURY_API PakFile
	{
	public:

		static std::shared_ptr<PakFile> Mount(const std::string& path, std::string& err);

		bool HasEntry(const std::string& key) const;

		bool ReadEntry(const std::string& key, std::vector<unsigned char>& out, std::string& err) const;

		const std::string& BootEntry() const;

		// Keys in index (sorted byte-order) order.
		std::vector<std::string> ListKeys() const;

		const std::string& Path() const;

	private:

		struct Entry
		{
			std::string key;
			uint64_t dataOffset = 0;
			uint64_t uncompressedSize = 0;
			uint32_t codec = 0;
			uint32_t blockSize = 0;
			std::vector<uint32_t> blockSizes;
			uint8_t sha1[20];
		};

		PakFile(const std::string& path);

		std::string m_Path;
		std::vector<Entry> m_Entries;
		std::unordered_map<std::string, size_t> m_Lookup;
		std::string m_BootKey;
	};
}

#endif // _FURY_PAKFILE_H_
