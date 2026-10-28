#ifndef _FURY_VULKAN_ENUM_MAP_H_
#define _FURY_VULKAN_ENUM_MAP_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_VULKAN

#include <volk.h>

#include "Fury/EnumUtil.h"
#include "Fury/RHI/RHITypes.h"

namespace fury
{
	// Canonical RHI enums -> Vk constants. Creation sites must check for
	// VK_FORMAT_UNDEFINED on format mappings (means unsupported).
	namespace VulkanEnumMap
	{
		// REPLACE reports blending disabled; the src/dst/op entries are only
		// meaningful when BlendEnabled() is true.
		FURY_API VkBlendFactor BlendModeSrc(BlendMode mode);

		FURY_API VkBlendFactor BlendModeDest(BlendMode mode);

		FURY_API VkBlendOp BlendModeOp(BlendMode mode);

		FURY_API bool BlendEnabled(BlendMode mode);

		FURY_API VkCompareOp Compare(CompareMode mode);

		FURY_API VkCullModeFlags Cull(CullMode mode);

		FURY_API VkPrimitiveTopology Primitive(RHIPrimitiveMode mode);

		FURY_API VkPolygonMode Fill(RHIFillMode mode);

		FURY_API VkFormat VertexFormat(RHIVertexFormat format);

		FURY_API VkImageType ImageType(TextureType type);

		FURY_API VkImageViewType ImageViewType(TextureType type);

		// UNKNOW / unsupported -> VK_FORMAT_UNDEFINED (creation must fail).
		FURY_API VkFormat PixelFormat(TextureFormat format);

		FURY_API TextureFormat ExpandTo4Channels(TextureFormat format);

		// True for formats with a depth aspect (with or without stencil).
		FURY_API bool IsDepthFormat(TextureFormat format);

		// True for formats with a stencil aspect.
		FURY_API bool IsStencilFormat(TextureFormat format);

		// COLOR / DEPTH / DEPTH|STENCIL aspect mask for the format.
		FURY_API VkImageAspectFlags AspectMask(TextureFormat format);

		// Bytes per pixel (uncompressed) or per block (BC) for upload sizing.
		FURY_API unsigned int BytesPerBlock(TextureFormat format);

		// BC formats upload 4x4 blocks; returns the block edge (1 or 4).
		FURY_API unsigned int BlockEdge(TextureFormat format);

		FURY_API VkFilter SamplerFilter(FilterMode mode);

		FURY_API VkSamplerMipmapMode SamplerMipFilter(FilterMode mode);

		FURY_API VkSamplerAddressMode SamplerWrap(WrapMode mode);

		FURY_API VkBufferUsageFlags BufferUsage(RHIBufferType type);
	}
}

#endif // FURY_RHI_VULKAN

#endif // _FURY_VULKAN_ENUM_MAP_H_
