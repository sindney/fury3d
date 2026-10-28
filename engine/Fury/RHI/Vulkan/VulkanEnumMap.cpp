#include "Fury/RHI/Vulkan/VulkanEnumMap.h"

#if FURY_RHI_VULKAN

namespace fury
{
	namespace VulkanEnumMap
	{
		VkBlendFactor BlendModeSrc(BlendMode mode)
		{
			switch (mode)
			{
			case BlendMode::REPLACE: return VK_BLEND_FACTOR_ONE;
			case BlendMode::ADD: return VK_BLEND_FACTOR_ONE;
			case BlendMode::MULTIPLY: return VK_BLEND_FACTOR_DST_COLOR;
			case BlendMode::ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
			case BlendMode::ADDALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
			case BlendMode::PREMULALPHA: return VK_BLEND_FACTOR_ONE;
			case BlendMode::INVDESTALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			case BlendMode::SUBTRACT: return VK_BLEND_FACTOR_ONE;
			case BlendMode::SUBTRACTALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
			}
			return VK_BLEND_FACTOR_ONE;
		}

		VkBlendFactor BlendModeDest(BlendMode mode)
		{
			switch (mode)
			{
			case BlendMode::REPLACE: return VK_BLEND_FACTOR_ZERO;
			case BlendMode::ADD: return VK_BLEND_FACTOR_ONE;
			case BlendMode::MULTIPLY: return VK_BLEND_FACTOR_ZERO;
			case BlendMode::ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			case BlendMode::ADDALPHA: return VK_BLEND_FACTOR_ONE;
			case BlendMode::PREMULALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			case BlendMode::INVDESTALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
			case BlendMode::SUBTRACT: return VK_BLEND_FACTOR_ONE;
			case BlendMode::SUBTRACTALPHA: return VK_BLEND_FACTOR_ONE;
			}
			return VK_BLEND_FACTOR_ZERO;
		}

		VkBlendOp BlendModeOp(BlendMode mode)
		{
			switch (mode)
			{
			case BlendMode::SUBTRACT:
			case BlendMode::SUBTRACTALPHA:
				return VK_BLEND_OP_REVERSE_SUBTRACT;
			default:
				return VK_BLEND_OP_ADD;
			}
		}

		bool BlendEnabled(BlendMode mode)
		{
			return mode != BlendMode::REPLACE;
		}

		VkCompareOp Compare(CompareMode mode)
		{
			switch (mode)
			{
			case CompareMode::LESS: return VK_COMPARE_OP_LESS;
			case CompareMode::GREATER: return VK_COMPARE_OP_GREATER;
			case CompareMode::EQUAL: return VK_COMPARE_OP_EQUAL;
			case CompareMode::ALWAYS: return VK_COMPARE_OP_ALWAYS;
			case CompareMode::LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
			case CompareMode::GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case CompareMode::NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
			}
			return VK_COMPARE_OP_LESS;
		}

		VkCullModeFlags Cull(CullMode mode)
		{
			switch (mode)
			{
			case CullMode::FRONT: return VK_CULL_MODE_FRONT_BIT;
			case CullMode::BACK: return VK_CULL_MODE_BACK_BIT;
			case CullMode::NONE: return VK_CULL_MODE_NONE;
			}
			return VK_CULL_MODE_BACK_BIT;
		}

		VkPrimitiveTopology Primitive(RHIPrimitiveMode mode)
		{
			switch (mode)
			{
			case RHIPrimitiveMode::TRIANGLES: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case RHIPrimitiveMode::LINES: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case RHIPrimitiveMode::LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			// No line loop in Vulkan; emulated as a strip with a closing segment.
			case RHIPrimitiveMode::LINE_LOOP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			}
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}

		VkPolygonMode Fill(RHIFillMode mode)
		{
			switch (mode)
			{
			case RHIFillMode::SOLID: return VK_POLYGON_MODE_FILL;
			case RHIFillMode::WIREFRAME: return VK_POLYGON_MODE_LINE;
			}
			return VK_POLYGON_MODE_FILL;
		}

		VkFormat VertexFormat(RHIVertexFormat format)
		{
			switch (format)
			{
			case RHIVertexFormat::FLOAT: return VK_FORMAT_R32_SFLOAT;
			case RHIVertexFormat::FLOAT2: return VK_FORMAT_R32G32_SFLOAT;
			case RHIVertexFormat::FLOAT3: return VK_FORMAT_R32G32B32_SFLOAT;
			case RHIVertexFormat::FLOAT4: return VK_FORMAT_R32G32B32A32_SFLOAT;
			case RHIVertexFormat::INT4: return VK_FORMAT_R32G32B32A32_SINT;
			case RHIVertexFormat::UINT4: return VK_FORMAT_R32G32B32A32_UINT;
			}
			return VK_FORMAT_R32G32B32_SFLOAT;
		}

		VkImageType ImageType(TextureType type)
		{
			switch (type)
			{
			case TextureType::TEXTURE_1D: return VK_IMAGE_TYPE_1D;
			case TextureType::TEXTURE_2D: return VK_IMAGE_TYPE_2D;
			case TextureType::TEXTURE_2D_ARRAY: return VK_IMAGE_TYPE_2D;
			case TextureType::TEXTURE_CUBE_MAP: return VK_IMAGE_TYPE_2D;
			case TextureType::TEXTURE_3D: return VK_IMAGE_TYPE_3D;
			}
			return VK_IMAGE_TYPE_2D;
		}

		VkImageViewType ImageViewType(TextureType type)
		{
			switch (type)
			{
			case TextureType::TEXTURE_1D: return VK_IMAGE_VIEW_TYPE_1D;
			case TextureType::TEXTURE_2D: return VK_IMAGE_VIEW_TYPE_2D;
			case TextureType::TEXTURE_2D_ARRAY: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case TextureType::TEXTURE_CUBE_MAP: return VK_IMAGE_VIEW_TYPE_CUBE;
			case TextureType::TEXTURE_3D: return VK_IMAGE_VIEW_TYPE_3D;
			}
			return VK_IMAGE_VIEW_TYPE_2D;
		}

		VkFormat PixelFormat(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::R8: return VK_FORMAT_R8_UNORM;
			case TextureFormat::R16: return VK_FORMAT_R16_UNORM;
			case TextureFormat::R16F: return VK_FORMAT_R16_SFLOAT;
			case TextureFormat::R32F: return VK_FORMAT_R32_SFLOAT;
			case TextureFormat::R32UI: return VK_FORMAT_R32_UINT;
			case TextureFormat::RG8: return VK_FORMAT_R8G8_UNORM;
			case TextureFormat::RG16: return VK_FORMAT_R16G16_UNORM;
			case TextureFormat::RG16F: return VK_FORMAT_R16G16_SFLOAT;
			case TextureFormat::RG32F: return VK_FORMAT_R32G32_SFLOAT;
			case TextureFormat::RGB8: return VK_FORMAT_R8G8B8A8_UNORM;
			case TextureFormat::RGB16: return VK_FORMAT_R16G16B16A16_UNORM;
			case TextureFormat::RGB16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
			case TextureFormat::RGB32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
			case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
			case TextureFormat::RGBA16: return VK_FORMAT_R16G16B16A16_UNORM;
			case TextureFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
			case TextureFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
			case TextureFormat::SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
			case TextureFormat::SRGB8: return VK_FORMAT_R8G8B8A8_SRGB;
			case TextureFormat::SRGB_ALPHA: return VK_FORMAT_R8G8B8A8_SRGB;
			case TextureFormat::SRGB8_ALPHA8: return VK_FORMAT_R8G8B8A8_SRGB;
			case TextureFormat::DEPTH16: return VK_FORMAT_D16_UNORM;
			case TextureFormat::DEPTH24: return VK_FORMAT_D32_SFLOAT;
			case TextureFormat::DEPTH32F: return VK_FORMAT_D32_SFLOAT;
			case TextureFormat::DEPTH32F_STENCIL8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
			case TextureFormat::DEPTH24_STENCIL8: return VK_FORMAT_D24_UNORM_S8_UINT;
			case TextureFormat::BC1_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			case TextureFormat::BC1_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
			case TextureFormat::BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
			case TextureFormat::BC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
			case TextureFormat::BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
			case TextureFormat::BC6H_UF: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
			case TextureFormat::BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
			case TextureFormat::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
			default: return VK_FORMAT_UNDEFINED;
			}
		}

		TextureFormat ExpandTo4Channels(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::RGB8: return TextureFormat::RGBA8;
			case TextureFormat::RGB16: return TextureFormat::RGBA16;
			case TextureFormat::RGB16F: return TextureFormat::RGBA16F;
			case TextureFormat::RGB32F: return TextureFormat::RGBA32F;
			case TextureFormat::SRGB: return TextureFormat::SRGB_ALPHA;
			case TextureFormat::SRGB8: return TextureFormat::SRGB8_ALPHA8;
			default: return format;
			}
		}

		bool IsDepthFormat(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::DEPTH16:
			case TextureFormat::DEPTH24:
			case TextureFormat::DEPTH32F:
			case TextureFormat::DEPTH32F_STENCIL8:
			case TextureFormat::DEPTH24_STENCIL8:
				return true;
			default:
				return false;
			}
		}

		bool IsStencilFormat(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::DEPTH32F_STENCIL8:
			case TextureFormat::DEPTH24_STENCIL8:
				return true;
			default:
				return false;
			}
		}

		VkImageAspectFlags AspectMask(TextureFormat format)
		{
			if (IsDepthFormat(format))
				return IsStencilFormat(format)
					? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
					: VK_IMAGE_ASPECT_DEPTH_BIT;
			return VK_IMAGE_ASPECT_COLOR_BIT;
		}

		unsigned int BytesPerBlock(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::R8: return 1;
			case TextureFormat::R16: return 2;
			case TextureFormat::R16F: return 2;
			case TextureFormat::R32F: return 4;
			case TextureFormat::R32UI: return 4;
			case TextureFormat::RG8: return 2;
			case TextureFormat::RG16: return 4;
			case TextureFormat::RG16F: return 4;
			case TextureFormat::RG32F: return 8;
			case TextureFormat::RGB8: return 3;
			case TextureFormat::RGB16: return 6;
			case TextureFormat::RGB16F: return 6;
			case TextureFormat::RGB32F: return 12;
			case TextureFormat::RGBA8: return 4;
			case TextureFormat::RGBA16: return 8;
			case TextureFormat::RGBA16F: return 8;
			case TextureFormat::RGBA32F: return 16;
			case TextureFormat::SRGB: return 3;
			case TextureFormat::SRGB8: return 3;
			case TextureFormat::SRGB_ALPHA: return 4;
			case TextureFormat::SRGB8_ALPHA8: return 4;
			case TextureFormat::DEPTH16: return 2;
			// Depth-only 24-bit targets use 32-bit float storage.
			case TextureFormat::DEPTH24: return 4;
			case TextureFormat::DEPTH32F: return 4;
			case TextureFormat::DEPTH32F_STENCIL8: return 8;
			case TextureFormat::DEPTH24_STENCIL8: return 4;
			case TextureFormat::BC1_UNORM: return 8;
			case TextureFormat::BC1_SRGB: return 8;
			case TextureFormat::BC3_UNORM: return 16;
			case TextureFormat::BC3_SRGB: return 16;
			case TextureFormat::BC5_UNORM: return 16;
			case TextureFormat::BC6H_UF: return 16;
			case TextureFormat::BC7_UNORM: return 16;
			case TextureFormat::BC7_SRGB: return 16;
			default: return 0;
			}
		}

		unsigned int BlockEdge(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::BC1_UNORM:
			case TextureFormat::BC1_SRGB:
			case TextureFormat::BC3_UNORM:
			case TextureFormat::BC3_SRGB:
			case TextureFormat::BC5_UNORM:
			case TextureFormat::BC6H_UF:
			case TextureFormat::BC7_UNORM:
			case TextureFormat::BC7_SRGB:
				return 4;
			default:
				return 1;
			}
		}

		VkFilter SamplerFilter(FilterMode mode)
		{
			switch (mode)
			{
			case FilterMode::NEAREST:
			case FilterMode::NEAREST_MIPMAP_NEAREST:
			case FilterMode::NEAREST_MIPMAP_LINEAR:
				return VK_FILTER_NEAREST;
			default:
				return VK_FILTER_LINEAR;
			}
		}

		VkSamplerMipmapMode SamplerMipFilter(FilterMode mode)
		{
			switch (mode)
			{
			case FilterMode::NEAREST_MIPMAP_NEAREST:
			case FilterMode::LINEAR_MIPMAP_NEAREST:
				return VK_SAMPLER_MIPMAP_MODE_NEAREST;
			default:
				return VK_SAMPLER_MIPMAP_MODE_LINEAR;
			}
		}

		VkSamplerAddressMode SamplerWrap(WrapMode mode)
		{
			switch (mode)
			{
			case WrapMode::REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			case WrapMode::MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case WrapMode::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case WrapMode::CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			}
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		}

		VkBufferUsageFlags BufferUsage(RHIBufferType type)
		{
			switch (type)
			{
			case RHIBufferType::VERTEX: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			case RHIBufferType::INDEX: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			case RHIBufferType::UNIFORM: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			case RHIBufferType::STORAGE: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			}
			return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		}
	}
}

#endif // FURY_RHI_VULKAN
