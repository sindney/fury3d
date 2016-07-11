#include "EnumUtil.h"
#include "GLLoader.h"

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

	const std::vector<std::tuple<CompareMode, unsigned int, std::string>> EnumUtil::m_CompareMode =
	{
		std::make_tuple(CompareMode::LESS, GL_LESS, "less"),
		std::make_tuple(CompareMode::GREATER, GL_GREATER, "greater"),
		std::make_tuple(CompareMode::EQUAL, GL_EQUAL, "equal"),
		std::make_tuple(CompareMode::ALWAYS, GL_ALWAYS, "always"),
		std::make_tuple(CompareMode::LEQUAL, GL_LEQUAL, "lequal"),
		std::make_tuple(CompareMode::GEQUAL, GL_GEQUAL, "gequal"),
		std::make_tuple(CompareMode::NOTEQUAL, GL_NOTEQUAL, "notequal")
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

	const std::vector<unsigned int> EnumUtil::m_BlendModeSrc =
	{
		GL_ONE,
		GL_ONE,
		GL_DST_COLOR,
		GL_SRC_ALPHA,
		GL_SRC_ALPHA,
		GL_ONE,
		GL_ONE_MINUS_DST_ALPHA,
		GL_ONE,
		GL_SRC_ALPHA
	};

	const std::vector<unsigned int> EnumUtil::m_BlendModeDest =
	{
		GL_ZERO,
		GL_ONE,
		GL_ZERO,
		GL_ONE_MINUS_SRC_ALPHA,
		GL_ONE,
		GL_ONE_MINUS_SRC_ALPHA,
		GL_DST_ALPHA,
		GL_ONE,
		GL_ONE
	};

	const std::vector<unsigned int> EnumUtil::m_BlendModeOp = 
	{
		GL_FUNC_ADD,
		GL_FUNC_ADD,
		GL_FUNC_ADD,
		GL_FUNC_ADD,
		GL_FUNC_ADD,
		GL_FUNC_ADD,
		GL_FUNC_ADD,
		GL_FUNC_REVERSE_SUBTRACT,
		GL_FUNC_REVERSE_SUBTRACT
	};

	const std::vector<std::tuple<TextureFormat, std::string, unsigned int, unsigned int>> EnumUtil::m_TextureFormat =
	{
		std::make_tuple(TextureFormat::UNKNOW, "", 0, 0),
		std::make_tuple(TextureFormat::R8, "r8", GL_R8, GL_RED),
		std::make_tuple(TextureFormat::R16, "r16", GL_R16, GL_RED),
		std::make_tuple(TextureFormat::R16F, "r16f", GL_R16F, GL_RED),
		std::make_tuple(TextureFormat::R32F, "r32f", GL_R32F, GL_RED),
		std::make_tuple(TextureFormat::RG8, "rg8", GL_RG8, GL_RG),
		std::make_tuple(TextureFormat::RG16, "rg16", GL_RG16, GL_RG),
		std::make_tuple(TextureFormat::RG16F, "rg16f", GL_RG16F, GL_RG),
		std::make_tuple(TextureFormat::RG32F, "rg32f", GL_RG32F, GL_RG),
		std::make_tuple(TextureFormat::RGB8, "rgb8", GL_RGB8, GL_RGB),
		std::make_tuple(TextureFormat::RGB16, "rgb16", GL_RGB16, GL_RGB),
		std::make_tuple(TextureFormat::RGB16F, "rgb16f", GL_RGB16F, GL_RGB),
		std::make_tuple(TextureFormat::RGB32F, "rgb32f", GL_RGB32F, GL_RGB),
		std::make_tuple(TextureFormat::RGBA8, "rgba8", GL_RGBA8, GL_RGBA),
		std::make_tuple(TextureFormat::RGBA16, "rgba16", GL_RGBA16, GL_RGBA),
		std::make_tuple(TextureFormat::RGBA16F, "rgba16f", GL_RGBA16F, GL_RGBA),
		std::make_tuple(TextureFormat::RGBA32F, "rgba32f", GL_RGBA32F, GL_RGBA),
		std::make_tuple(TextureFormat::DEPTH16, "depth16", GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT),
		std::make_tuple(TextureFormat::DEPTH24, "depth24", GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT),
		std::make_tuple(TextureFormat::DEPTH32F, "depth32f", GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT),
		std::make_tuple(TextureFormat::DEPTH32F_STENCIL8, "depth32f_stencil8", GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL),
		std::make_tuple(TextureFormat::DEPTH24_STENCIL8, "depth24_stencil8", GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL)
	};

	const std::vector<std::tuple<TextureType, std::string, unsigned int>> EnumUtil::m_TextureType =
	{
		std::make_tuple(TextureType::TEXTURE_1D, "1d", GL_TEXTURE_1D), 
		std::make_tuple(TextureType::TEXTURE_2D, "2d", GL_TEXTURE_2D), 
		std::make_tuple(TextureType::TEXTURE_2D_ARRAY, "2d_array", GL_TEXTURE_2D_ARRAY), 
		std::make_tuple(TextureType::TEXTURE_CUBE_MAP, "cube", GL_TEXTURE_CUBE_MAP)
	};

	const std::vector<std::tuple<FilterMode, unsigned int, std::string>> EnumUtil::m_FilterMode =
	{
		std::make_tuple(FilterMode::NEAREST, GL_NEAREST, "nearest"),
		std::make_tuple(FilterMode::LINEAR, GL_LINEAR, "linear"),
		std::make_tuple(FilterMode::NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_NEAREST, "nearest_mipmap_nearest"),
		std::make_tuple(FilterMode::NEAREST_MIPMAP_LINEAR, GL_NEAREST_MIPMAP_LINEAR, "nearest_mipmap_linear"),
		std::make_tuple(FilterMode::LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST, "linear_mipmap_nearest"),
		std::make_tuple(FilterMode::LINEAR_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR, "linear_mipmap_linear")
	};

	const std::vector<std::tuple<WrapMode, unsigned int, std::string>> EnumUtil::m_WrapMode =
	{
		std::make_tuple(WrapMode::REPEAT, GL_REPEAT, "repeat"),
		std::make_tuple(WrapMode::MIRRORED_REPEAT, GL_MIRRORED_REPEAT, "mirrored_repeat"),
		std::make_tuple(WrapMode::CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, "clamp_to_edge"),
		std::make_tuple(WrapMode::CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER, "clamp_to_border")
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
		std::make_pair(ShaderTexture::NORMAL, "normal")
	};

	const std::vector<unsigned int> EnumUtil::m_LineMode =
	{
		GL_LINES, 
		GL_LINE_LOOP, 
		GL_LINE_STRIP
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

	unsigned int EnumUtil::CompareModeToUint(CompareMode mode)
	{
		return std::get<1>(m_CompareMode[(int)mode]);
	}

	std::string EnumUtil::CompareModeToString(CompareMode mode)
	{
		return std::get<2>(m_CompareMode[(int)mode]);
	}

	CompareMode EnumUtil::CompareModeFromString(const std::string &name)
	{
		for (const auto &mode : m_CompareMode)
		{
			if (std::get<2>(mode) == name)
				return std::get<0>(mode);
		}
		return CompareMode::LESS;
	}

	unsigned int EnumUtil::BlendModeSrc(BlendMode mode)
	{
		return m_BlendModeSrc[(int)mode];
	}

	unsigned int EnumUtil::BlendModeDest(BlendMode mode)
	{
		return m_BlendModeDest[(int)mode];
	}

	unsigned int EnumUtil::BlendModeOp(BlendMode mode)
	{
		return m_BlendModeOp[(int)mode];
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

	std::pair<bool, unsigned int> EnumUtil::CullModeToUint(CullMode mode)
	{
		bool flag = mode != CullMode::NONE;
		unsigned int data = 0;

		if (mode == CullMode::BACK)
			data = GL_BACK;
		else if (mode == CullMode::FRONT)
			data = GL_FRONT;

		return std::make_pair(flag, data);
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
		else
			return DrawMode::QUAD;
	}

	std::pair<bool, unsigned int> EnumUtil::TextureFormatToUint(TextureFormat format, bool internalFormat)
	{
		auto data = m_TextureFormat[(int)format];
		return std::make_pair(std::get<0>(data) == TextureFormat::UNKNOW, 
			internalFormat ? std::get<2>(data) : std::get<3>(data));
	}

	std::string EnumUtil::TextureFormatToString(TextureFormat foramt)
	{
		auto data = m_TextureFormat[(int)foramt];
		return std::get<1>(data);
	}

	TextureFormat EnumUtil::TextureFormatFromString(const std::string &name)
	{
		for (const auto &format : m_TextureFormat)
		{
			if (std::get<1>(format) == name)
				return std::get<0>(format);
		}
		return TextureFormat::RGBA8;
	}

	unsigned int EnumUtil::TextureTypeToUnit(TextureType type)
	{
		auto data = m_TextureType[(int)type];
		return std::get<2>(data);
	}

	std::string EnumUtil::TextureTypeToString(TextureType type)
	{
		auto data = m_TextureType[(int)type];
		return std::get<1>(data);
	}

	TextureType EnumUtil::TextureTypeFromString(const std::string &name)
	{
		for (auto const &type : m_TextureType)
		{
			if (std::get<1>(type) == name)
				return std::get<0>(type);
		}
		return TextureType::TEXTURE_2D;
	}

	unsigned int EnumUtil::FilterModeToUint(FilterMode mode)
	{
		return std::get<1>(m_FilterMode[(int)mode]);
	}

	std::string EnumUtil::FilterModeToString(FilterMode mode)
	{
		return std::get<2>(m_FilterMode[(int)mode]);
	}

	FilterMode EnumUtil::FilterModeFromString(const std::string &name)
	{
		for (const auto &mode : m_FilterMode)
		{
			if (std::get<2>(mode) == name)
				return std::get<0>(mode);
		}
		return FilterMode::LINEAR;
	}

	unsigned int EnumUtil::WrapModeToUint(WrapMode mode)
	{
		return std::get<1>(m_WrapMode[(int)mode]);
	}

	std::string EnumUtil::WrapModeToString(WrapMode mode)
	{
		return std::get<2>(m_WrapMode[(int)mode]);
	}

	WrapMode EnumUtil::WrapModeFromString(const std::string &name)
	{
		for (const auto &mode : m_WrapMode)
		{
			if (std::get<2>(mode) == name)
				return std::get<0>(mode);
		}
		return WrapMode::CLAMP_TO_EDGE;
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

	unsigned int EnumUtil::LineModeToUnit(LineMode mode)
	{
		return m_LineMode[(unsigned int)mode];
	}
}