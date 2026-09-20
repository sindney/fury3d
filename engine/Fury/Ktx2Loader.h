#ifndef _FURY_KTX2_LOADER_H_
#define _FURY_KTX2_LOADER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "Macros.h"

namespace fury
{
	// Parsed view of an unsupercompressed KTX2 file holding a BCn payload.
	// Levels point into the caller's buffer; nothing is copied.
	class FURY_API Ktx2Image
	{
	public:

		struct Level
		{
			uint64_t offset; // into the source buffer
			uint64_t length;
		};

		bool m_Valid = false;
		std::string m_Error;
		uint32_t m_VkFormat = 0; // raw Vulkan format number; callers map it
		uint32_t m_Width = 0, m_Height = 0;
		std::vector<Level> m_Levels; // level 0 (largest mip) first
	};

	class FURY_API Ktx2Loader final
	{
	public:

		static bool Sniff(const unsigned char* data, size_t size);

		static Ktx2Image Parse(const unsigned char* data, size_t size);
	};
}

#endif // _FURY_KTX2_LOADER_H_
