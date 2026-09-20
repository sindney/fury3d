#include "HashUtil.h"

#include <fstream>

#include "SHA1/sha1.h"

namespace fury
{
	std::string HashUtil::Sha1Hex(const void* data, size_t len)
	{
		fury_sha1_ctx ctx;
		fury_sha1_init(&ctx);
		fury_sha1_update(&ctx, data, len);
		uint8_t digest[20];
		char hex[41];
		fury_sha1_final(&ctx, digest);
		fury_sha1_hex(digest, hex);
		return hex;
	}

	std::string HashUtil::Sha1Hex(const std::string& str)
	{
		return Sha1Hex(str.data(), str.size());
	}

	bool HashUtil::Sha1HexOfFile(const std::string& path, std::string& outHex)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open())
			return false;

		fury_sha1_ctx ctx;
		fury_sha1_init(&ctx);
		char chunk[64 * 1024];
		while (stream.good())
		{
			stream.read(chunk, sizeof(chunk));
			std::streamsize got = stream.gcount();
			if (got > 0)
				fury_sha1_update(&ctx, chunk, (size_t)got);
		}
		if (stream.bad())
			return false;

		uint8_t digest[20];
		char hex[41];
		fury_sha1_final(&ctx, digest);
		fury_sha1_hex(digest, hex);
		outHex = hex;
		return true;
	}
}
