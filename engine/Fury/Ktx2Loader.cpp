#include "Ktx2Loader.h"

#include <cstring>

namespace fury
{
	namespace
	{
		const unsigned char kIdentifier[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };

		const uint32_t kHeaderSize = 80; // identifier + 9 u32 + index u32/u64 fields
		const uint32_t kLevelIndexStride = 24; // 3 x u64 per entry

		// BC1_RGB_UNORM, BC1_RGB_SRGB, BC3_UNORM, BC3_SRGB,
		// BC5_UNORM, BC6H_UFLOAT, BC7_UNORM, BC7_SRGB.
		const uint32_t kSupportedVkFormats[] = { 131, 132, 137, 138, 141, 143, 145, 146 };

		uint32_t ReadU32(const unsigned char* p)
		{
			uint32_t v;
			std::memcpy(&v, p, sizeof(v));
			return v; // KTX2 is little-endian; all supported targets are LE
		}

		uint64_t ReadU64(const unsigned char* p)
		{
			uint64_t v;
			std::memcpy(&v, p, sizeof(v));
			return v;
		}

		bool IsSupportedVkFormat(uint32_t vkFormat)
		{
			for (uint32_t f : kSupportedVkFormats)
			{
				if (f == vkFormat)
				{
					return true;
				}
			}
			return false;
		}
	}

	bool Ktx2Loader::Sniff(const unsigned char* data, size_t size)
	{
		return data != nullptr && size >= sizeof(kIdentifier) && std::memcmp(data, kIdentifier, sizeof(kIdentifier)) == 0;
	}

	Ktx2Image Ktx2Loader::Parse(const unsigned char* data, size_t size)
	{
		Ktx2Image image;
		if (!Sniff(data, size))
		{
			image.m_Error = "not a KTX2 file";
			return image;
		}

		if (size < kHeaderSize)
		{
			image.m_Error = "truncated KTX2 header";
			return image;
		}

		const uint32_t vkFormat = ReadU32(data + 12);
		const uint32_t pixelWidth = ReadU32(data + 20);
		const uint32_t pixelHeight = ReadU32(data + 24);
		const uint32_t pixelDepth = ReadU32(data + 28);
		const uint32_t layerCount = ReadU32(data + 32);
		const uint32_t faceCount = ReadU32(data + 36);
		const uint32_t levelCount = ReadU32(data + 40);
		const uint32_t supercompressionScheme = ReadU32(data + 44);

		if (supercompressionScheme != 0)
		{
			image.m_Error = "supercompression scheme " + std::to_string(supercompressionScheme) + " not supported";
			return image;
		}
		if (pixelDepth != 0)
		{
			image.m_Error = "3D textures not supported";
			return image;
		}
		if (layerCount != 0)
		{
			image.m_Error = "array textures not supported";
			return image;
		}
		if (faceCount != 1)
		{
			image.m_Error = "cube maps not supported";
			return image;
		}
		if (levelCount == 0)
		{
			image.m_Error = "levelCount 0 (generate-mips) not supported";
			return image;
		}
		if (!IsSupportedVkFormat(vkFormat))
		{
			image.m_Error = "unsupported vkFormat " + std::to_string(vkFormat);
			return image;
		}

		const uint64_t indexBytes = static_cast<uint64_t>(levelCount) * kLevelIndexStride;
		if (indexBytes > size - kHeaderSize)
		{
			image.m_Error = "truncated level index";
			return image;
		}

		image.m_Levels.resize(levelCount);
		for (uint32_t i = 0; i < levelCount; ++i)
		{
			const unsigned char* entry = data + kHeaderSize + static_cast<size_t>(i) * kLevelIndexStride;
			const uint64_t offset = ReadU64(entry);
			const uint64_t length = ReadU64(entry + 8);
			const uint64_t uncompressedLength = ReadU64(entry + 16);

			if (offset > size || length > size - offset)
			{
				image.m_Error = "level " + std::to_string(i) + " data out of bounds";
				return image;
			}
			if (uncompressedLength != 0 && uncompressedLength != length)
			{
				image.m_Error = "level " + std::to_string(i) + " uncompressedByteLength mismatch";
				return image;
			}

			image.m_Levels[i] = { offset, length };
		}

		image.m_Valid = true;
		image.m_VkFormat = vkFormat;
		image.m_Width = pixelWidth;
		image.m_Height = pixelHeight;
		return image;
	}
}
