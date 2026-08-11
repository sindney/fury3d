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

	class Texture;

	namespace Editor
	{
#if WITH_EDITOR
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

		// Clickable texture row for component asset slots (Terrain layers,
		// sky textures): 48x48 thumbnail (black slot when empty), label +
		// current path; click opens the scene-texture picker. onPick gets
		// the chosen texture (nullptr for "Set to None").
		void FURY_API RenderLinkedTextureRow(const char* label, const char* pickerId,
			const std::string& currentPath, const std::shared_ptr<Texture>& current,
			std::function<void(std::shared_ptr<Texture>)> onPick);
#else
		inline void RenderAssetPickerModal(const char*, const char*,
			std::type_index, std::function<void(std::shared_ptr<void>)>) {}
		inline std::vector<std::shared_ptr<AnimationClip>> CollectAnimationClips() { return {}; }
#endif
	}
}

#endif // _FURY_EDITOR_ASSET_PICKER_H_
