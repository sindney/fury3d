#ifdef _FURY_GUI_IMP_

#ifndef _FURY_GUI_H_
#define _FURY_GUI_H_

#include <float.h>
#include <functional>
#include <memory>

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>

#include "Fury/Macros.h"

struct ImDrawData;

namespace fury
{
	namespace Gui
	{
		bool FURY_API Initialize(sf::Window *window, float scale = 1.0f, float fontScale = 1.0f);

		void FURY_API Shutdown();

		void FURY_API HandleEvent(sf::Event &event);

		void FURY_API NewFrame(float frameTime);

		void FURY_API ShowDefault(float dt);

		void FURY_API Render();

		// ImGui input-capture queries, exposed so Lua scripts can gate camera
		// motion when the cursor/keyboard is over an ImGui widget.
		bool FURY_API WantCaptureMouse();

		bool FURY_API WantCaptureKeyboard();

		// Minimal ImGui forwarders for Lua-driven panels and menus. The
		// return-the-new-value shape of SliderFloat / Checkbox sidesteps
		// Lua/C++ pointer marshalling — see openspec changes/
		// lua-input-bindings-flythrough-cam.
		// Begin a window with a close button. Returns (still_open, visible):
		//   still_open = false after the user clicks the X (caller should
		//                stop calling Begin until they want to reopen).
		//   visible    = whether the window body should be drawn this frame
		//                (false when the window is collapsed). The caller
		//                must always pair Begin with End regardless.
		std::pair<bool, bool> FURY_API Begin(const char* title, bool open);

		void FURY_API End();

		float FURY_API SliderFloat(const char* label, float current, float vmin, float vmax);

		bool FURY_API Checkbox(const char* label, bool current);

		bool FURY_API Button(const char* label);

		void FURY_API Separator();

		void FURY_API Text(const char* str);

		bool FURY_API BeginMenu(const char* label);

		void FURY_API EndMenu();

		bool FURY_API MenuItem(const char* label);

		// Returns the (possibly-edited) string. In/out shape matches
		// SliderFloat / Checkbox so Lua can write `s = Gui.InputText(label, s, max_len)`.
		std::string FURY_API InputText(const char* label, const std::string &current, int max_len);

		// Register an optional callback invoked inside the engine's main menu
		// bar. With WITH_EDITOR=OFF this is the only menu-bar consumer; with
		// WITH_EDITOR=ON the editor calls this between its built-in menus and
		// the engine's internals so script-emitted menus render between
		// `Window` and the trailing built-ins. Pass an empty std::function to
		// clear. The caller is responsible for clearing before any captured
		// Lua state is destroyed.
		void FURY_API SetMenuBarCallback(std::function<void()> cb);

		// Internal: invoke the menu-bar callback registered via
		// SetMenuBarCallback. The editor uses this to render the script
		// callback between its own built-in menus.
		void FURY_API InvokeMenuBarCallback();

		// Request that the engine window close. Safe to call multiple times:
		// the second invocation is a no-op when the window has already been
		// destroyed. Used by Lua scripts via the `Window.Close()` binding to
		// implement File -> Quit (the engine no longer owns the File menu).
		void FURY_API CloseWindow();
	}
}

#endif // _FURY_GUI_H_

#endif // _FURY_GUI_IMP_
