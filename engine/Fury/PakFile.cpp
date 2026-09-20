#include "Fury/PakFile.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "LZ4/lz4.h"
#include "SHA1/sha1.h"

namespace
{
	const uint32_t kPakVersion = 1;
	const uint32_t kCodecNone = 0;
	const uint32_t kCodecLz4 = 1;
	const uint32_t kLz4BlockSize = 65536;
	const size_t kSha1Size = 20;
	// magic | version | indexOffset | indexSize | index SHA-1 | reserved.
	const size_t kFooterSize = 8 + 4 + 8 + 8 + kSha1Size + 4;
	const char kMagic[8] = { 'F', 'U', 'R', 'Y', 'P', 'A', 'K', '1' };

	bool KeyLess(const std::string& a, const std::string& b)
	{
		return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
			[](char x, char y) { return (unsigned char)x < (unsigned char)y; });
	}

	bool NormalizeKey(const std::string& in, std::string& out)
	{
		out.clear();
		out.reserve(in.size());
		for (char c : in)
		{
			if (c == '\0')
				return false;
			out.push_back(c == '\\' ? '/' : c);
		}
		return !out.empty();
	}

	void AppendU32(std::vector<unsigned char>& buf, uint32_t v)
	{
		for (int i = 0; i < 4; i++)
			buf.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
	}

	void AppendU64(std::vector<unsigned char>& buf, uint64_t v)
	{
		for (int i = 0; i < 8; i++)
			buf.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
	}

	bool ReadU32(const unsigned char*& cursor, const unsigned char* end, uint32_t& out)
	{
		if ((size_t)(end - cursor) < 4)
			return false;
		out = 0;
		for (int i = 0; i < 4; i++)
			out |= (uint32_t)cursor[i] << (8 * i);
		cursor += 4;
		return true;
	}

	bool ReadU64(const unsigned char*& cursor, const unsigned char* end, uint64_t& out)
	{
		if ((size_t)(end - cursor) < 8)
			return false;
		out = 0;
		for (int i = 0; i < 8; i++)
			out |= (uint64_t)cursor[i] << (8 * i);
		cursor += 8;
		return true;
	}

	void Sha1Of(const void* data, size_t len, uint8_t out[kSha1Size])
	{
		fury_sha1_ctx ctx;
		fury_sha1_init(&ctx);
		if (len > 0)
			fury_sha1_update(&ctx, data, len);
		fury_sha1_final(&ctx, out);
	}

	void WriteBytes(std::ofstream& stream, const void* data, size_t len)
	{
		if (len > 0)
			stream.write((const char*)data, (std::streamsize)len);
	}

	void RemoveQuietly(const std::string& path)
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
}

namespace fury
{
	PakWriter::PakWriter(const std::string& outPath)
		: m_OutPath(outPath)
	{
		m_Stream.open(outPath, std::ios::binary | std::ios::trunc);
	}

	PakWriter::~PakWriter()
	{
		if (!m_Finished)
		{
			m_Stream.close();
			RemoveQuietly(m_OutPath);
		}
	}

	std::shared_ptr<PakWriter> PakWriter::Create(const std::string& outPath)
	{
		if (outPath.empty())
			return nullptr;
		std::shared_ptr<PakWriter> writer(new PakWriter(outPath));
		if (!writer->m_Stream.is_open())
			return nullptr;
		return writer;
	}

	bool PakWriter::AddEntry(const std::string& key, const std::vector<unsigned char>& bytes, bool compress)
	{
		if (m_Finished)
			return false;

		Entry entry;
		if (!NormalizeKey(key, entry.key))
			return false;
		for (const Entry& e : m_Entries)
		{
			if (e.key == entry.key)
				return false;
		}

		entry.uncompressedSize = (uint64_t)bytes.size();
		Sha1Of(bytes.data(), bytes.size(), entry.sha1);

		if (compress && !bytes.empty())
		{
			std::vector<unsigned char> compressed;
			std::vector<uint32_t> blockSizes;
			size_t offset = 0;
			while (offset < bytes.size())
			{
				int blockLen = (int)std::min<size_t>((size_t)kLz4BlockSize, bytes.size() - offset);
				int bound = LZ4_compressBound(blockLen);
				std::vector<char> block((size_t)bound);
				int got = LZ4_compress_default((const char*)bytes.data() + offset, block.data(), blockLen, bound);
				if (got <= 0)
					return false;
				blockSizes.push_back((uint32_t)got);
				compressed.insert(compressed.end(), block.begin(), block.begin() + got);
				offset += (size_t)blockLen;
			}
			if (compressed.size() < bytes.size())
			{
				entry.codec = kCodecLz4;
				entry.payload = std::move(compressed);
				entry.blockSizes = std::move(blockSizes);
			}
		}
		if (entry.codec == kCodecNone)
			entry.payload = bytes;

		m_Entries.push_back(std::move(entry));
		return true;
	}

	bool PakWriter::AddEntryFromFile(const std::string& key, const std::string& filePath, bool compress)
	{
		std::ifstream stream(filePath, std::ios::binary);
		if (!stream.is_open())
			return false;
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		if (stream.bad())
			return false;
		return AddEntry(key, bytes, compress);
	}

	void PakWriter::SetBootEntry(const std::string& key)
	{
		std::string normalized;
		if (NormalizeKey(key, normalized))
			m_BootKey = normalized;
	}

	bool PakWriter::Finish(std::string& err)
	{
		if (m_Finished)
		{
			err = "Finish already called";
			return false;
		}

		std::sort(m_Entries.begin(), m_Entries.end(), [](const Entry& a, const Entry& b) { return KeyLess(a.key, b.key); });
		for (size_t i = 1; i < m_Entries.size(); i++)
		{
			if (m_Entries[i - 1].key == m_Entries[i].key)
			{
				err = "duplicate key '" + m_Entries[i].key + "'";
				return false;
			}
		}
		if (!m_BootKey.empty())
		{
			bool found = false;
			for (const Entry& e : m_Entries)
			{
				if (e.key == m_BootKey)
					found = true;
			}
			if (!found)
			{
				err = "boot entry '" + m_BootKey + "' was not added";
				return false;
			}
		}

		uint64_t offset = 0;
		for (Entry& e : m_Entries)
		{
			e.dataOffset = offset;
			WriteBytes(m_Stream, e.payload.data(), e.payload.size());
			offset += (uint64_t)e.payload.size();
		}
		const uint64_t indexOffset = offset;

		std::vector<unsigned char> index;
		AppendU32(index, (uint32_t)m_Entries.size());
		for (const Entry& e : m_Entries)
		{
			AppendU32(index, (uint32_t)e.key.size());
			index.insert(index.end(), e.key.begin(), e.key.end());
			AppendU64(index, e.dataOffset);
			AppendU64(index, e.uncompressedSize);
			AppendU32(index, e.codec);
			AppendU32(index, e.codec == kCodecLz4 ? kLz4BlockSize : 0);
			AppendU32(index, (uint32_t)e.blockSizes.size());
			for (uint32_t s : e.blockSizes)
				AppendU32(index, s);
			index.insert(index.end(), e.sha1, e.sha1 + kSha1Size);
		}
		AppendU32(index, (uint32_t)m_BootKey.size());
		index.insert(index.end(), m_BootKey.begin(), m_BootKey.end());

		uint8_t indexSha1[kSha1Size];
		Sha1Of(index.data(), index.size(), indexSha1);

		std::vector<unsigned char> footer;
		footer.insert(footer.end(), kMagic, kMagic + 8);
		AppendU32(footer, kPakVersion);
		AppendU64(footer, indexOffset);
		AppendU64(footer, (uint64_t)index.size());
		footer.insert(footer.end(), indexSha1, indexSha1 + kSha1Size);
		AppendU32(footer, 0);

		WriteBytes(m_Stream, index.data(), index.size());
		WriteBytes(m_Stream, footer.data(), footer.size());
		m_Stream.close();
		if (!m_Stream)
		{
			RemoveQuietly(m_OutPath);
			err = "failed writing '" + m_OutPath + "'";
			return false;
		}
		m_Finished = true;
		return true;
	}

	PakFile::PakFile(const std::string& path)
		: m_Path(path)
	{
	}

	std::shared_ptr<PakFile> PakFile::Mount(const std::string& path, std::string& err)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open())
		{
			err = path + ": cannot open pak";
			return nullptr;
		}
		stream.seekg(0, std::ios::end);
		std::streamoff fileSizeOff = stream.tellg();
		if (fileSizeOff < (std::streamoff)kFooterSize)
		{
			err = path + ": too small for a FURYPAK1 footer";
			return nullptr;
		}
		const uint64_t fileSize = (uint64_t)fileSizeOff;
		stream.seekg((std::streamoff)(fileSize - kFooterSize));
		unsigned char footer[kFooterSize];
		stream.read((char*)footer, (std::streamsize)kFooterSize);
		if (!stream)
		{
			err = path + ": failed reading footer";
			return nullptr;
		}

		const unsigned char* cursor = footer;
		if (std::memcmp(cursor, kMagic, 8) != 0)
		{
			err = path + ": bad magic, not a FURYPAK1 archive";
			return nullptr;
		}
		cursor += 8;
		uint32_t version;
		uint64_t indexOffset;
		uint64_t indexSize;
		uint8_t expectedSha1[kSha1Size];
		uint32_t reserved;
		ReadU32(cursor, footer + kFooterSize, version);
		ReadU64(cursor, footer + kFooterSize, indexOffset);
		ReadU64(cursor, footer + kFooterSize, indexSize);
		std::memcpy(expectedSha1, cursor, kSha1Size);
		cursor += kSha1Size;
		ReadU32(cursor, footer + kFooterSize, reserved);
		(void)reserved;

		if (version != kPakVersion)
		{
			err = path + ": unsupported pak version " + std::to_string(version);
			return nullptr;
		}
		if (indexOffset + indexSize > fileSize - kFooterSize)
		{
			err = path + ": index region lies outside the file";
			return nullptr;
		}

		std::vector<unsigned char> index((size_t)indexSize);
		stream.seekg((std::streamoff)indexOffset);
		if (indexSize > 0)
			stream.read((char*)index.data(), (std::streamsize)indexSize);
		if (!stream)
		{
			err = path + ": failed reading index region";
			return nullptr;
		}

		uint8_t actualSha1[kSha1Size];
		Sha1Of(index.data(), index.size(), actualSha1);
		if (std::memcmp(actualSha1, expectedSha1, kSha1Size) != 0)
		{
			err = path + ": index SHA-1 mismatch (archive corrupted or truncated)";
			return nullptr;
		}

		std::shared_ptr<PakFile> pak(new PakFile(path));
		const unsigned char* end = index.data() + index.size();
		cursor = index.data();
		uint32_t entryCount;
		if (!ReadU32(cursor, end, entryCount))
		{
			err = path + ": truncated index (entry count)";
			return nullptr;
		}
		for (uint32_t i = 0; i < entryCount; i++)
		{
			Entry e;
			uint32_t keyLen;
			uint32_t blockCount;
			if (!ReadU32(cursor, end, keyLen) || (size_t)(end - cursor) < (size_t)keyLen)
			{
				err = path + ": truncated index (entry key)";
				return nullptr;
			}
			e.key.assign((const char*)cursor, (size_t)keyLen);
			cursor += keyLen;
			if (!ReadU64(cursor, end, e.dataOffset) ||
				!ReadU64(cursor, end, e.uncompressedSize) ||
				!ReadU32(cursor, end, e.codec) ||
				!ReadU32(cursor, end, e.blockSize) ||
				!ReadU32(cursor, end, blockCount) ||
				(size_t)(end - cursor) < (size_t)blockCount * 4 + kSha1Size)
			{
				err = path + ": truncated index (entry metadata)";
				return nullptr;
			}
			for (uint32_t b = 0; b < blockCount; b++)
			{
				uint32_t s;
				ReadU32(cursor, end, s);
				e.blockSizes.push_back(s);
			}
			std::memcpy(e.sha1, cursor, kSha1Size);
			cursor += kSha1Size;

			if (e.codec > kCodecLz4)
			{
				err = path + ": entry '" + e.key + "' has unknown codec " + std::to_string(e.codec);
				return nullptr;
			}
			if (e.codec == kCodecNone)
			{
				if (blockCount != 0)
				{
					err = path + ": entry '" + e.key + "' is raw but has a block table";
					return nullptr;
				}
			}
			else
			{
				uint64_t expectedBlocks = e.blockSize > 0
					? (e.uncompressedSize + e.blockSize - 1) / e.blockSize
					: 0;
				if (e.blockSize == 0 || (uint64_t)blockCount != expectedBlocks)
				{
					err = path + ": entry '" + e.key + "' has an inconsistent block table";
					return nullptr;
				}
			}
			uint64_t storedSize = e.uncompressedSize;
			if (e.codec == kCodecLz4)
			{
				storedSize = 0;
				for (uint32_t s : e.blockSizes)
					storedSize += s;
			}
			if (storedSize > indexOffset || e.dataOffset > indexOffset - storedSize)
			{
				err = path + ": entry '" + e.key + "' data lies outside the data region";
				return nullptr;
			}
			if (!pak->m_Lookup.emplace(e.key, pak->m_Entries.size()).second)
			{
				err = path + ": duplicate key '" + e.key + "' in index";
				return nullptr;
			}
			pak->m_Entries.push_back(std::move(e));
		}

		uint32_t bootLen;
		if (!ReadU32(cursor, end, bootLen) || (uint64_t)(end - cursor) != bootLen)
		{
			err = path + ": truncated index (boot key)";
			return nullptr;
		}
		pak->m_BootKey.assign((const char*)cursor, (size_t)bootLen);
		return pak;
	}

	bool PakFile::HasEntry(const std::string& key) const
	{
		std::string normalized;
		if (!NormalizeKey(key, normalized))
			return false;
		return m_Lookup.find(normalized) != m_Lookup.end();
	}

	bool PakFile::ReadEntry(const std::string& key, std::vector<unsigned char>& out, std::string& err) const
	{
		std::string normalized;
		if (!NormalizeKey(key, normalized))
		{
			err = m_Path + ": invalid entry key";
			return false;
		}
		auto it = m_Lookup.find(normalized);
		if (it == m_Lookup.end())
		{
			err = m_Path + ": entry '" + normalized + "' not found";
			return false;
		}
		const Entry& e = m_Entries[it->second];

		std::ifstream stream(m_Path, std::ios::binary);
		if (!stream.is_open())
		{
			err = m_Path + ": cannot reopen pak";
			return false;
		}

		out.resize((size_t)e.uncompressedSize);
		stream.seekg((std::streamoff)e.dataOffset);
		if (e.codec == kCodecNone)
		{
			if (e.uncompressedSize > 0)
				stream.read((char*)out.data(), (std::streamsize)e.uncompressedSize);
		}
		else
		{
			size_t produced = 0;
			for (size_t b = 0; b < e.blockSizes.size(); b++)
			{
				size_t expected = std::min<size_t>((size_t)e.blockSize, (size_t)e.uncompressedSize - produced);
				std::vector<char> compressed(e.blockSizes[b]);
				if (e.blockSizes[b] > 0)
					stream.read(compressed.data(), (std::streamsize)e.blockSizes[b]);
				int got = LZ4_decompress_safe(compressed.data(), (char*)out.data() + produced,
					(int)e.blockSizes[b], (int)expected);
				if (got != (int)expected)
				{
					err = m_Path + ": entry '" + e.key + "' LZ4 block " + std::to_string(b)
						+ " failed to decompress";
					return false;
				}
				produced += (size_t)got;
			}
		}
		if (!stream)
		{
			err = m_Path + ": entry '" + e.key + "' payload is truncated";
			return false;
		}

		uint8_t actualSha1[kSha1Size];
		Sha1Of(out.data(), out.size(), actualSha1);
		if (std::memcmp(actualSha1, e.sha1, kSha1Size) != 0)
		{
			err = m_Path + ": entry '" + e.key + "' payload SHA-1 mismatch";
			return false;
		}
		return true;
	}

	const std::string& PakFile::BootEntry() const
	{
		return m_BootKey;
	}

	std::vector<std::string> PakFile::ListKeys() const
	{
		std::vector<std::string> keys;
		keys.reserve(m_Entries.size());
		for (const Entry& e : m_Entries)
			keys.push_back(e.key);
		return keys;
	}

	const std::string& PakFile::Path() const
	{
		return m_Path;
	}
}
