#include "Fury/EnumUtil.h"

#include <cctype>

#undef OPAQUE
#undef TRANSPARENT

namespace fury
{
	const std::vector<std::pair<ClearMode, std::string>> EnumUtil::m_ClearMode =
	{
		std::make_pair(ClearMode::NONE, "none"),
		std::make_pair(ClearMode::COLOR, "color"),
		std::make_pair(ClearMode::DEPTH, "depth"),
		std::make_pair(ClearMode::STENCIL, "stencil"),
		std::make_pair(ClearMode::COLOR_DEPTH, "color_depth"),
		std::make_pair(ClearMode::COLOR_STENCIL, "color_stencil"),
		std::make_pair(ClearMode::STENCIL_DEPTH, "stencil_depth"),
		std::make_pair(ClearMode::COLOR_DEPTH_STENCIL, "color_depth_stencil")
	};

	const std::vector<std::pair<CompareMode, std::string>> EnumUtil::m_CompareMode =
	{
		std::make_pair(CompareMode::LESS, "less"),
		std::make_pair(CompareMode::GREATER, "greater"),
		std::make_pair(CompareMode::EQUAL, "equal"),
		std::make_pair(CompareMode::ALWAYS, "always"),
		std::make_pair(CompareMode::LEQUAL, "lequal"),
		std::make_pair(CompareMode::GEQUAL, "gequal"),
		std::make_pair(CompareMode::NOTEQUAL, "notequal")
	};

	const std::vector<std::pair<BlendMode, std::string>> EnumUtil::m_BlendMode =
	{
		std::make_pair(BlendMode::REPLACE, "replace"),
		std::make_pair(BlendMode::ADD, "add"),
		std::make_pair(BlendMode::MULTIPLY, "multiply"),
		std::make_pair(BlendMode::ALPHA, "alpha"),
		std::make_pair(BlendMode::ADDALPHA, "addalpha"),
		std::make_pair(BlendMode::PREMULALPHA, "premulalpha"),
		std::make_pair(BlendMode::INVDESTALPHA, "invdestalpha"),
		std::make_pair(BlendMode::SUBTRACT, "subtract"),
		std::make_pair(BlendMode::SUBTRACTALPHA, "subtractalpha")
	};

	const std::vector<std::pair<TextureFormat, std::string>> EnumUtil::m_TextureFormat =
	{
		std::make_pair(TextureFormat::UNKNOW, ""),
		std::make_pair(TextureFormat::R8, "r8"),
		std::make_pair(TextureFormat::R16, "r16"),
		std::make_pair(TextureFormat::R16F, "r16f"),
		std::make_pair(TextureFormat::R32F, "r32f"),
		std::make_pair(TextureFormat::R32UI, "r32ui"),
		std::make_pair(TextureFormat::RG8, "rg8"),
		std::make_pair(TextureFormat::RG16, "rg16"),
		std::make_pair(TextureFormat::RG16F, "rg16f"),
		std::make_pair(TextureFormat::RG32F, "rg32f"),
		std::make_pair(TextureFormat::RGB8, "rgb8"),
		std::make_pair(TextureFormat::RGB16, "rgb16"),
		std::make_pair(TextureFormat::RGB16F, "rgb16f"),
		std::make_pair(TextureFormat::RGB32F, "rgb32f"),
		std::make_pair(TextureFormat::RGBA8, "rgba8"),
		std::make_pair(TextureFormat::RGBA16, "rgba16"),
		std::make_pair(TextureFormat::RGBA16F, "rgba16f"),
		std::make_pair(TextureFormat::RGBA32F, "rgba32f"),
		std::make_pair(TextureFormat::SRGB, "srgb"),
		std::make_pair(TextureFormat::SRGB8, "srgb8"),
		std::make_pair(TextureFormat::SRGB_ALPHA, "srgba"),
		std::make_pair(TextureFormat::SRGB8_ALPHA8, "srgba8"),
		std::make_pair(TextureFormat::DEPTH16, "depth16"),
		std::make_pair(TextureFormat::DEPTH24, "depth24"),
		std::make_pair(TextureFormat::DEPTH32F, "depth32f"),
		std::make_pair(TextureFormat::DEPTH32F_STENCIL8, "depth32f_stencil8"),
		std::make_pair(TextureFormat::DEPTH24_STENCIL8, "depth24_stencil8"),
		std::make_pair(TextureFormat::BC1_UNORM, "bc1"),
		std::make_pair(TextureFormat::BC1_SRGB, "bc1_srgb"),
		std::make_pair(TextureFormat::BC3_UNORM, "bc3"),
		std::make_pair(TextureFormat::BC3_SRGB, "bc3_srgb"),
		std::make_pair(TextureFormat::BC5_UNORM, "bc5"),
		std::make_pair(TextureFormat::BC6H_UF, "bc6h"),
		std::make_pair(TextureFormat::BC7_UNORM, "bc7"),
		std::make_pair(TextureFormat::BC7_SRGB, "bc7_srgb")
	};

	const std::vector<std::pair<TextureFormat, unsigned int>> EnumUtil::m_TextureFormatBitPerPixel =
	{
		std::make_pair(TextureFormat::UNKNOW, 0),
		std::make_pair(TextureFormat::R8, 8),
		std::make_pair(TextureFormat::R16, 16),
		std::make_pair(TextureFormat::R16F, 16),
		std::make_pair(TextureFormat::R32F, 32),
		std::make_pair(TextureFormat::R32UI, 32),
		std::make_pair(TextureFormat::RG8, 16),
		std::make_pair(TextureFormat::RG16, 32),
		std::make_pair(TextureFormat::RG16F, 32),
		std::make_pair(TextureFormat::RG32F, 64),
		std::make_pair(TextureFormat::RGB8, 24),
		std::make_pair(TextureFormat::RGB16, 48),
		std::make_pair(TextureFormat::RGB16F, 48),
		std::make_pair(TextureFormat::RGB32F, 96),
		std::make_pair(TextureFormat::RGBA8, 32),
		std::make_pair(TextureFormat::RGBA16, 64),
		std::make_pair(TextureFormat::RGBA16F, 64),
		std::make_pair(TextureFormat::RGBA32F, 128),
		std::make_pair(TextureFormat::SRGB, 24),
		std::make_pair(TextureFormat::SRGB8, 24),
		std::make_pair(TextureFormat::SRGB8_ALPHA8, 32),
		std::make_pair(TextureFormat::SRGB_ALPHA, 32),
		std::make_pair(TextureFormat::DEPTH16, 16),
		std::make_pair(TextureFormat::DEPTH24, 24),
		std::make_pair(TextureFormat::DEPTH32F, 32),
		std::make_pair(TextureFormat::DEPTH32F_STENCIL8, 40),
		std::make_pair(TextureFormat::DEPTH24_STENCIL8, 32),
		std::make_pair(TextureFormat::BC1_UNORM, 4),
		std::make_pair(TextureFormat::BC1_SRGB, 4),
		std::make_pair(TextureFormat::BC3_UNORM, 8),
		std::make_pair(TextureFormat::BC3_SRGB, 8),
		std::make_pair(TextureFormat::BC5_UNORM, 8),
		std::make_pair(TextureFormat::BC6H_UF, 8),
		std::make_pair(TextureFormat::BC7_UNORM, 8),
		std::make_pair(TextureFormat::BC7_SRGB, 8)
	};

	const std::vector<std::pair<TextureType, std::string>> EnumUtil::m_TextureType =
	{
		std::make_pair(TextureType::TEXTURE_1D, "1d"),
		std::make_pair(TextureType::TEXTURE_2D, "2d"),
		std::make_pair(TextureType::TEXTURE_2D_ARRAY, "2d_array"),
		std::make_pair(TextureType::TEXTURE_CUBE_MAP, "cube"),
		std::make_pair(TextureType::TEXTURE_3D, "3d")
	};

	const std::vector<std::pair<FilterMode, std::string>> EnumUtil::m_FilterMode =
	{
		std::make_pair(FilterMode::NEAREST, "nearest"),
		std::make_pair(FilterMode::LINEAR, "linear"),
		std::make_pair(FilterMode::NEAREST_MIPMAP_NEAREST, "nearest_mipmap_nearest"),
		std::make_pair(FilterMode::NEAREST_MIPMAP_LINEAR, "nearest_mipmap_linear"),
		std::make_pair(FilterMode::LINEAR_MIPMAP_NEAREST, "linear_mipmap_nearest"),
		std::make_pair(FilterMode::LINEAR_MIPMAP_LINEAR, "linear_mipmap_linear")
	};

	const std::vector<std::pair<WrapMode, std::string>> EnumUtil::m_WrapMode =
	{
		std::make_pair(WrapMode::REPEAT, "repeat"),
		std::make_pair(WrapMode::MIRRORED_REPEAT, "mirrored_repeat"),
		std::make_pair(WrapMode::CLAMP_TO_EDGE, "clamp_to_edge"),
		std::make_pair(WrapMode::CLAMP_TO_BORDER, "clamp_to_border")
	};

	const std::vector<std::pair<LightType, std::string>> EnumUtil::m_LightType =
	{
		std::make_pair(LightType::DIRECTIONAL, "directional"),
		std::make_pair(LightType::POINT, "point"),
		std::make_pair(LightType::SPOT, "spot")
	};

	const std::vector<std::pair<ShaderType, std::string>> EnumUtil::m_ShaderType =
	{
		std::make_pair(ShaderType::OTHER, "other"),
		std::make_pair(ShaderType::STATIC_MESH, "static_mesh"),
		std::make_pair(ShaderType::SKINNED_MESH, "skinned_mesh")
	};

	const std::vector<std::pair<ShaderTexture, std::string>> EnumUtil::m_ShaderTexture =
	{
		std::make_pair(ShaderTexture::COLOR_ONLY, "color_only"),
		std::make_pair(ShaderTexture::DIFFUSE, "diffuse"),
		std::make_pair(ShaderTexture::SPECULAR, "specular"),
		std::make_pair(ShaderTexture::NORMAL, "normal"),
		std::make_pair(ShaderTexture::ALPHA_TEST, "alpha_test"),
		// Shadow-receive variant bit -- pipeline JSONs pair "shadow"
		// with a SHADOW define so the shadow block compiles only in
		// the *_shadow_shader variants (see Forward.glsl).
		std::make_pair(ShaderTexture::SHADOW, "shadow"),
		std::make_pair(ShaderTexture::TWO_SIDED, "two_sided"),
		std::make_pair(ShaderTexture::WIND, "wind"),
		std::make_pair(ShaderTexture::BILLBOARD, "billboard"),
		std::make_pair(ShaderTexture::INSTANCED, "instanced"),
		std::make_pair(ShaderTexture::INSTANCE_SSBO, "instance_ssbo")
	};

	const std::vector<std::pair<AnimWrapMode, std::string>> EnumUtil::m_AnimWrapMode =
	{
		std::make_pair(AnimWrapMode::Default, "default"),
		std::make_pair(AnimWrapMode::Once, "once"),
		std::make_pair(AnimWrapMode::Loop, "loop"),
		std::make_pair(AnimWrapMode::ClampForever, "clamp_forever"),
		std::make_pair(AnimWrapMode::PingPong, "ping_pong")
	};

	const std::vector<std::pair<PlayMode, std::string>> EnumUtil::m_PlayMode =
	{
		std::make_pair(PlayMode::StopSameLayer, "stop_same_layer"),
		std::make_pair(PlayMode::StopAll, "stop_all")
	};

	const std::vector<std::pair<AlphaMode, std::string>> EnumUtil::m_AlphaMode =
	{
		std::make_pair(AlphaMode::OPAQUE, "opaque"),
		std::make_pair(AlphaMode::MASK, "mask"),
		std::make_pair(AlphaMode::BLEND, "blend")
	};


	std::string EnumUtil::ClearModeToString(ClearMode mode)
	{
		return m_ClearMode[(unsigned int)mode].second;
	}

	ClearMode EnumUtil::ClearModeFromString(const std::string &name)
	{
		for (const auto &mode : m_ClearMode)
		{
			if (mode.second == name)
				return mode.first;
		}
		return ClearMode::COLOR_DEPTH_STENCIL;
	}

	std::string EnumUtil::CompareModeToString(CompareMode mode)
	{
		return m_CompareMode[(int)mode].second;
	}

	CompareMode EnumUtil::CompareModeFromString(const std::string &name)
	{
		for (const auto &mode : m_CompareMode)
		{
			if (mode.second == name)
				return mode.first;
		}
		return CompareMode::LESS;
	}

	std::string EnumUtil::BlendModeToString(BlendMode mode)
	{
		return m_BlendMode[(int)mode].second;
	}

	BlendMode EnumUtil::BlendModeFromString(const std::string &name)
	{
		for (const auto &mode : m_BlendMode)
		{
			if (mode.second == name)
				return mode.first;
		}
		return BlendMode::REPLACE;
	}

	std::string EnumUtil::CullModeToString(CullMode mode)
	{
		if (mode == CullMode::BACK)
			return "back";
		else if (mode == CullMode::FRONT)
			return "front";
		else
			return "";
	}

	CullMode EnumUtil::CullModeFromString(const std::string &name)
	{
		if (name == "back")
			return CullMode::BACK;
		else if (name == "front")
			return CullMode::FRONT;
		else
			return CullMode::NONE;
	}

	std::string EnumUtil::DrawModeToString(DrawMode mode)
	{
		switch (mode)
		{
		case DrawMode::OPAQUE:
			return "opaque";
		case DrawMode::TRANSPARENT:
			return "transparent";
		case DrawMode::LIGHT:
			return "light";
		case DrawMode::SKY:
			return "sky";
		case DrawMode::OCEAN:
			return "ocean";
		case DrawMode::QUAD:
		default:
			return "quad";
		}
	}

	DrawMode EnumUtil::DrawModeFromString(const std::string &name)
	{
		if (name == "opaque")
			return DrawMode::OPAQUE;
		else if (name == "transparent")
			return DrawMode::TRANSPARENT;
		else if (name == "light")
			return DrawMode::LIGHT;
		else if (name == "sky")
			return DrawMode::SKY;
		else if (name == "ocean")
			return DrawMode::OCEAN;
		else
			return DrawMode::QUAD;
	}

	bool EnumUtil::TextureFormatIsCompressed(TextureFormat format)
	{
		return (int)format >= (int)TextureFormat::BC1_UNORM;
	}

	TextureFormat EnumUtil::TextureFormatFromVkFormat(unsigned int vkFormat)
	{
		// Vulkan header values for the BC block-compressed formats.
		switch (vkFormat)
		{
		case 131: return TextureFormat::BC1_UNORM; // BC1_RGB_UNORM_BLOCK
		case 132: return TextureFormat::BC1_SRGB;  // BC1_RGB_SRGB_BLOCK
		case 137: return TextureFormat::BC3_UNORM; // BC3_UNORM_BLOCK
		case 138: return TextureFormat::BC3_SRGB;  // BC3_SRGB_BLOCK
		case 141: return TextureFormat::BC5_UNORM; // BC5_UNORM_BLOCK
		case 143: return TextureFormat::BC6H_UF;   // BC6H_UFLOAT_BLOCK
		case 145: return TextureFormat::BC7_UNORM; // BC7_UNORM_BLOCK
		case 146: return TextureFormat::BC7_SRGB;  // BC7_SRGB_BLOCK
		default: return TextureFormat::UNKNOW;
		}
	}

	std::string EnumUtil::TextureFormatToString(TextureFormat foramt)
	{
		return m_TextureFormat[(int)foramt].second;
	}

	TextureFormat EnumUtil::TextureFormatFromString(const std::string &name)
	{
		for (const auto &format : m_TextureFormat)
		{
			if (format.second == name)
				return format.first;
		}
		return TextureFormat::RGBA8;
	}

	unsigned int EnumUtil::TextureBitPerPixel(TextureFormat format)
	{
		for (const auto &pair : m_TextureFormatBitPerPixel)
		{
			if (pair.first == format)
				return pair.second;
		}
		return 0;
	}

	std::string EnumUtil::TextureTypeToString(TextureType type)
	{
		return m_TextureType[(int)type].second;
	}

	TextureType EnumUtil::TextureTypeFromString(const std::string &name)
	{
		for (auto const &type : m_TextureType)
		{
			if (type.second == name)
				return type.first;
		}
		return TextureType::TEXTURE_2D;
	}

	std::string EnumUtil::FilterModeToString(FilterMode mode)
	{
		return m_FilterMode[(int)mode].second;
	}

	FilterMode EnumUtil::FilterModeFromString(const std::string &name)
	{
		for (const auto &mode : m_FilterMode)
		{
			if (mode.second == name)
				return mode.first;
		}
		return FilterMode::LINEAR;
	}

	std::string EnumUtil::WrapModeToString(WrapMode mode)
	{
		return m_WrapMode[(int)mode].second;
	}

	WrapMode EnumUtil::WrapModeFromString(const std::string &name)
	{
		for (const auto &mode : m_WrapMode)
		{
			if (mode.second == name)
				return mode.first;
		}
		return WrapMode::CLAMP_TO_EDGE;
	}

	std::string EnumUtil::LightTypeToString(LightType type)
	{
		return m_LightType[(int)type].second;
	}

	LightType EnumUtil::LightTypeFromString(const std::string &name)
	{
		for (const auto &pair : m_LightType)
		{
			if (pair.second == name)
				return pair.first;
		}
		return LightType::DIRECTIONAL;
	}

	std::string EnumUtil::ShaderTypeToString(ShaderType type)
	{
		return m_ShaderType[(int)type].second;
	}

	ShaderType EnumUtil::ShaderTypeFromString(const std::string &name)
	{
		for (const auto &pair : m_ShaderType)
		{
			if (pair.second == name)
				return pair.first;
		}
		return ShaderType::OTHER;
	}

	std::string EnumUtil::ShaderTextureToString(ShaderTexture texture)
	{
		for (const auto &pair : m_ShaderTexture)
		{
			if (pair.first == texture)
				return pair.second;
		}
		return "";
	}

	ShaderTexture EnumUtil::ShaderTextureFromString(const std::string &name)
	{
		for (const auto &pair : m_ShaderTexture)
		{
			if (pair.second == name)
				return pair.first;
		}
		return ShaderTexture::COLOR_ONLY;
	}

	void EnumUtil::GetShaderTextures(unsigned int flags, std::vector<ShaderTexture> &textures)
	{
		std::vector<ShaderTexture> enums = { ShaderTexture::COLOR_ONLY, ShaderTexture::DIFFUSE,
			ShaderTexture::NORMAL, ShaderTexture::SPECULAR };
		for (auto single : enums)
		{
			if (flags & (unsigned int)single)
				textures.push_back(single);
		}
	}

	std::string EnumUtil::AnimWrapModeToString(AnimWrapMode mode)
	{
		return m_AnimWrapMode[(unsigned int)mode].second;
	}

	AnimWrapMode EnumUtil::AnimWrapModeFromString(const std::string &name)
	{
		for (const auto &pair : m_AnimWrapMode)
		{
			if (pair.second == name)
				return pair.first;
		}
		return AnimWrapMode::Default;
	}

	std::string EnumUtil::PlayModeToString(PlayMode mode)
	{
		return m_PlayMode[(unsigned int)mode].second;
	}

	PlayMode EnumUtil::PlayModeFromString(const std::string &name)
	{
		for (const auto &pair : m_PlayMode)
		{
			if (pair.second == name)
				return pair.first;
		}
		return PlayMode::StopAll;
	}

	std::string EnumUtil::PostProcessStageToString(PostProcessStage stage)
	{
		switch (stage)
		{
		case PostProcessStage::PRE_TONEMAP:
			return "pre_tonemap";
		case PostProcessStage::TONEMAP:
			return "tonemap";
		case PostProcessStage::POST_TONEMAP:
		default:
			return "post_tonemap";
		}
	}

	PostProcessStage EnumUtil::PostProcessStageFromString(const std::string &name)
	{
		if (name == "pre_tonemap")
			return PostProcessStage::PRE_TONEMAP;
		else if (name == "tonemap")
			return PostProcessStage::TONEMAP;
		else
			return PostProcessStage::POST_TONEMAP;
	}

	std::string EnumUtil::AlphaModeToString(AlphaMode mode)
	{
		return m_AlphaMode[(int)mode].second;
	}

	AlphaMode EnumUtil::AlphaModeFromString(const std::string &name)
	{
		std::string lower = name;
		for (auto &c : lower)
			c = (char)tolower((unsigned char)c);
		for (const auto &pair : m_AlphaMode)
		{
			if (pair.second == lower)
				return pair.first;
		}
		return AlphaMode::OPAQUE;
	}
}
