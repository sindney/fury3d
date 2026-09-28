#ifndef _FURY_EDITOR_SCENE_NODE_PICKER_H_
#define _FURY_EDITOR_SCENE_NODE_PICKER_H_

#include <functional>
#include <memory>
#include <typeindex>

#include "Fury/Macros.h"

namespace fury
{
	class SceneNode;

	namespace Editor
	{
#if WITH_EDITOR
		// Render a modal SceneNode picker. Opens a BeginPopupModal with popup
		// ID `popup_id` and title `title`, walking the active scene's root and
		// listing every node whose own `GetComponent(requiredComponent)` is
		// non-null (strict self-match, root excluded). Pass `typeid(void)` to
		// skip the component filter. Single-click selects; Enter / `OK`
		// invokes `onPick` with the chosen node (nullptr on Cancel or `Set to
		// None`); Esc / `Cancel` / `X` closes without invoking.
		//
		// The caller is responsible for opening the popup
		// (`ImGui::OpenPopup("PickSkySunNode")` from a button handler) before
		// calling this function. Per-popup-id state is kept internally so
		// multiple pickers (sun / ocean / camera / etc.) can be open
		// simultaneously without aliasing. **Popup IDs MUST be globally
		// unique** -- the per-popup-id state map would alias if two pickers
		// shared an id.
		void FURY_API RenderSceneNodePickerModal(const char* popup_id,
			const char* title,
			std::type_index requiredComponent,
			std::function<void(std::shared_ptr<SceneNode>)> onPick);

		// No-filter overload: every non-root node is eligible.
		void FURY_API RenderSceneNodePickerModal(const char* popup_id,
			const char* title,
			std::function<void(std::shared_ptr<SceneNode>)> onPick);
#else
		inline void RenderSceneNodePickerModal(const char*, const char*,
			std::type_index,
			std::function<void(std::shared_ptr<SceneNode>)>) {}
		inline void RenderSceneNodePickerModal(const char*, const char*,
			std::function<void(std::shared_ptr<SceneNode>)>) {}
#endif
	}
}

#endif // _FURY_EDITOR_SCENE_NODE_PICKER_H_
