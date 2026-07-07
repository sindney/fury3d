#ifndef _FURY_EDITOR_DEBUG_H_
#define _FURY_EDITOR_DEBUG_H_

#include "Fury/Macros.h"

namespace fury
{
	class Color;
}

#ifdef WITH_EDITOR

namespace fury
{
	// Deterministic LOD -> color palette used by the editor's LOD-debug
	// overlay (engine/Fury/Editor/EditorDebug.cpp). Indices past 5 wrap
	// modulo the table size, so chains longer than 6 levels still get a
	// distinct (cycled) tint per level.
	constexpr unsigned int kLodPaletteSize = 6;

	// Resolve a level index into the palette. Wraps for chains longer
	// than kLodPaletteSize levels. Always returns one of the declared
	// colors; never random or undefined.
	FURY_API Color GetLodDebugColor(unsigned int lodIndex);
}

#endif // WITH_EDITOR

#endif // _FURY_EDITOR_DEBUG_H_
