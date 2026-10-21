#ifndef _FURY_METAL_ENUM_MAP_H_
#define _FURY_METAL_ENUM_MAP_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_METAL

#include <Metal/Metal.hpp>

#include "Fury/EnumUtil.h"
#include "Fury/RHI/RHITypes.h"

namespace fury
{
	// Canonical RHI enums -> MTL constants. One translation unit (MetalEnumMap.cpp)
	// also carries the metal-cpp private-implementation defines.
	namespace MetalEnumMap
	{
		// REPLACE reports blending disabled; the src/dst/op entries are only
		// meaningful when BlendEnabled() is true.
		FURY_API MTL::BlendFactor BlendModeSrc(BlendMode mode);

		FURY_API MTL::BlendFactor BlendModeDest(BlendMode mode);

		FURY_API MTL::BlendOperation BlendModeOp(BlendMode mode);

		FURY_API bool BlendEnabled(BlendMode mode);

		FURY_API MTL::CompareFunction Compare(CompareMode mode);

		FURY_API MTL::CullMode Cull(CullMode mode);

		FURY_API MTL::PrimitiveType Primitive(RHIPrimitiveMode mode);

		FURY_API MTL::VertexFormat VertexFormat(RHIVertexFormat format);

		// UNKNOW / unsupported -> MTL::PixelFormatInvalid (creation must fail).
		FURY_API MTL::PixelFormat PixelFormat(TextureFormat format);

		// Bytes per pixel (uncompressed) or per block (BC) for upload sizing.
		FURY_API unsigned int BytesPerBlock(TextureFormat format);

		// BC formats upload 4x4 blocks; returns the block edge (1 or 4).
		FURY_API unsigned int BlockEdge(TextureFormat format);

		// Metal has no 3-channel 8/16/32-bit formats; RGB* formats expand to
		// their RGBA* counterparts CPU-side on upload.
		FURY_API TextureFormat ExpandTo4Channels(TextureFormat format);

		// Components per pixel of a float format (0 for non-float formats).
		FURY_API unsigned int ChannelCount(TextureFormat format);

		// Storage size of one float component (2 for half, 4 for float;
		// 0 for non-float formats).
		FURY_API unsigned int FloatComponentBytes(TextureFormat format);

		FURY_API MTL::SamplerMinMagFilter SamplerFilter(FilterMode mode);

		FURY_API MTL::SamplerMipFilter SamplerMipFilter(FilterMode mode);

		FURY_API MTL::SamplerAddressMode SamplerWrap(WrapMode mode);

		FURY_API MTL::TextureType MTLTextureType(TextureType type);
	}
}

#endif // FURY_RHI_METAL

#endif // _FURY_METAL_ENUM_MAP_H_
