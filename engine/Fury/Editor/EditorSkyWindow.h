#ifndef _FURY_EDITOR_SKY_WINDOW_H_
#define _FURY_EDITOR_SKY_WINDOW_H_

#include <memory>

#include "Fury/Macros.h"

namespace fury
{
	class SceneNode;

	namespace Editor
	{
#if WITH_EDITOR
		// Open the per-node SkyAtmosphere editor window (idempotent - a
		// second Open call for the same node focuses the existing window).
		void FURY_API OpenSkyEditor(const std::shared_ptr<SceneNode> &node);

		// Render every open sky editor window. Called from
		// RenderAllOpenAssetEditors (EditorAssetWindows.cpp).
		void FURY_API RenderAllOpenSkyEditors();
#else
		inline void OpenSkyEditor(const std::shared_ptr<SceneNode> &) {}
		inline void RenderAllOpenSkyEditors() {}
#endif
	}
}

#endif // _FURY_EDITOR_SKY_WINDOW_H_
