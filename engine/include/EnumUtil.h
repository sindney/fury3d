#ifndef _FURY_ENUM_UTIL_H_
#define _FURY_ENUM_UTIL_H_

#include <vector>
#include <string>
#include <tuple>

#include "Macros.h"

#undef DELETE
#undef OPAQUE
#undef TRANSPARENT

namespace fury
{
	enum class ClearMode : unsigned int
	{
		NONE = 0, 
		COLOR, 
		STENCIL, 
		DEPTH, 
		COLOR_DEPTH, 
		COLOR_STENCIL, 
		STENCIL_DEPTH, 
		COLOR_DEPTH_STENCIL
	};

	enum class CompareMode : unsigned int
	{
		LESS = 0,
		GREATER,
		EQUAL,
		ALWAYS,
		LEQUAL,
		GEQUAL,
		NOTEQUAL
	};

	enum class BlendMode : unsigned int
	{
		REPLACE = 0,
		ADD,
		MULTIPLY,
		ALPHA,
		ADDALPHA,
		PREMULALPHA,
		INVDESTALPHA,
		SUBTRACT,
		SUBTRACTALPHA
	};

	enum class CullMode
	{
		FRONT,
		BACK,
		NONE
	};

	enum class DrawMode
	{
		OPAQUE,
		TRANSPARENT,
		LIGHT,
		QUAD
	};

	enum class TextureFormat : unsigned int
	{
		UNKNOW = 0, 
		R8,
		R16,
		R16F,
		R32F,
		RG8,
		RG16,
		RG16F,
		RG32F,
		RGB8,
		RGB16,
		RGB16F,
		RGB32F,
		RGBA8,
		RGBA16,
		RGBA16F,
		RGBA32F,
		DEPTH16,
		DEPTH24,
		DEPTH32F,
		DEPTH32F_STENCIL8,
		DEPTH24_STENCIL8
	};

	enum class TextureType : unsigned int
	{
		TEXTURE_1D = 0, 
		TEXTURE_2D, 
		TEXTURE_2D_ARRAY, 
		TEXTURE_CUBE_MAP
	};

	enum class FilterMode : unsigned int
	{
		NEAREST = 0,
		LINEAR,
		NEAREST_MIPMAP_NEAREST,
		NEAREST_MIPMAP_LINEAR,
		LINEAR_MIPMAP_NEAREST,
		LINEAR_MIPMAP_LINEAR
	};

	enum class WrapMode : unsigned int
	{
		REPEAT = 0,
		MIRRORED_REPEAT,
		CLAMP_TO_EDGE,
		CLAMP_TO_BORDER
	};

	enum class Side
	{
		IN,
		OUT,
		STRADDLE
	};

	enum class LightType : unsigned int
	{
		DIRECTIONAL = 0, 
		POINT,
		SPOT
	};
	
	enum class ShaderType : unsigned int
	{
		OTHER = 0, 
		STATIC_MESH, 
		SKINNED_MESH
	};

	enum class ShaderTexture : unsigned int
	{
		COLOR_ONLY	= 0x0001, 
		DIFFUSE		= 0x0002, 
		SPECULAR	= 0x0004, 
		NORMAL		= 0x0008
	};

	enum class LineMode : unsigned int
	{
		LINES = 0, 
		LINE_LOOP, 
		LINE_STRIP
	};

	class FURY_API EnumUtil final
	{
	private:

		static const std::vector<std::pair<ClearMode, std::string>> m_ClearMode;

		static const std::vector<std::tuple<CompareMode, unsigned int, std::string>> m_CompareMode;

		static const std::vector<std::pair<BlendMode, std::string>> m_BlendMode;

		static const std::vector<unsigned int> m_BlendModeSrc;

		static const std::vector<unsigned int> m_BlendModeDest;

		static const std::vector<unsigned int> m_BlendModeOp;

		static const std::vector<std::tuple<TextureFormat, std::string, unsigned int, unsigned int>> m_TextureFormat;

		static const std::vector < std::tuple < TextureType, std::string, unsigned int >> m_TextureType;

		static const std::vector<std::tuple<FilterMode, unsigned int, std::string>> m_FilterMode;

		static const std::vector<std::tuple<WrapMode, unsigned int, std::string>> m_WrapMode;

		static const std::vector<std::pair<ShaderType, std::string>> m_ShaderType;

		static const std::vector<std::pair<ShaderTexture, std::string>> m_ShaderTexture;

		static const std::vector<unsigned int> m_LineMode;

	public:

		static std::string ClearModeToString(ClearMode mode);

		static ClearMode ClearModeFromString(const std::string &name);


		static unsigned int CompareModeToUint(CompareMode mode);

		static std::string CompareModeToString(CompareMode mode);

		static CompareMode CompareModeFromString(const std::string &name);


		static unsigned int BlendModeSrc(BlendMode mode);

		static unsigned int BlendModeDest(BlendMode mode);

		static unsigned int BlendModeOp(BlendMode mode);

		static std::string BlendModeToString(BlendMode mode);

		static BlendMode BlendModeFromString(const std::string &name);


		static std::pair<bool, unsigned int> CullModeToUint(CullMode mode);

		static std::string CullModeToString(CullMode mode);

		static CullMode CullModeFromString(const std::string &name);


		static std::string DrawModeToString(DrawMode mode);

		static DrawMode DrawModeFromString(const std::string &name);


		static std::pair<bool, unsigned int> TextureFormatToUint(TextureFormat format, bool internalFormat = true);

		static std::string TextureFormatToString(TextureFormat foramt);

		static TextureFormat TextureFormatFromString(const std::string &name);


		static unsigned int TextureTypeToUnit(TextureType type);

		static std::string TextureTypeToString(TextureType type);

		static TextureType TextureTypeFromString(const std::string &name);


		static unsigned int FilterModeToUint(FilterMode mode);

		static std::string FilterModeToString(FilterMode mode);

		static FilterMode FilterModeFromString(const std::string &name);


		static unsigned int WrapModeToUint(WrapMode mode);

		static std::string WrapModeToString(WrapMode mode);

		static WrapMode WrapModeFromString(const std::string &name);


		static std::string ShaderTypeToString(ShaderType type);

		static ShaderType ShaderTypeFromString(const std::string &name);


		static std::string ShaderTextureToString(ShaderTexture texture);

		static ShaderTexture ShaderTextureFromString(const std::string &name);

		static void GetShaderTextures(unsigned int flags, std::vector<ShaderTexture> &textures);


		static unsigned int LineModeToUnit(LineMode mode);
	};
}

#endif // _FURY_ENUM_UTIL_H_