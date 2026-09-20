#ifndef _FURY_EDITOR_PACKAGE_DIALOG_H_
#define _FURY_EDITOR_PACKAGE_DIALOG_H_

#include <string>

#include "Fury/Macros.h"

namespace fury
{
	namespace Editor
	{
#if WITH_EDITOR
		namespace EditorPackageDialog
		{
			// Queue the Package dialog for `scenePath` (a scene with a saved
			// on-disk path). The modal opens on the next Draw call: a
			// settings view (compression / texture target / output folder /
			// verbose), then a progress view that spawns <exe
			// dir>/furye-cli package as a subprocess and streams its
			// combined stdout+stderr into a log.
			void FURY_API Open(const std::string& scenePath);

			// Renders the dialog when open, no-op otherwise. Called every
			// frame from Editor::Tick, next to RenderConfirmDialog.
			void FURY_API Draw();

			// True between Open and the user closing the dialog.
			bool FURY_API IsOpen();
		}
#else
		namespace EditorPackageDialog
		{
			inline void Open(const std::string&) {}
			inline void Draw() {}
			inline bool IsOpen() { return false; }
		}
#endif
	}
}

#endif // _FURY_EDITOR_PACKAGE_DIALOG_H_
