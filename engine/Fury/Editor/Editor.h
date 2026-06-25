#ifndef _FURY_EDITOR_H_
#define _FURY_EDITOR_H_

#include <functional>
#include <string>
#include <vector>

#include "Fury/Macros.h"

namespace fury
{
	class SceneNode;

	namespace Editor
	{
#ifdef WITH_EDITOR
		// ----- lifecycle ---------------------------------------------------
		void FURY_API Initialize();

		// Drive one frame of the editor: menu bar, dockspace, registered
		// windows. Must run AFTER Gui::NewFrame and BEFORE Gui::Render.
		void FURY_API Tick();

		void FURY_API Shutdown();

		// ----- selection / visibility -------------------------------------
		// Returns the SceneNode currently selected in the Scene Inspector,
		// or nullptr if none / the selection has been invalidated.
		SceneNode* FURY_API GetSelectedSceneNode();

		// Programmatic show/hide of built-in editor windows. Valid names:
		// "Profiler", "SceneInspector", "Console", "ContentBrowser",
		// "Settings". Unknown names are silently ignored.
		void FURY_API SetWindowVisible(const char* name, bool visible);

		bool FURY_API GetWindowVisible(const char* name);

		// ----- import flags -----------------------------------------------
		// Project-supplied import flag (e.g. "auto_default_sun") backing the
		// Settings → Import section. Editor.lua reads these in import_scene
		// to honor the user's toggle.
		void FURY_API SetImportFlag(const char* name, bool value);

		bool FURY_API GetImportFlag(const char* name, bool default_value = false);

		// ----- logging ----------------------------------------------------
		// Push a line into the Console log view. level is one of "info",
		// "warn", "error", "debug" (anything else is treated as info).
		void FURY_API Log(const char* level, const char* text);

		// ----- Lua extension hooks ----------------------------------------
		struct SceneIO
		{
			std::function<std::vector<std::string>()> list_files;
			std::function<void()> on_new;
			std::function<void(const std::string&)> on_open;
			std::function<void(const std::string&)> on_import;
			std::function<void(const std::string&)> on_save_as;
			std::function<std::string()> scene_dir;
		};

		void FURY_API SetSceneIO(SceneIO io);

		void FURY_API ClearSceneIO();

		// Returns the resolved scene directory: SceneIO.scene_dir() if set,
		// otherwise the absolute path to "Resource/Scene/".
		std::string FURY_API GetSceneDir();

		struct TreeNode
		{
			std::string name;
			SceneNode* node = nullptr;
			std::vector<TreeNode> children;
		};

		using SceneTreeProvider = std::function<TreeNode()>;
		void FURY_API SetSceneTreeProvider(SceneTreeProvider p);
		void FURY_API ClearSceneTreeProvider();

		using CommandHandler = std::function<void(const std::string&)>;
		void FURY_API SetCommandHandler(CommandHandler h);
		void FURY_API ClearCommandHandler();

		struct CameraControl
		{
			std::string label;
			std::string kind;          // "slider" or "checkbox"
			std::function<float()> get_f;
			std::function<void(float)> set_f;
			std::function<bool()> get_b;
			std::function<void(bool)> set_b;
			float vmin = 0.0f;
			float vmax = 1.0f;
		};

		void FURY_API SetCameraControls(std::vector<CameraControl> controls);
		void FURY_API ClearCameraControls();

#else
		// Stubs so call sites compile cleanly with WITH_EDITOR=OFF. The
		// linker has nothing to resolve.
		inline void Initialize() {}
		inline void Tick() {}
		inline void Shutdown() {}
		inline SceneNode* GetSelectedSceneNode() { return nullptr; }
		inline void SetWindowVisible(const char*, bool) {}
		inline bool GetWindowVisible(const char*) { return false; }
		inline void SetImportFlag(const char*, bool) {}
		inline bool GetImportFlag(const char*, bool d = false) { return d; }
		inline void Log(const char*, const char*) {}
#endif
	}
}

#endif // _FURY_EDITOR_H_
