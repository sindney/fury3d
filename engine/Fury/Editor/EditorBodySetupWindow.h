#ifndef _FURY_EDITOR_BODY_SETUP_WINDOW_H_
#define _FURY_EDITOR_BODY_SETUP_WINDOW_H_

#include <memory>

#include "Fury/Macros.h"

namespace fury
{
	class SceneNode;

	namespace Editor
	{
#ifdef WITH_EDITOR
		// Open the per-node BodySetup editor window (idempotent - a
		// second Open call for the same node focuses the existing
		// window rather than opening a duplicate).
		void FURY_API OpenBodySetupEditor(const std::shared_ptr<SceneNode> &node);

		// Render every open BodySetup editor window. Called from
		// RenderAllOpenAssetEditors (EditorAssetWindows.cpp).
		void FURY_API RenderAllOpenBodySetupEditors();
#else
		inline void OpenBodySetupEditor(const std::shared_ptr<SceneNode> &) {}
		inline void RenderAllOpenBodySetupEditors() {}
#endif
	}
}

#endif // _FURY_EDITOR_BODY_SETUP_WINDOW_H_
