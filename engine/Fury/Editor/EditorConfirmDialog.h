#ifndef _FURY_EDITOR_CONFIRM_DIALOG_H_
#define _FURY_EDITOR_CONFIRM_DIALOG_H_

#include <functional>
#include <string>

#include "Fury/Macros.h"

namespace fury
{
	namespace Editor
	{
#ifdef WITH_EDITOR
		// Queue a Yes/No confirm dialog. On the next editor tick (if no
		// dialog is currently open), this opens a BeginPopupModal titled
		// `title`, renders `message` as body text, and presents Yes / No
		// buttons. Clicking either button closes the modal and invokes
		// `onResult` exactly once (true for Yes, false for No). Esc /
		// click-outside is treated as No. Subsequent requests queue and
		// open when the previous one closes.
		//
		// This helper is intended for non-trivial confirms (asset
		// deletion with in-use warning, discard-unsaved-changes prompts).
		// Trivial 2-3 line inline confirms MAY continue to use
		// ImGui::OpenPopup inline without going through this helper.
		void FURY_API RequestConfirmDialog(const std::string& title,
			const std::string& message,
			std::function<void(bool)> onResult);

		// Renders the currently-open confirm dialog (if any). Called
		// from Editor::Tick every frame, next to the other window renders.
		void FURY_API RenderConfirmDialog();
#else
		inline void RequestConfirmDialog(const std::string&, const std::string&,
			std::function<void(bool)>) {}
		inline void RenderConfirmDialog() {}
#endif
	}
}

#endif // _FURY_EDITOR_CONFIRM_DIALOG_H_
