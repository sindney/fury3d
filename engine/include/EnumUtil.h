#ifndef _FURY_ENUM_UTIL_H_
#define _FURY_ENUM_UTIL_H_

#include <vector>
#include <string>
#include <tuple>

#include "Singleton.h"

namespace fury
{
	enum class CompareMode : int
	{
		LESS = 0,
		GREATER,
		EQUAL,
		ALWAYS,
		LEQUAL,
		GEQUAL,
		NOTEQUAL
	};

	enum class BlendMode : int
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

	enum class TextureFormat : int
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

	enum class FilterMode : int 
	{
		NEAREST = 0,
		LINEAR,
		NEAREST_MIPMAP_NEAREST,
		NEAREST_MIPMAP_LINEAR,
		LINEAR_MIPMAP_NEAREST,
		LINEAR_MIPMAP_LINEAR
	};

	enum class WrapMode : int
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

	enum class LightType : int
	{
		DIRECTIONAL = 0, 
		POINT,
		SPOT
	};

	enum class DrawMode
	{
		RENDERABLE, 
		LIGHT, 
		QUAD
	};
	
	enum class ShaderType : int
	{
		OTHER = 0, 
		DIFFUSE_TEXTURE, 
		COLOR_ONLY, 
		POINT_LIGHT, 
		SPOT_LIGHT, 
		DIR_LIGHT, 
		POST_EFFECT
	};

	class FURY_API EnumUtil : public Singleton <EnumUtil>
	{
	public:

		typedef std::shared_ptr<EnumUtil> Ptr;

	private:

		static const std::vector<std::tuple<CompareMode, unsigned int, std::string>> m_CompareMode;

		static const std::vector<std::pair<BlendMode, std::string>> m_BlendMode;

		static const std::vector<unsigned int> m_BlendModeSrc;

		static const std::vector<unsigned int> m_BlendModeDest;

		static const std::vector<unsigned int> m_BlendModeOp;

		static const std::vector<std::tuple<TextureFormat, std::string, unsigned int, unsigned int>> m_TextureFormat;

		static const std::vector<std::tuple<FilterMode, unsigned int, std::string>> m_FilterMode;

		static const std::vector<std::tuple<WrapMode, unsigned int, std::string>> m_WrapMode;

		static const std::vector<std::pair<ShaderType, std::string>> m_ShaderType;

	public:

		unsigned int CompareModeToUint(CompareMode mode);

		std::string CompareModeToString(CompareMode mode);

		CompareMode CompareModeFromString(const std::string &name);


		unsigned int BlendModeSrc(BlendMode mode);

		unsigned int BlendModeDest(BlendMode mode);

		unsigned int BlendModeOp(BlendMode mode);

		std::string BlendModeToString(BlendMode mode);

		BlendMode BlendModeFromString(const std::string &name);


		std::pair<bool, unsigned int> CullModeToUint(CullMode mode);

		std::string CullModeToString(CullMode mode);

		CullMode CullModeFromString(const std::string &name);


		std::pair<bool, unsigned int> TextureFormatToUint(TextureFormat format, bool internalFormat = true);

		std::string TextureFormatToString(TextureFormat foramt);

		TextureFormat TextureFormatFromString(const std::string &name);


		unsigned int FilterModeToUint(FilterMode mode);

		std::string FilterModeToString(FilterMode mode);

		FilterMode FilterModeFromString(const std::string &name);


		unsigned int WrapModeToUint(WrapMode mode);

		std::string WrapModeToString(WrapMode mode);

		WrapMode WrapModeFromString(const std::string &name);


		std::string DrawModeToString(DrawMode mode);

		DrawMode DrawModeFromString(const std::string &name);


		std::string ShaderTypeToString(ShaderType type);

		ShaderType ShaderTypeFromString(const std::string &name);

	};
}

#endif // _FURY_ENUM_UTIL_H_