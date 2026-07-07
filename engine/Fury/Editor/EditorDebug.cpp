#ifdef WITH_EDITOR

#include "Fury/Editor/EditorDebug.h"
#include "Fury/Color.h"

namespace fury
{
	// Palette order matches the proposal/design:
	// LOD 0 = green, 1 = yellow, 2 = red, 3 = cyan, 4 = magenta, 5 = white.
	// Index 5 sits at the boundary so deeper LODs cycle back through the
	// same colors rather than going undefined.
	static const Color kLodColors[kLodPaletteSize] = {
		Color(0.0f, 1.0f, 0.0f, 1.0f), // 0: green
		Color(1.0f, 1.0f, 0.0f, 1.0f), // 1: yellow
		Color(1.0f, 0.0f, 0.0f, 1.0f), // 2: red
		Color(0.0f, 1.0f, 1.0f, 1.0f), // 3: cyan
		Color(1.0f, 0.0f, 1.0f, 1.0f), // 4: magenta
		Color(1.0f, 1.0f, 1.0f, 1.0f)  // 5: white (5+ wraps here)
	};

	Color GetLodDebugColor(unsigned int lodIndex)
	{
		return kLodColors[lodIndex % kLodPaletteSize];
	}
}

#endif // WITH_EDITOR
