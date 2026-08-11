#ifndef _FURY_EDITOR_TERRAIN_WINDOW_H_
#define _FURY_EDITOR_TERRAIN_WINDOW_H_

#include <memory>

#include "Fury/Macros.h"

namespace fury
{
	class SceneNode;

	namespace Editor
	{
#if WITH_EDITOR
		// Open the per-node Terrain editor window (idempotent - a second
		// Open call for the same node focuses the existing window).
		void FURY_API OpenTerrainEditor(const std::shared_ptr<SceneNode> &node);

		// Render every open Terrain editor window. Called from
		// RenderAllOpenAssetEditors (EditorAssetWindows.cpp).
		void FURY_API RenderAllOpenTerrainEditors();
#else
		inline void OpenTerrainEditor(const std::shared_ptr<SceneNode> &) {}
		inline void RenderAllOpenTerrainEditors() {}
#endif
	}
}

#endif // _FURY_EDITOR_TERRAIN_WINDOW_H_
