#include "Fury/RHI/OpenGL/GLEnumMap.h"

#include "Fury/GLLoader.h"

// S3TC/BPTC constants glLoadGen did not emit.
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
#endif
#ifndef GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT 0x8E8F
#endif

namespace fury
{
	unsigned int GLEnumMap::CompareModeToGL(CompareMode mode)
	{
		switch (mode)
		{
		case CompareMode::LESS: return GL_LESS;
		case CompareMode::GREATER: return GL_GREATER;
		case CompareMode::EQUAL: return GL_EQUAL;
		case CompareMode::ALWAYS: return GL_ALWAYS;
		case CompareMode::LEQUAL: return GL_LEQUAL;
		case CompareMode::GEQUAL: return GL_GEQUAL;
		case CompareMode::NOTEQUAL: return GL_NOTEQUAL;
		}
		return GL_LESS;
	}

	unsigned int GLEnumMap::BlendModeSrcToGL(BlendMode mode)
	{
		switch (mode)
		{
		case BlendMode::REPLACE: return GL_ONE;
		case BlendMode::ADD: return GL_ONE;
		case BlendMode::MULTIPLY: return GL_DST_COLOR;
		case BlendMode::ALPHA: return GL_SRC_ALPHA;
		case BlendMode::ADDALPHA: return GL_SRC_ALPHA;
		case BlendMode::PREMULALPHA: return GL_ONE;
		case BlendMode::INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
		case BlendMode::SUBTRACT: return GL_ONE;
		case BlendMode::SUBTRACTALPHA: return GL_SRC_ALPHA;
		}
		return GL_ONE;
	}

	unsigned int GLEnumMap::BlendModeDestToGL(BlendMode mode)
	{
		switch (mode)
		{
		case BlendMode::REPLACE: return GL_ZERO;
		case BlendMode::ADD: return GL_ONE;
		case BlendMode::MULTIPLY: return GL_ZERO;
		case BlendMode::ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
		case BlendMode::ADDALPHA: return GL_ONE;
		case BlendMode::PREMULALPHA: return GL_ONE_MINUS_SRC_ALPHA;
		case BlendMode::INVDESTALPHA: return GL_DST_ALPHA;
		case BlendMode::SUBTRACT: return GL_ONE;
		case BlendMode::SUBTRACTALPHA: return GL_ONE;
		}
		return GL_ZERO;
	}

	unsigned int GLEnumMap::BlendModeOpToGL(BlendMode mode)
	{
		switch (mode)
		{
		case BlendMode::SUBTRACT:
		case BlendMode::SUBTRACTALPHA:
			return GL_FUNC_REVERSE_SUBTRACT;
		default:
			return GL_FUNC_ADD;
		}
	}

	std::pair<bool, unsigned int> GLEnumMap::CullModeToGL(CullMode mode)
	{
		bool flag = mode != CullMode::NONE;
		unsigned int data = 0;

		if (mode == CullMode::BACK)
			data = GL_BACK;
		else if (mode == CullMode::FRONT)
			data = GL_FRONT;

		return std::make_pair(flag, data);
	}

	std::pair<bool, unsigned int> GLEnumMap::TextureFormatToGL(TextureFormat format, bool internalFormat)
	{
		switch (format)
		{
		case TextureFormat::R8: return { false, internalFormat ? GL_R8 : GL_RED };
		case TextureFormat::R16: return { false, internalFormat ? GL_R16 : GL_RED };
		case TextureFormat::R16F: return { false, internalFormat ? GL_R16F : GL_RED };
		case TextureFormat::R32F: return { false, internalFormat ? GL_R32F : GL_RED };
		case TextureFormat::R32UI: return { false, internalFormat ? GL_R32UI : GL_RED_INTEGER };
		case TextureFormat::RG8: return { false, internalFormat ? GL_RG8 : GL_RG };
		case TextureFormat::RG16: return { false, internalFormat ? GL_RG16 : GL_RG };
		case TextureFormat::RG16F: return { false, internalFormat ? GL_RG16F : GL_RG };
		case TextureFormat::RG32F: return { false, internalFormat ? GL_RG32F : GL_RG };
		case TextureFormat::RGB8: return { false, internalFormat ? GL_RGB8 : GL_RGB };
		case TextureFormat::RGB16: return { false, internalFormat ? GL_RGB16 : GL_RGB };
		case TextureFormat::RGB16F: return { false, internalFormat ? GL_RGB16F : GL_RGB };
		case TextureFormat::RGB32F: return { false, internalFormat ? GL_RGB32F : GL_RGB };
		case TextureFormat::RGBA8: return { false, internalFormat ? GL_RGBA8 : GL_RGBA };
		case TextureFormat::RGBA16: return { false, internalFormat ? GL_RGBA16 : GL_RGBA };
		case TextureFormat::RGBA16F: return { false, internalFormat ? GL_RGBA16F : GL_RGBA };
		case TextureFormat::RGBA32F: return { false, internalFormat ? GL_RGBA32F : GL_RGBA };
		case TextureFormat::SRGB: return { false, internalFormat ? GL_SRGB : GL_RGB };
		case TextureFormat::SRGB8: return { false, internalFormat ? GL_SRGB8 : GL_RGB };
		// SRGB_ALPHA is a non-sized format -- not valid for glTexStorage2D.
		case TextureFormat::SRGB_ALPHA: return { false, internalFormat ? GL_SRGB_ALPHA : GL_RGBA };
		case TextureFormat::SRGB8_ALPHA8: return { false, internalFormat ? GL_SRGB8_ALPHA8 : GL_RGBA };
		case TextureFormat::DEPTH16: return { false, internalFormat ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT };
		case TextureFormat::DEPTH24: return { false, internalFormat ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT };
		case TextureFormat::DEPTH32F: return { false, internalFormat ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT };
		case TextureFormat::DEPTH32F_STENCIL8: return { false, internalFormat ? GL_DEPTH32F_STENCIL8 : GL_DEPTH_STENCIL };
		case TextureFormat::DEPTH24_STENCIL8: return { false, internalFormat ? GL_DEPTH24_STENCIL8 : GL_DEPTH_STENCIL };
		// Compressed formats have no base-format component.
		case TextureFormat::BC1_UNORM: return { false, internalFormat ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : 0 };
		case TextureFormat::BC1_SRGB: return { false, internalFormat ? GL_COMPRESSED_SRGB_S3TC_DXT1_EXT : 0 };
		case TextureFormat::BC3_UNORM: return { false, internalFormat ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : 0 };
		case TextureFormat::BC3_SRGB: return { false, internalFormat ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : 0 };
		case TextureFormat::BC5_UNORM: return { false, internalFormat ? GL_COMPRESSED_RG_RGTC2 : 0 };
		case TextureFormat::BC6H_UF: return { false, internalFormat ? GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT : 0 };
		case TextureFormat::BC7_UNORM: return { false, internalFormat ? GL_COMPRESSED_RGBA_BPTC_UNORM : 0 };
		case TextureFormat::BC7_SRGB: return { false, internalFormat ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM : 0 };
		case TextureFormat::UNKNOW: break;
		}
		return { true, 0 };
	}

	unsigned int GLEnumMap::TextureTypeToGL(TextureType type)
	{
		switch (type)
		{
		case TextureType::TEXTURE_1D: return GL_TEXTURE_1D;
		case TextureType::TEXTURE_2D: return GL_TEXTURE_2D;
		case TextureType::TEXTURE_2D_ARRAY: return GL_TEXTURE_2D_ARRAY;
		case TextureType::TEXTURE_CUBE_MAP: return GL_TEXTURE_CUBE_MAP;
		case TextureType::TEXTURE_3D: return GL_TEXTURE_3D;
		}
		return GL_TEXTURE_2D;
	}

	unsigned int GLEnumMap::FilterModeToGL(FilterMode mode)
	{
		switch (mode)
		{
		case FilterMode::NEAREST: return GL_NEAREST;
		case FilterMode::LINEAR: return GL_LINEAR;
		case FilterMode::NEAREST_MIPMAP_NEAREST: return GL_NEAREST_MIPMAP_NEAREST;
		case FilterMode::NEAREST_MIPMAP_LINEAR: return GL_NEAREST_MIPMAP_LINEAR;
		case FilterMode::LINEAR_MIPMAP_NEAREST: return GL_LINEAR_MIPMAP_NEAREST;
		case FilterMode::LINEAR_MIPMAP_LINEAR: return GL_LINEAR_MIPMAP_LINEAR;
		}
		return GL_LINEAR;
	}

	unsigned int GLEnumMap::WrapModeToGL(WrapMode mode)
	{
		switch (mode)
		{
		case WrapMode::REPEAT: return GL_REPEAT;
		case WrapMode::MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
		case WrapMode::CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
		case WrapMode::CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;
		}
		return GL_CLAMP_TO_EDGE;
	}

	unsigned int GLEnumMap::LineModeToGL(LineMode mode)
	{
		switch (mode)
		{
		case LineMode::LINES: return GL_LINES;
		case LineMode::LINE_LOOP: return GL_LINE_LOOP;
		case LineMode::LINE_STRIP: return GL_LINE_STRIP;
		}
		return GL_LINES;
	}
}
