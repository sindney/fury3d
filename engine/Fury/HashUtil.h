#ifndef _FURY_HASH_UTIL_H_
#define _FURY_HASH_UTIL_H_

#include <string>

#include "Macros.h"

namespace fury
{
	class FURY_API HashUtil final
	{
	public:

		static std::string Sha1Hex(const void* data, size_t len);

		static std::string Sha1Hex(const std::string& str);

		// Streams the file in chunks. Returns false when the file can't be read.
		static bool Sha1HexOfFile(const std::string& path, std::string& outHex);
	};
}

#endif // _FURY_HASH_UTIL_H_
