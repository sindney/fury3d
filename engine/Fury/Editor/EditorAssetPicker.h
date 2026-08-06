#ifndef _FURY_EDITOR_ASSET_PICKER_H_
#define _FURY_EDITOR_ASSET_PICKER_H_

#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include "Fury/Macros.h"

namespace fury
{
	class AnimationClip;

	namespace Editor
	{
#ifdef WITH_EDITOR
		// Render a modal asset picker. Opens a BeginPopupModal with popup
		// ID `popup_id` and title `title`, listing every asset of the
		// requested `type` (Mesh / Material / Texture / AnimationClip)
		// registered in the active Scene's EntityManager as a Selectable
		// row. Single-click selects (highlights); double-click or `OK`
		// confirms and invokes `onPick` with the chosen asset (as
		// shared_ptr<void>), then closes the popup. `Cancel` closes
		// without invoking. Mirrors the RenderOpenImportModal pattern
		// (Editor.cpp:373-438).
		//
		// The caller is responsible for opening the popup (e.g.
		// `ImGui::OpenPopup("MeshPicker")` from a Browse... button handler)
		// before calling this function. Per-popup-id selection state is
		// kept internally so multiple pickers (mesh / per-slot material
		// / texture / clip) can be open simultaneously without aliasing.
		void FURY_API RenderAssetPickerModal(const char* popup_id,
			const char* title, std::type_index type,
			std::function<void(std::shared_ptr<void>)> onPick);

		// Enumerate every AnimationClip registered in the active Scene's
		// EntityManager. Shared between the asset-picker modal and the
		// Animation window's clip sidebar so the collection logic lives
		// in one place.
		std::vector<std::shared_ptr<AnimationClip>> FURY_API CollectAnimationClips();
#else
		inline void RenderAssetPickerModal(const char*, const char*,
			std::type_index, std::function<void(std::shared_ptr<void>)>) {}
		inline std::vector<std::shared_ptr<AnimationClip>> CollectAnimationClips() { return {}; }
#endif
	}
}

#endif // _FURY_EDITOR_ASSET_PICKER_H_
