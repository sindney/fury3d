#ifndef _FURY_LUA_BINDINGS_H_
#define _FURY_LUA_BINDINGS_H_

#include <string>

#include <sol/forward.hpp>

namespace fury
{
	namespace LuaBindings
	{
		// Launcher-side overrides for fields of EngineOptions that the launcher
		// owns (set from CLI flags). The Engine.run Lua binding layers these
		// over whatever the script supplies in its options table. Empty-string
		// `screenshot_path` (the default) means no override.
		struct LauncherEngineOptions
		{
			std::string screenshot_path;
			int screenshot_frame = 2;
			// Temporal capture: --screenshot-series "path,N,interval" captures N
			// back-buffer frames every `interval` frames (starting at
			// screenshot_frame) and writes ONE contact-sheet atlas PNG. For
			// diagnosing temporal artifacts (flicker, stepping, popping).
			std::string screenshot_series_path;
			int series_count = 0;
			int series_interval = 0;

			// Auto-answer any Editor.RequestConfirmDialog with the default
			// (affirmative) selection, so modal prompts (import unit-scale
			// etc.) don't block headless automation.
			bool auto_confirm = false;
			// Frame the camera on the opened scene's content (whole-scene
			// bounds) after load -- same math as the editor's
			// frame-selection, applied automatically.
			bool auto_focus = false;
			// Sentinel `-1` means the engine never wrote a result. Engine::Run
			// writes 0 (capture success) or 1 (capture failure) once a capture
			// is requested via screenshot_path.
			int exit_code = -1;
		};

		// Set the launcher options before invoking the user's Lua script. The
		// Engine.run Lua binding reads them and writes back to `exit_code` once
		// the engine loop returns.
		void SetLauncherOptions(LauncherEngineOptions *options);

		// Register all Demo-driven engine types and free functions on the given
		// Lua state. Idempotent (sol2 overwrites existing usertypes on re-register
		// but we don't rely on this -- call once per state).
		void Register(sol::state_view lua);
	}
}

#endif // _FURY_LUA_BINDINGS_H_
