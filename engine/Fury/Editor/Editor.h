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

		// Drive the editor's post-render work: the viewport-picking
		// state machine. Called from Engine.cpp once per frame, AFTER
		// the user pipeline (Pipeline::Active->Execute) has run and
		// BEFORE Gui::Render flushes ImGui draws to the back buffer.
		void FURY_API TickPostRender();

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

		// ----- gizmo controls --------------------------------------------
		// Set the active TRS gizmo mode. Valid name values: "translate",
		// "rotate", "scale". Unknown names are silently ignored. The
		// change applies on the next frame.
		void FURY_API SetGizmoMode(const char* name);

		// Set the gizmo's reference space. Valid name values: "local",
		// "world". Unknown names are silently ignored. SCALE always
		// operates in local space regardless of this setting.
		void FURY_API SetGizmoSpace(const char* name);

		void FURY_API SetSnapEnabled(bool enabled);

		const char* FURY_API GetGizmoMode();

		const char* FURY_API GetGizmoSpace();

		bool FURY_API GetSnapEnabled();

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
			std::function<void(const std::string&)> on_save;
			std::function<void(const std::string&)> on_save_as;
			std::function<std::string()> scene_dir;
		};

		void FURY_API SetSceneIO(SceneIO io);

		void FURY_API ClearSceneIO();

		// Returns the resolved scene directory: SceneIO.scene_dir() if set,
		// otherwise the absolute path to "Resource/Scene/".
		std::string FURY_API GetSceneDir();

		// Track which file the user is currently editing so File → Save can
		// route to in-place save (native .json/.bin) or fall through to Save
		// As (non-native: .gltf/.glb/.fbx, or no path tracked).
		void FURY_API SetCurrentScene(const std::string& path, bool is_native);

		void FURY_API ClearCurrentScene();

		std::string FURY_API GetCurrentScenePath();

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
		inline void TickPostRender() {}
		inline void Shutdown() {}
		inline SceneNode* GetSelectedSceneNode() { return nullptr; }
		inline void SetWindowVisible(const char*, bool) {}
		inline bool GetWindowVisible(const char*) { return false; }
		inline void SetGizmoMode(const char*) {}
		inline void SetGizmoSpace(const char*) {}
		inline void SetSnapEnabled(bool) {}
		inline const char* GetGizmoMode() { return "translate"; }
		inline const char* GetGizmoSpace() { return "world"; }
		inline bool GetSnapEnabled() { return false; }
		inline void SetImportFlag(const char*, bool) {}
		inline bool GetImportFlag(const char*, bool d = false) { return d; }
		inline void Log(const char*, const char*) {}
		inline void SetCurrentScene(const std::string&, bool) {}
		inline void ClearCurrentScene() {}
		inline std::string GetCurrentScenePath() { return {}; }
#endif
	}
}

#endif // _FURY_EDITOR_H_
