#ifndef _FURY_ENUM_UTIL_H_
#define _FURY_ENUM_UTIL_H_

#include <vector>
#include <string>
#include <tuple>

#include "Macros.h"

#undef DELETE
#undef OPAQUE
#undef TRANSPARENT
#undef IN
#undef OUT

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

	// glTF alphaMode: OPAQUE solid, MASK alpha-tested, BLEND transparent. Material::m_Opaque = (mode != BLEND).
	enum class AlphaMode : unsigned int
	{
		OPAQUE = 0,
		MASK,
		BLEND
	};

	enum class TextureFormat : unsigned int
	{
		UNKNOW = 0,
		R8,
		R16,
		R16F,
		R32F,
		R32UI,
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
		SRGB,
		SRGB8,
		SRGB_ALPHA,
		SRGB8_ALPHA8,
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
		SKINNED_MESH,
		PARTICLE
	};

	enum class ShaderTexture : unsigned int
	{
		COLOR_ONLY = 0x0001,
		DIFFUSE = 0x0002,
		SPECULAR = 0x0004,
		NORMAL = 0x0008,
		// MASK material variant bit -- pipeline JSONs pair "alpha_test" texture flag with an ALPHA_TEST define so the discard branch compiles only where needed.
		ALPHA_TEST = 0x0010,
		// Shadow-receive variant bit ("shadow" + SHADOW define) -- set
		// per-draw when the light casts shadows, not by the material.
		SHADOW = 0x0020
	};

	enum class LineMode : unsigned int
	{
		LINES = 0,
		LINE_LOOP,
		LINE_STRIP
	};

	// Animation clip wrap mode. Named AnimWrapMode in C++ to avoid
	// colliding with the texture WrapMode above; exposed to Lua as
	// `WrapMode` (there is no Lua binding for the texture WrapMode).
	enum class AnimWrapMode : unsigned int
	{
		Default = 0,
		Once,
		Loop,
		ClampForever,
		PingPong
	};

	enum class PlayMode : unsigned int
	{
		StopSameLayer = 0,
		StopAll
	};

	// Where a postprocess effect sits in the chain. Chain order is
	// engine-owned (users toggle on/off only): PRE_TONEMAP effects
	// (SSAO/SSR -- gbuffer consumers) run on linear HDR scene color,
	// TONEMAP is the HDR->LDR pivot (ACES; auto-injected when HDR is
	// on, stripped when off), POST_TONEMAP display effects (FXAA/CRT)
	// run last on LDR. Sorted by (stage, order, name).
	enum class PostProcessStage : unsigned int
	{
		PRE_TONEMAP = 0,
		TONEMAP,
		POST_TONEMAP
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

		static const std::vector<std::pair<TextureFormat, unsigned int>> m_TextureFormatBitPerPixel;

		static const std::vector<std::tuple<TextureType, std::string, unsigned int>> m_TextureType;

		static const std::vector<std::tuple<FilterMode, unsigned int, std::string>> m_FilterMode;

		static const std::vector<std::tuple<WrapMode, unsigned int, std::string>> m_WrapMode;

		static const std::vector<std::pair<LightType, std::string>> m_LightType;

		static const std::vector<std::pair<ShaderType, std::string>> m_ShaderType;

		static const std::vector<std::pair<ShaderTexture, std::string>> m_ShaderTexture;

		static const std::vector<unsigned int> m_LineMode;

		static const std::vector<std::pair<AnimWrapMode, std::string>> m_AnimWrapMode;

		static const std::vector<std::pair<PlayMode, std::string>> m_PlayMode;

		static const std::vector<std::pair<AlphaMode, std::string>> m_AlphaMode;

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

		static unsigned int TextureBitPerPixel(TextureFormat format);


		static unsigned int TextureTypeToUnit(TextureType type);

		static std::string TextureTypeToString(TextureType type);

		static TextureType TextureTypeFromString(const std::string &name);


		static unsigned int FilterModeToUint(FilterMode mode);

		static std::string FilterModeToString(FilterMode mode);

		static FilterMode FilterModeFromString(const std::string &name);

		static FilterMode FilterModeFromUint(unsigned int value);


		static unsigned int WrapModeToUint(WrapMode mode);

		static std::string WrapModeToString(WrapMode mode);

		static WrapMode WrapModeFromString(const std::string &name);

		static WrapMode WrapModeFromUint(unsigned int value);


		static std::string LightTypeToString(LightType type);

		static LightType LightTypeFromString(const std::string &name);


		static std::string ShaderTypeToString(ShaderType type);

		static ShaderType ShaderTypeFromString(const std::string &name);


		static std::string ShaderTextureToString(ShaderTexture texture);

		static ShaderTexture ShaderTextureFromString(const std::string &name);

		static void GetShaderTextures(unsigned int flags, std::vector<ShaderTexture> &textures);


		static unsigned int LineModeToUnit(LineMode mode);

		static std::string AnimWrapModeToString(AnimWrapMode mode);

		static AnimWrapMode AnimWrapModeFromString(const std::string &name);

		static std::string PlayModeToString(PlayMode mode);

		static PlayMode PlayModeFromString(const std::string &name);

		static std::string PostProcessStageToString(PostProcessStage stage);

		static PostProcessStage PostProcessStageFromString(const std::string &name);

		static std::string AlphaModeToString(AlphaMode mode);

		// Case-insensitive; accepts glTF spellings ("OPAQUE") and
		// engine spellings ("opaque"). Unknown -> OPAQUE.
		static AlphaMode AlphaModeFromString(const std::string &name);
	};
}

#endif // _FURY_ENUM_UTIL_H_