// The one metal-cpp implementation TU: the private-implementation defines
// must precede the FIRST Metal include in this TU (MetalEnumMap.h pulls
// <Metal/Metal.hpp>), or every selector links as an undefined extern.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "Fury/RHI/Metal/MetalEnumMap.h"

#if FURY_RHI_METAL

#include <QuartzCore/QuartzCore.hpp>

namespace fury
{
	namespace MetalEnumMap
	{
		MTL::BlendFactor BlendModeSrc(BlendMode mode)
		{
			switch (mode)
			{
			case BlendMode::REPLACE: return MTL::BlendFactorOne;
			case BlendMode::ADD: return MTL::BlendFactorOne;
			case BlendMode::MULTIPLY: return MTL::BlendFactorDestinationColor;
			case BlendMode::ALPHA: return MTL::BlendFactorSourceAlpha;
			case BlendMode::ADDALPHA: return MTL::BlendFactorSourceAlpha;
			case BlendMode::PREMULALPHA: return MTL::BlendFactorOne;
			case BlendMode::INVDESTALPHA: return MTL::BlendFactorOneMinusDestinationAlpha;
			case BlendMode::SUBTRACT: return MTL::BlendFactorOne;
			case BlendMode::SUBTRACTALPHA: return MTL::BlendFactorSourceAlpha;
			}
			return MTL::BlendFactorOne;
		}

		MTL::BlendFactor BlendModeDest(BlendMode mode)
		{
			switch (mode)
			{
			case BlendMode::REPLACE: return MTL::BlendFactorZero;
			case BlendMode::ADD: return MTL::BlendFactorOne;
			case BlendMode::MULTIPLY: return MTL::BlendFactorZero;
			case BlendMode::ALPHA: return MTL::BlendFactorOneMinusSourceAlpha;
			case BlendMode::ADDALPHA: return MTL::BlendFactorOne;
			case BlendMode::PREMULALPHA: return MTL::BlendFactorOneMinusSourceAlpha;
			case BlendMode::INVDESTALPHA: return MTL::BlendFactorDestinationAlpha;
			case BlendMode::SUBTRACT: return MTL::BlendFactorOne;
			case BlendMode::SUBTRACTALPHA: return MTL::BlendFactorOne;
			}
			return MTL::BlendFactorZero;
		}

		MTL::BlendOperation BlendModeOp(BlendMode mode)
		{
			switch (mode)
			{
			case BlendMode::SUBTRACT:
			case BlendMode::SUBTRACTALPHA:
				return MTL::BlendOperationReverseSubtract;
			default:
				return MTL::BlendOperationAdd;
			}
		}

		bool BlendEnabled(BlendMode mode)
		{
			return mode != BlendMode::REPLACE;
		}

		MTL::CompareFunction Compare(CompareMode mode)
		{
			switch (mode)
			{
			case CompareMode::LESS: return MTL::CompareFunctionLess;
			case CompareMode::GREATER: return MTL::CompareFunctionGreater;
			case CompareMode::EQUAL: return MTL::CompareFunctionEqual;
			case CompareMode::ALWAYS: return MTL::CompareFunctionAlways;
			case CompareMode::LEQUAL: return MTL::CompareFunctionLessEqual;
			case CompareMode::GEQUAL: return MTL::CompareFunctionGreaterEqual;
			case CompareMode::NOTEQUAL: return MTL::CompareFunctionNotEqual;
			}
			return MTL::CompareFunctionLess;
		}

		MTL::CullMode Cull(CullMode mode)
		{
			switch (mode)
			{
			case CullMode::FRONT: return MTL::CullModeFront;
			case CullMode::BACK: return MTL::CullModeBack;
			case CullMode::NONE: return MTL::CullModeNone;
			}
			return MTL::CullModeBack;
		}

		MTL::PrimitiveType Primitive(RHIPrimitiveMode mode)
		{
			switch (mode)
			{
			case RHIPrimitiveMode::TRIANGLES: return MTL::PrimitiveTypeTriangle;
			case RHIPrimitiveMode::LINES: return MTL::PrimitiveTypeLine;
			case RHIPrimitiveMode::LINE_STRIP: return MTL::PrimitiveTypeLineStrip;
			case RHIPrimitiveMode::LINE_LOOP: return MTL::PrimitiveTypeLineStrip;
			}
			return MTL::PrimitiveTypeTriangle;
		}

		MTL::VertexFormat VertexFormat(RHIVertexFormat format)
		{
			switch (format)
			{
			case RHIVertexFormat::FLOAT: return MTL::VertexFormatFloat;
			case RHIVertexFormat::FLOAT2: return MTL::VertexFormatFloat2;
			case RHIVertexFormat::FLOAT3: return MTL::VertexFormatFloat3;
			case RHIVertexFormat::FLOAT4: return MTL::VertexFormatFloat4;
			case RHIVertexFormat::INT4: return MTL::VertexFormatInt4;
			case RHIVertexFormat::UINT4: return MTL::VertexFormatUInt4;
			}
			return MTL::VertexFormatFloat3;
		}

		MTL::PixelFormat PixelFormat(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::R8: return MTL::PixelFormatR8Unorm;
			case TextureFormat::R16: return MTL::PixelFormatR16Unorm;
			case TextureFormat::R16F: return MTL::PixelFormatR16Float;
			case TextureFormat::R32F: return MTL::PixelFormatR32Float;
			case TextureFormat::R32UI: return MTL::PixelFormatR32Uint;
			case TextureFormat::RG8: return MTL::PixelFormatRG8Unorm;
			case TextureFormat::RG16: return MTL::PixelFormatRG16Unorm;
			case TextureFormat::RG16F: return MTL::PixelFormatRG16Float;
			case TextureFormat::RG32F: return MTL::PixelFormatRG32Float;
			case TextureFormat::RGB8: return MTL::PixelFormatRGBA8Unorm;
			case TextureFormat::RGB16: return MTL::PixelFormatRGBA16Unorm;
			case TextureFormat::RGB16F: return MTL::PixelFormatRGBA16Float;
			case TextureFormat::RGB32F: return MTL::PixelFormatRGBA32Float;
			case TextureFormat::RGBA8: return MTL::PixelFormatRGBA8Unorm;
			case TextureFormat::RGBA16: return MTL::PixelFormatRGBA16Unorm;
			case TextureFormat::RGBA16F: return MTL::PixelFormatRGBA16Float;
			case TextureFormat::RGBA32F: return MTL::PixelFormatRGBA32Float;
			case TextureFormat::SRGB: return MTL::PixelFormatRGBA8Unorm_sRGB;
			case TextureFormat::SRGB8: return MTL::PixelFormatRGBA8Unorm_sRGB;
			case TextureFormat::SRGB_ALPHA: return MTL::PixelFormatRGBA8Unorm_sRGB;
			case TextureFormat::SRGB8_ALPHA8: return MTL::PixelFormatRGBA8Unorm_sRGB;
			case TextureFormat::DEPTH16: return MTL::PixelFormatDepth16Unorm;
			// No 24-bit depth on Mac family GPUs; widen to 32F.
			case TextureFormat::DEPTH24: return MTL::PixelFormatDepth32Float;
			case TextureFormat::DEPTH32F: return MTL::PixelFormatDepth32Float;
			case TextureFormat::DEPTH32F_STENCIL8: return MTL::PixelFormatDepth32Float_Stencil8;
			case TextureFormat::DEPTH24_STENCIL8: return MTL::PixelFormatDepth32Float_Stencil8;
			case TextureFormat::BC1_UNORM: return MTL::PixelFormatBC1_RGBA;
			case TextureFormat::BC1_SRGB: return MTL::PixelFormatBC1_RGBA_sRGB;
			case TextureFormat::BC3_UNORM: return MTL::PixelFormatBC3_RGBA;
			case TextureFormat::BC3_SRGB: return MTL::PixelFormatBC3_RGBA_sRGB;
			case TextureFormat::BC5_UNORM: return MTL::PixelFormatBC5_RGUnorm;
			case TextureFormat::BC6H_UF: return MTL::PixelFormatBC6H_RGBUfloat;
			case TextureFormat::BC7_UNORM: return MTL::PixelFormatBC7_RGBAUnorm;
			case TextureFormat::BC7_SRGB: return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
			default: return MTL::PixelFormatInvalid;
			}
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
			case TextureFormat::RGB8: return 4;
			case TextureFormat::RGB16: return 8;
			case TextureFormat::RGB16F: return 8;
			case TextureFormat::RGB32F: return 16;
			case TextureFormat::RGBA8: return 4;
			case TextureFormat::RGBA16: return 8;
			case TextureFormat::RGBA16F: return 8;
			case TextureFormat::RGBA32F: return 16;
			case TextureFormat::SRGB: return 4;
			case TextureFormat::SRGB8: return 4;
			case TextureFormat::SRGB_ALPHA: return 4;
			case TextureFormat::SRGB8_ALPHA8: return 4;
			case TextureFormat::DEPTH16: return 2;
			case TextureFormat::DEPTH24: return 4;
			case TextureFormat::DEPTH32F: return 4;
			case TextureFormat::DEPTH32F_STENCIL8: return 5;
			case TextureFormat::DEPTH24_STENCIL8: return 5;
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

		unsigned int ChannelCount(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::R16F:
			case TextureFormat::R32F:
				return 1;
			case TextureFormat::RG16F:
			case TextureFormat::RG32F:
				return 2;
			case TextureFormat::RGB16F:
			case TextureFormat::RGB32F:
				return 3;
			case TextureFormat::RGBA16F:
			case TextureFormat::RGBA32F:
				return 4;
			default:
				return 0;
			}
		}

		unsigned int FloatComponentBytes(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::R16F:
			case TextureFormat::RG16F:
			case TextureFormat::RGB16F:
			case TextureFormat::RGBA16F:
				return 2;
			case TextureFormat::R32F:
			case TextureFormat::RG32F:
			case TextureFormat::RGB32F:
			case TextureFormat::RGBA32F:
				return 4;
			default:
				return 0;
			}
		}

		MTL::SamplerMinMagFilter SamplerFilter(FilterMode mode)
		{
			switch (mode)
			{
			case FilterMode::NEAREST:
			case FilterMode::NEAREST_MIPMAP_NEAREST:
			case FilterMode::NEAREST_MIPMAP_LINEAR:
				return MTL::SamplerMinMagFilterNearest;
			default:
				return MTL::SamplerMinMagFilterLinear;
			}
		}

		MTL::SamplerMipFilter SamplerMipFilter(FilterMode mode)
		{
			switch (mode)
			{
			case FilterMode::NEAREST_MIPMAP_NEAREST:
			case FilterMode::LINEAR_MIPMAP_NEAREST:
				return MTL::SamplerMipFilterNearest;
			case FilterMode::NEAREST_MIPMAP_LINEAR:
			case FilterMode::LINEAR_MIPMAP_LINEAR:
				return MTL::SamplerMipFilterLinear;
			default:
				return MTL::SamplerMipFilterNotMipmapped;
			}
		}

		MTL::SamplerAddressMode SamplerWrap(WrapMode mode)
		{
			switch (mode)
			{
			case WrapMode::REPEAT: return MTL::SamplerAddressModeRepeat;
			case WrapMode::MIRRORED_REPEAT: return MTL::SamplerAddressModeMirrorRepeat;
			case WrapMode::CLAMP_TO_EDGE: return MTL::SamplerAddressModeClampToEdge;
			case WrapMode::CLAMP_TO_BORDER: return MTL::SamplerAddressModeClampToBorderColor;
			}
			return MTL::SamplerAddressModeRepeat;
		}

		MTL::TextureType MTLTextureType(TextureType type)
		{
			switch (type)
			{
			case TextureType::TEXTURE_1D: return MTL::TextureType1D;
			case TextureType::TEXTURE_2D: return MTL::TextureType2D;
			case TextureType::TEXTURE_2D_ARRAY: return MTL::TextureType2DArray;
			case TextureType::TEXTURE_CUBE_MAP: return MTL::TextureTypeCube;
			case TextureType::TEXTURE_3D: return MTL::TextureType3D;
			}
			return MTL::TextureType2D;
		}
	}
}

#endif // FURY_RHI_METAL
