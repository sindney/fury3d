#ifndef _FURY_GL_ENUM_MAP_H_
#define _FURY_GL_ENUM_MAP_H_

#include <utility>

#include "Fury/EnumUtil.h"
#include "Fury/Macros.h"

namespace fury
{
	// Engine enum -> GL constant mapping. OpenGLRHI-internal; engine code
	// speaks the canonical enums from EnumUtil.h.
	class FURY_API GLEnumMap final
	{
	public:

		static unsigned int CompareModeToGL(CompareMode mode);

		static unsigned int BlendModeSrcToGL(BlendMode mode);

		static unsigned int BlendModeDestToGL(BlendMode mode);

		static unsigned int BlendModeOpToGL(BlendMode mode);

		// first: cull enabled, second: GL face.
		static std::pair<bool, unsigned int> CullModeToGL(CullMode mode);

		// first: true when format is UNKNOW. internalFormat=false gives the base format.
		static std::pair<bool, unsigned int> TextureFormatToGL(TextureFormat format, bool internalFormat = true);

		static unsigned int TextureTypeToGL(TextureType type);

		static unsigned int FilterModeToGL(FilterMode mode);

		static unsigned int WrapModeToGL(WrapMode mode);

		static unsigned int LineModeToGL(LineMode mode);
	};
}

#endif // _FURY_GL_ENUM_MAP_H_
