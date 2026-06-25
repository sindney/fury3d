//-----------------------------------------------------------------------------
// USER IMPLEMENTATION
// Fury3d local customizations on top of the upstream ImGui imconfig.h.
//-----------------------------------------------------------------------------

#pragma once

#include "Fury/Macros.h"

#define IM_ASSERT(_EXPR)  ASSERT_MSG(_EXPR, "ImGui Assert!")

#if defined(_WIN32)

	#ifdef FURY_API_EXPORT
		#define IMGUI_API __declspec(dllexport)
	#else
		#define IMGUI_API __declspec(dllimport)
	#endif

#else

	#define IMGUI_API

#endif

// Keep backward-compatible function names available so the existing
// Gui::* forwarders (and any user scripts that call them) continue to
// resolve. Re-enable later once we've migrated callers off deprecated
// signatures.
// #define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
