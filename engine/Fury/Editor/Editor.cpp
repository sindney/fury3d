#if WITH_EDITOR

#include "Fury/Editor/Editor.h"

#include "Fury/Editor/EditorAssetWindows.h"
#include "Fury/Editor/EditorConfirmDialog.h"
#include "Fury/Editor/EditorLog.h"
#include "Fury/Editor/EditorPicking.hpp"
#include "Fury/Editor/EditorThemes.h"
#include "Fury/Engine.h"
#include "Fury/FileUtil.h"
#include "Fury/Gui.h"
#include "Fury/Log.h"
#include "Fury/MeshRender.h"
#include "Fury/Pipeline.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

#include <SFML/Window.hpp>

// imgui.h manages its own pragma push/pop (lines 135/4506) -- don't wrap,
// an outer pop would consume the inner push and trigger C4193.
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGuizmo.h"
#include "nfd.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
	#include <windows.h>
#else
	#include <spawn.h>
	#include <signal.h>
	extern char **environ;
#endif

namespace fury {
namespace Editor {
// Forward declarations of window-rendering functions (defined in
// EditorWindows.cpp / EditorNodeProperties.cpp).
void RenderSettingsWindow(bool* open);
void RenderProfilerWindow(bool* open);
void RenderSceneInspectorWindow(bool* open);
void RenderConsoleWindow(bool* open);
void RenderContentBrowserWindow(bool* open);
void RenderNodePropertiesWindow(bool* open);
void RenderViewportWindow(bool* open);
void RenderAnimationWindow(bool* open);

// Defined in EditorGizmo.cpp.
void RenderGizmo(const ImVec2& central_rect_min, const ImVec2& central_rect_size);

// Defined in EditorSelectionViz.cpp.
void DrawSelectionOverlay();

// Gizmo state owned by EditorGizmo.cpp; read here for the imgui.ini
// persistence handler. Using extern (rather than going through the
// public string-keyed setters) keeps the Write/Read path symmetric
// with how Theme=N is persisted.
extern ImGuizmo::OPERATION g_GizmoOp;
extern ImGuizmo::MODE g_GizmoSpace;
extern bool g_SnapEnabled;
extern float g_SnapTranslate;
extern float g_SnapRotate;
extern float g_SnapScale;
extern bool g_ShowGrid;
// Defined in EditorWindows.cpp next to g_ShowGrid; persisted Tracy toggle.
extern bool g_TracyEnabled;

// Public-ish accessors used by the window rendering code, kept in
// this TU so we don't multiply globals.
extern SceneIO g_SceneIO;
extern SceneTreeProvider g_TreeProvider;
extern CommandHandler g_CommandHandler;
extern std::vector<CameraControl> g_CameraControls;
extern std::unordered_map<std::string, bool> g_ImportFlags;
extern SceneNode* g_SelectedSceneNode;
extern bool g_ShowSettings;
extern bool g_ShowProfiler;
extern bool g_ShowSceneInspector;
extern bool g_ShowNodeProperties;
extern bool g_ShowConsole;
extern bool g_ShowContentBrowser;
extern bool g_ShowViewport;
extern bool g_ShowAnimation;
extern ImVec2 g_ViewportContentMin;
extern ImVec2 g_ViewportContentSize;
extern bool g_ViewportHovered;
extern bool g_ViewportVisible;
extern std::string g_CurrentScenePath;
extern bool g_CurrentSceneIsNative;

// Defined in EditorWindows.cpp -- set by SelectAssetInBrowser,
// read by RenderContentBrowserWindow.
extern std::optional<std::pair<std::type_index, std::string>> g_SelectedAsset;
extern std::optional<std::string> g_PendingScrollToAsset;

// Per-TU storage. Defined here, declared as extern above so
// EditorWindows.cpp can read them without a header dependency.
SceneIO g_SceneIO;
SceneTreeProvider g_TreeProvider;
CommandHandler g_CommandHandler;
std::vector<CameraControl> g_CameraControls;
// Frame-selection handler registered from Lua (Editor.lua sets this so
// the Scene Inspector's leaf-double-click can drive the Lua-owned
// editor camera). Stored as a file-static so the public
// SetFrameSelectionHandler / FrameSelection entry points can read it
// without exposing it on the Editor namespace.
std::function<void(SceneNode*)> g_FrameSelectionHandler;
std::unordered_map<std::string, bool> g_ImportFlags;
SceneNode* g_SelectedSceneNode = nullptr; // cached anchor of g_SelectionSet
SceneNodeSet g_SelectionSet;              // full multi-node selection set

// Per-node Scene Inspector tree open/closed state. Members are
// SceneNode* (raw pointers -- the trigger funcs clear the set on
// scene swap so dangling pointers don't leak across scenes).
std::unordered_set<SceneNode*> g_OpenedNodes;

bool g_ShowSettings = false;
// Hidden by default -- opened on demand via the Window menu. (It was
// briefly default-ON for dock persistence; the [FuryEditor] ini
// handler now persists visibility either way, so the default layout
// stays clean.)
bool g_ShowProfiler = false; // hidden by default -- opened on demand
bool g_ShowSceneInspector = true; // visible by default -- docked left
bool g_ShowNodeProperties = true; // visible by default -- docked right
bool g_ShowConsole = true;		  // visible by default -- bottom dock
bool g_ShowContentBrowser = true; // visible by default -- bottom dock
bool g_ShowViewport = true;		  // visible by default -- docked center
bool g_ShowAnimation = false;	  // hidden by default -- opened on demand
// Captured each frame by RenderViewportWindow so the gizmo and
// picking operate in viewport-content-rect space (not full-window
// space). When the Viewport window is hidden/collapsed, size is
// zero and g_ViewportVisible is false -- gizmo + picking no-op.
ImVec2 g_ViewportContentMin(0, 0);
ImVec2 g_ViewportContentSize(0, 0);
bool g_ViewportHovered = false;
bool g_ViewportVisible = false;
bool g_SceneDirty = false; // set on inspector mutation, cleared on save
std::string g_CurrentScenePath;
bool g_CurrentSceneIsNative = false;

namespace {
bool s_FirstFrame = true;
bool s_RequestRebuildLayout = false;
ImGuiID s_DockspaceID = 0;

// Did imgui.ini exist at startup? Determines whether we should
// run BuildDefaultLayout on the first frame.
bool s_HadIniOnStartup = false;

// Layout-version migration. The imgui.ini stores `Layout=N`
// under [FuryEditor][Editor]. When a new build ships a layout
// change (e.g. adding the Viewport window), bump
// kCurrentLayoutVersion; on startup, an ini with an older
// version triggers a one-time BuildDefaultLayout so the new
// window snaps into place without disturbing future custom
// layouts.
const int kCurrentLayoutVersion = 3;
int s_LoadedLayoutVersion = 0;

sf::Window *g_WindowForPersistence = nullptr;

// Click-vs-drag state machine for viewport picking. A pick
// fires only on a true click: LMB press + release at (approx)
// the same spot with no intervening drag. A press that moves
// past MouseDragThreshold is a camera-drag, not a pick.
bool s_PickDownValid = false; // LMB pressed inside viewport
ImVec2 s_PickDownPos(0, 0);	  // press position (screen px)
bool s_PickIsDrag = false;	  // exceeded threshold since press

void HookEngineLog() {
	if (auto log = fury::Log<0>::Instance()) {
		log->SetExtraSink([](const fury::Record& rec) {
			LogLevel lvl = LogLevel::Info;
			if (rec.level == "EROR")
				lvl = LogLevel::Error;
			else if (rec.level == "WARN")
				lvl = LogLevel::Warn;
			else if (rec.level == "DBUG")
				lvl = LogLevel::Debug;
			std::string text = rec.stream.str();
			GlobalLogBuffer().Push(lvl, std::move(text));
		});
	}
}

void UnhookEngineLog() {
	if (auto log = fury::Log<0>::Instance()) {
		log->SetExtraSink(nullptr);
	}
}

// imgui.ini settings handler -- persists the active theme index
// alongside ImGui's window layout state. Single key under
// [FuryEditor][Editor]: `Theme=N`.
void* SettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/) {
	return (void*)1; // non-null sentinel -- we have a single entry
}

void SettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
	int v = 0;
	if (std::sscanf(line, "Theme=%d", &v) == 1) {
		SetCurrentThemeIndex(v);
		return;
	}

	if (std::sscanf(line, "Layout=%d", &v) == 1) {
		s_LoadedLayoutVersion = v;
		return;
	}

	// Reference-grid toggle (EditorWindows.cpp owns the global).
	int grid = -1;
	if (std::sscanf(line, "ShowGrid=%d", &grid) == 1 && (grid == 0 || grid == 1)) {
		g_ShowGrid = (grid != 0);
		return;
	}

	// Tracy profiler toggle. Default on (armed; with TRACY_ON_DEMAND an
	// armed profiler still does nothing until a client connects).
	int tracy = -1;
	if (std::sscanf(line, "Tracy=%d", &tracy) == 1 && (tracy == 0 || tracy == 1)) {
		g_TracyEnabled = (tracy != 0);
		return;
	}

	// Import flags -- one line per flag: ImportFlag=<name>=<0|1>.
	// Persisted so toggles in Settings -> Import survive a restart.
	char flagname[128];
	int flagval = -1;
	if (std::sscanf(line, "ImportFlag=%127[^=]=%d", flagname, &flagval) == 2
		&& (flagval == 0 || flagval == 1)) {
		g_ImportFlags[flagname] = (flagval != 0);
		return;
	}

	// Gizmo persistence. Single line:
	// Gizmo=<op>,<space>,<snap_enabled>,<snap_t>,<snap_r>,<snap_s>
	// op: 0=translate, 1=rotate, 2=scale (matches the order we
	// write below, NOT ImGuizmo's bitmask values, so adding
	// future ops doesn't break files in the wild).
	int op = 0, space = 0, snap = 0;
	float st = 0, sr = 0, sc = 0;
	if (std::sscanf(line, "Gizmo=%d,%d,%d,%f,%f,%f",
					&op, &space, &snap, &st, &sr, &sc) == 6) {
		g_GizmoOp = (op == 1)	? ImGuizmo::ROTATE
					: (op == 2) ? ImGuizmo::SCALE
								: ImGuizmo::TRANSLATE;
		g_GizmoSpace = (space == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
		g_SnapEnabled = (snap != 0);
		g_SnapTranslate = st;
		g_SnapRotate = sr;
		g_SnapScale = sc;
		return;
	}

	// Window visibility -- one line per window: Show=<name>=<0|1>.
	// Restoring these BEFORE the first frame means each visible
	// window is Begin()'d every frame, which is what lets ImGui
	// record + restore its [Window][<name>] dock/pos entry (the
	// Settings window previously defaulted closed, so its dock
	// position was never persisted).
	char winname[64];
	int winval = -1;
	if (std::sscanf(line, "Show=%63[^=]=%d", winname, &winval) == 2
		&& (winval == 0 || winval == 1)) {
		static const std::pair<const char*, bool*> kWindows[] = {
			{"Settings", &g_ShowSettings},
			{"Profiler", &g_ShowProfiler},
			{"SceneInspector", &g_ShowSceneInspector},
			{"NodeProperties", &g_ShowNodeProperties},
			{"Console", &g_ShowConsole},
			{"ContentBrowser", &g_ShowContentBrowser},
			{"Viewport", &g_ShowViewport},
			{"Animation", &g_ShowAnimation},
		};
		for (const auto& kv : kWindows) {
			if (std::strcmp(winname, kv.first) == 0) {
				*kv.second = (winval != 0);
				break;
			}
		}
		return;
	}
}

void SettingsHandler_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*) {
	// Apply the persisted theme once ImGui finishes loading the ini.
	ApplyPersistedTheme();

	// Tracy profiler switch (change: add-tracy-profiler), default on;
	// FURY_TRACY=0 still overrides per run.
	Engine::SetTracyEnabled(g_TracyEnabled);

	// One-time layout migration: if the ini predates the current
	// layout version (e.g. an ini from before the Viewport window
	// existed), rebuild the default layout so new windows snap
	// into place. Future launches keep the user's custom layout
	// (the new version is persisted below).
	if (s_LoadedLayoutVersion < kCurrentLayoutVersion)
		s_RequestRebuildLayout = true;
}

void SettingsHandler_WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
	buf->appendf("[%s][Editor]\n", handler->TypeName);
	buf->appendf("Theme=%d\n", GetCurrentThemeIndex());
	buf->appendf("Layout=%d\n", kCurrentLayoutVersion);
	buf->appendf("ShowGrid=%d\n", g_ShowGrid ? 1 : 0);
	buf->appendf("Tracy=%d\n", g_TracyEnabled ? 1 : 0);
	// Import flags -- one line per flag so adding/removing flags
	// doesn't break the format.
	for (const auto& kv : g_ImportFlags)
		buf->appendf("ImportFlag=%s=%d\n", kv.first.c_str(), kv.second ? 1 : 0);
	const int op_idx =
		(g_GizmoOp == ImGuizmo::ROTATE) ? 1 : (g_GizmoOp == ImGuizmo::SCALE) ? 2
																			 : 0;
	const int space_idx = (g_GizmoSpace == ImGuizmo::LOCAL) ? 0 : 1;
	buf->appendf("Gizmo=%d,%d,%d,%.6f,%.6f,%.6f\n",
				 op_idx, space_idx, g_SnapEnabled ? 1 : 0,
				 g_SnapTranslate, g_SnapRotate, g_SnapScale);
	// Window visibility -- restoring these pre-first-frame keeps every
	// window Begin()'d, so ImGui can persist/restore its dock entry.
	buf->appendf("Show=Settings=%d\n", g_ShowSettings ? 1 : 0);
	buf->appendf("Show=Profiler=%d\n", g_ShowProfiler ? 1 : 0);
	buf->appendf("Show=SceneInspector=%d\n", g_ShowSceneInspector ? 1 : 0);
	buf->appendf("Show=NodeProperties=%d\n", g_ShowNodeProperties ? 1 : 0);
	buf->appendf("Show=Console=%d\n", g_ShowConsole ? 1 : 0);
	buf->appendf("Show=ContentBrowser=%d\n", g_ShowContentBrowser ? 1 : 0);
	buf->appendf("Show=Viewport=%d\n", g_ShowViewport ? 1 : 0);
	buf->appendf("Show=Animation=%d\n", g_ShowAnimation ? 1 : 0);
	if (g_WindowForPersistence != nullptr)
	{
		const sf::Vector2u sz = g_WindowForPersistence->getSize();
		const sf::Vector2i pos = g_WindowForPersistence->getPosition();
		if (sz.x > 0 && sz.y > 0)
			buf->appendf("Window=%u,%u,%d,%d\n",
				static_cast<unsigned int>(sz.x),
				static_cast<unsigned int>(sz.y),
				pos.x, pos.y);
	}
	buf->append("\n");
}

void RegisterSettingsHandler() {
	ImGuiSettingsHandler ini;
	ini.TypeName = "FuryEditor";
	ini.TypeHash = ImHashStr("FuryEditor");
	ini.ReadOpenFn = SettingsHandler_ReadOpen;
	ini.ReadLineFn = SettingsHandler_ReadLine;
	ini.ApplyAllFn = SettingsHandler_ApplyAll;
	ini.WriteAllFn = SettingsHandler_WriteAll;
	ImGui::AddSettingsHandler(&ini);
}

void BuildDefaultLayout(ImGuiID dockspace_id) {
	ImGui::DockBuilderRemoveNode(dockspace_id);
	ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

	// Split order: left -> right -> bottom. The bottom region then
	// spans the central area between the two side panels (Unity /
	// Godot convention). The center node hosts the Viewport window.
	ImGuiID center = dockspace_id;
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

	ImGui::DockBuilderDockWindow("Scene Inspector", left);
	ImGui::DockBuilderDockWindow("Node Properties", right);
	ImGui::DockBuilderDockWindow("Profiler", right);
	ImGui::DockBuilderDockWindow("Console", bottom);
	ImGui::DockBuilderDockWindow("Content Browser", bottom);
	ImGui::DockBuilderDockWindow("Viewport", center);

	ImGui::DockBuilderFinish(dockspace_id);
}

// Returns "Cmd+S" on macOS (when ImGui's macOS-style behaviors are
// active), else "Ctrl+S". The shortcut text is display-only;
// actual routing uses ImGui::Shortcut(ImGuiMod_Ctrl|...).
const char* PlatformShortcut(const char* mac_text, const char* other_text) {
	return ImGui::GetIO().ConfigMacOSXBehaviors ? mac_text : other_text;
}

// Per-scene editor reset. Wipes selection + tree state so dangling
// SceneNode* pointers don't leak across scene swaps, then seeds the
// new scene's root as expanded.
void ResetForNewScene() {
	g_OpenedNodes.clear();
	ClearSelection();
	if (Scene::Active && Scene::Active->GetRootNode())
		Editor::SetNodeOpen(Scene::Active->GetRootNode().get(), true);
}

void TriggerNew() {
	if (g_SceneIO.on_new) try {
			g_SceneIO.on_new();
		} catch (...) {}
	ClearCurrentScene();
	ResetForNewScene();
}

// Forward declaration -- TriggerSave falls through to TriggerSaveAs when
// the current scene has no native path (defined below).
void TriggerSaveAs();

void TriggerSave() {
	if (Scene::Active == nullptr) return;
	// In-place save is only available when we have a tracked
	// native (.json / .bin) path. The dirty flag tracks whether
	// the in-memory scene has diverged from the on-disk file,
	// but does NOT gate Save -- the user can always save when
	// they want. Non-native paths (FBX, glTF) always go
	// through Save As since the engine can't write those formats.
	const bool path_ok = !g_CurrentScenePath.empty() && g_CurrentSceneIsNative;
	if (path_ok && g_SceneIO.on_save) {
		try {
			g_SceneIO.on_save(g_CurrentScenePath);
		} catch (...) {}
	} else {
		TriggerSaveAs();
	}
}

// File -> Save As / Ctrl+Shift+S entry point. Defers entirely to the
// Lua-registered on_save_as callback, which now drives a native
// Editor.SaveDialog (replacing the retired ImGui "Save Scene As"
// modal). The empty path argument is unused by Lua -- it's preserved
// only for the std::function<void(const std::string&)> signature.
void TriggerSaveAs() {
	if (Scene::Active == nullptr) return;
	if (g_SceneIO.on_save_as) {
		try {
			g_SceneIO.on_save_as({});
		} catch (...) {}
	}
}

// File -> Import... / Ctrl+Shift+I entry point. Passes an empty string
// to on_import as the signal for "drive the native multi-select
// Editor.OpenDialog" -- the File -> Import > <file> submenu still
// passes a bare filename, which on_import routes to the existing
// Resource/Scene/ load path. Replaces the retired ImGui "Import Scene"
// modal.
void TriggerImport() {
	ResetForNewScene();
	if (g_SceneIO.on_import) {
		try {
			g_SceneIO.on_import({});
		} catch (...) {}
	}
}

// File -> Open... / Ctrl+O entry point. Mirrors TriggerImport/TriggerSaveAs:
// the empty-string signal to on_open drives a native single-select
// Editor.OpenDialog (replacing the retired ImGui "Open Scene" modal).
// The File -> Open > <file> submenu still passes a bare filename.
void TriggerOpen() {
	ResetForNewScene();
	if (g_SceneIO.on_open) {
		try {
			g_SceneIO.on_open({});
		} catch (...) {}
	}
}

// Spawn `fury` detached (no pipe capture, no wait): POSIX posix_spawn with
// SIGCHLD ignored so the child auto-reaps; Windows CreateProcess with the
// handle closed. The editor stays interactive; closing the fury window
// ends the play session.
bool LaunchDetached(const std::string &exePath, const std::vector<std::string> &args) {
#if defined(_WIN32)
	std::string cmdline = "\"" + exePath + "\"";
	for (const auto &a : args)
		cmdline += " \"" + a + "\"";

	STARTUPINFOA si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr,
		FALSE, CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &si, &pi);
	if (!ok)
		return false;
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
#else
	// Auto-reap children so a closed play window never leaves a zombie.
	static bool s_SigchldIgnored = false;
	if (!s_SigchldIgnored) {
		signal(SIGCHLD, SIG_IGN);
		s_SigchldIgnored = true;
	}

	std::vector<char *> argv;
	argv.push_back(const_cast<char *>(exePath.c_str()));
	for (const auto &a : args)
		argv.push_back(const_cast<char *>(a.c_str()));
	argv.push_back(nullptr);

	pid_t pid = 0;
	const int rc = posix_spawn(&pid, exePath.c_str(), nullptr, nullptr,
		argv.data(), environ);
	return rc == 0;
#endif
}

// Play (F5 / menu-bar button): save the active scene to a temp file next to
// the original (never touching it - editor-only nodes are skipped by the
// serializer), then launch `fury Player.lua <temp>` detached.
void TriggerPlay() {
	if (Scene::Active == nullptr) return;

	std::string dir;
	std::string stem = "untitled";
	if (!g_CurrentScenePath.empty()) {
		const std::filesystem::path p(g_CurrentScenePath);
		dir = p.parent_path().generic_string();
		stem = p.stem().generic_string();
	} else {
		// Never-saved scene: system temp. Relative asset paths (textures
		// next to the scene) won't resolve - warn loudly.
		dir = std::filesystem::temp_directory_path().generic_string();
		FURYW << "Play: scene has no file yet; saving temp scene to " << dir
			<< " - relative asset paths may not resolve. Save the scene first for best results.";
	}

	// Clean stale play temps from previous sessions (they're per-scene,
	// so they only ever land in this directory).
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		const std::string name = entry.path().filename().generic_string();
		if (name.rfind(".play_", 0) == 0 && name.size() > 8 &&
			name.compare(name.size() - 8, 8, ".tmp.bin") == 0)
		{
			std::filesystem::remove(entry.path(), ec);
		}
	}

	const std::string tmpPath = dir + "/.play_" + stem + ".tmp.bin";
	if (!FileUtil::SaveCompressedFile(Scene::Active, tmpPath)) {
		FURYE << "Play: failed to write temp scene " << tmpPath;
		return;
	}

	const std::string exePath = FileUtil::GetExecutablePath();
	if (exePath.empty()) {
		FURYE << "Play: cannot locate the running executable.";
		return;
	}
	const std::string exeDir = std::filesystem::path(exePath).parent_path().generic_string();
#if defined(_WIN32)
	const std::string furyPath = exeDir + "/fury.exe";
#else
	const std::string furyPath = exeDir + "/fury";
#endif
	if (!FileUtil::FileExist(furyPath)) {
		FURYE << "Play: fury binary not found at " << furyPath
			<< " - build the `fury` target next to furye.";
		return;
	}

	const std::string playerScript = exeDir + "/Player.lua";
	if (!FileUtil::FileExist(playerScript)) {
		FURYE << "Play: Player.lua not found at " << playerScript;
		return;
	}

	if (LaunchDetached(furyPath, { playerScript, tmpPath })) {
		FURYI << "Play: launched fury on " << tmpPath;
	} else {
		FURYE << "Play: failed to launch " << furyPath;
	}
}


// Filename portion of the tracked scene path, or empty when no
// scene is tracked. Used by the menu-bar status and by the Lua-side
// save_as callback's default-name seed.
std::string CurrentSceneBasename() {
	if (g_CurrentScenePath.empty()) return {};
	std::error_code ec;
	return std::filesystem::path(g_CurrentScenePath).filename().string();
}

void RenderMenuBar() {
	if (!ImGui::BeginMainMenuBar()) return;

	const bool has_scene = (Scene::Active != nullptr);

	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("New", PlatformShortcut("Cmd+N", "Ctrl+N"))) {
			TriggerNew();
		}

		if (ImGui::MenuItem("Open...", PlatformShortcut("Cmd+O", "Ctrl+O"))) {
			TriggerOpen();
		}

		if (ImGui::MenuItem("Import...", PlatformShortcut("Cmd+Shift+I", "Ctrl+Shift+I"))) {
			TriggerImport();
		}

		if (ImGui::MenuItem("Save", PlatformShortcut("Cmd+S", "Ctrl+S"), false, has_scene)) {
			TriggerSave();
		}

		if (ImGui::MenuItem("Save As...", PlatformShortcut("Cmd+Shift+S", "Ctrl+Shift+S"), false, has_scene)) {
			TriggerSaveAs();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Settings")) {
			g_ShowSettings = !g_ShowSettings;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Quit", PlatformShortcut("Cmd+Q", "Ctrl+Q"))) {
			Gui::CloseWindow();
		}

		ImGui::EndMenu();
	}

	// Edit menu: scene-graph actions on the current selection.
	if (ImGui::BeginMenu("Edit")) {
		const bool has_selection = (g_SelectedSceneNode != nullptr);
		const bool can_mutate = has_selection && g_SelectedSceneNode->GetParent() != nullptr;

		if (ImGui::MenuItem("Add Child", nullptr, false, has_selection)) {
			AddChildToSelectedSceneNode();
		}
		if (ImGui::MenuItem("Duplicate", PlatformShortcut("Cmd+D", "Ctrl+D"), false, can_mutate)) {
			DuplicateSelectedSceneNode();
		}
		if (ImGui::MenuItem("Delete", PlatformShortcut("Cmd+Del", "Ctrl+Del"), false, can_mutate)) {
			DeleteSelectedSceneNode();
		}
		ImGui::EndMenu();
	}

	// Global keyboard shortcuts for the Edit menu. We trigger
	// them on the same frame the menu is rendered so the user
	// sees the action in the menu even when triggered by key.
	// ImGui's IsKeyPressed returns true on the first frame the
	// key transitions down; we also gate on !WantCaptureKeyboard
	// so typing in a text field doesn't trigger the action.
	{
		const bool can_mutate = (g_SelectedSceneNode != nullptr && g_SelectedSceneNode->GetParent() != nullptr);
		if (can_mutate && !Gui::WantCaptureKeyboard()) {
			const bool cmd = ImGui::GetIO().KeySuper;
			const bool ctrl = ImGui::GetIO().KeyCtrl;
			const bool mod = (ImGui::GetIO().ConfigMacOSXBehaviors ? cmd : ctrl);
			if (mod && ImGui::IsKeyPressed(ImGuiKey_D)) {
				DuplicateSelectedSceneNode();
			} else if (mod && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
				DeleteSelectedSceneNode();
			}
		}
	}

	if (ImGui::BeginMenu("Window")) {
		ImGui::MenuItem("Viewport", nullptr, &g_ShowViewport);
		ImGui::MenuItem("Profiler", nullptr, &g_ShowProfiler);
		ImGui::MenuItem("Scene Inspector", nullptr, &g_ShowSceneInspector);
		ImGui::MenuItem("Node Properties", nullptr, &g_ShowNodeProperties);
		ImGui::MenuItem("Console", nullptr, &g_ShowConsole);
		ImGui::MenuItem("Content Browser", nullptr, &g_ShowContentBrowser);
		ImGui::MenuItem("Animation", nullptr, &g_ShowAnimation);
		ImGui::Separator();
		ImGui::MenuItem("Settings", nullptr, &g_ShowSettings);
		if (ImGui::MenuItem("Reset Layout")) {
			s_RequestRebuildLayout = true;
			g_ShowSettings = false;
			g_ShowProfiler = false;
			g_ShowNodeProperties = true;
			g_ShowViewport = true;
		}
		ImGui::EndMenu();
	}

	// Script-emitted menus render between Window and the trailing
	// (currently empty) built-ins, matching today's contract.
	Gui::InvokeMenuBarCallback();

	// Right-aligned current-scene status -- what the user is
	// editing right now. Empty path -> "(no scene)" hint so the
	// header is never blank. Hover reveals the full absolute
	// path for paste / disambiguation.
	{
		std::string base = CurrentSceneBasename();
		std::string status;
		if (base.empty()) {
			status = "(no scene)";
		} else if (g_CurrentSceneIsNative) {
			status = base;
		} else {
			// Imported source: lead with [imported] so it's
			// obvious Save will route to Save As.
			status = "[imported] " + base;
		}

		// Play button sits right-aligned with the status: save-to-temp +
		// launch fury on the temp scene (see TriggerPlay).
		const char* play_label = "Play";
		const float play_w = ImGui::CalcTextSize(play_label).x +
			ImGui::GetStyle().FramePadding.x * 2.0f;
		const float text_w = ImGui::CalcTextSize(status.c_str()).x;
		const float avail = ImGui::GetContentRegionAvail().x;
		const float total_w = play_w + 8.0f + text_w + 8.0f;
		if (avail > total_w) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - total_w));
		}

		const bool can_play = (Scene::Active != nullptr);
		if (!can_play) ImGui::BeginDisabled();
		if (ImGui::SmallButton(play_label)) {
			TriggerPlay();
		}
		if (!can_play) ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("Play in fury (F5) - saves the scene to a temp file first");
		}
		ImGui::SameLine(0.0f, 8.0f);

		ImGui::TextDisabled("%s", status.c_str());
		if (!g_CurrentScenePath.empty() && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", g_CurrentScenePath.c_str());
		}
	}

	ImGui::EndMainMenuBar();
}

void HandleShortcuts() {
	// Unconditional global routing -- works whether the menu is open or
	// not. ImGui's text-input fields take focus priority and won't
	// fire these.
	const ImGuiInputFlags route = ImGuiInputFlags_RouteGlobal;

	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, route)) {
		TriggerNew();
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, route)) {
		TriggerOpen();
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_I, route)) {
		TriggerImport();
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, route)) {
		if (Scene::Active != nullptr) TriggerSaveAs();
	} else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, route)) {
		TriggerSave();
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, route)) {
		Gui::CloseWindow();
	}
	if (ImGui::Shortcut(ImGuiKey_F5, route)) {
		TriggerPlay();
	}
}
} // namespace

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void Initialize() {
	std::error_code ec;
	s_HadIniOnStartup = std::filesystem::exists("imgui.ini", ec);

	RegisterSettingsHandler();
	ApplyPersistedTheme();
	HookEngineLog();

	// Initialize nativefiledialog-extended. NFD_Init sets up the
	// platform backend (AppKit on macOS, Win32 on Windows); the
	// Editor.OpenDialog / Editor.SaveDialog Lua bindings call into
	// nfd on demand. NFD_Quit is paired in Shutdown. Failure here is
	// non-fatal -- the dialog bindings will log "NFD_* error" and
	// return nil if a backend isn't available.
	if (NFD_Init() != NFD_OKAY) {
		FURYE << "NFD_Init failed: "
			  << (NFD_GetError() ? NFD_GetError() : "(unknown)");
	}
}

void Tick() {
	// Shortcuts route globally so they fire whether the menu is open
	// or not, and even when no editor window has keyboard focus.
	HandleShortcuts();

	RenderMenuBar();

	s_DockspaceID = ImGui::DockSpaceOverViewport(0,
												 ImGui::GetMainViewport(),
												 ImGuiDockNodeFlags_None);

	if ((s_FirstFrame && !s_HadIniOnStartup) || s_RequestRebuildLayout) {
		BuildDefaultLayout(s_DockspaceID);
		s_RequestRebuildLayout = false;
	}
	s_FirstFrame = false;

	// The "Open Scene" ImGui modal was retired in favor of the native
	// single-select Editor.OpenDialog flow driven by TriggerOpen ->
	// SceneIO.on_open("") (see Editor.lua on_open).
	// (The "Import Scene" modal was likewise retired in favor of
	// TriggerImport -> SceneIO.on_import("").)

	if (g_ShowSettings) RenderSettingsWindow(&g_ShowSettings);
	if (g_ShowProfiler) RenderProfilerWindow(&g_ShowProfiler);
	if (g_ShowSceneInspector) RenderSceneInspectorWindow(&g_ShowSceneInspector);
	if (g_ShowNodeProperties) RenderNodePropertiesWindow(&g_ShowNodeProperties);
	if (g_ShowConsole) RenderConsoleWindow(&g_ShowConsole);
	if (g_ShowContentBrowser) RenderContentBrowserWindow(&g_ShowContentBrowser);
	if (g_ShowAnimation) RenderAnimationWindow(&g_ShowAnimation);
	if (g_ShowViewport) RenderViewportWindow(&g_ShowViewport);

	// Confirm dialog runs after the window renders so a pending
	// request opened this frame can BeginPopupModal on the same
	// frame (OpenPopup is called from RenderConfirmDialog below).
	// It also serializes after Save As / Open / Import modals so
	// an in-flight delete confirm doesn't stack on top of them.
	RenderConfirmDialog();

	// Per-asset editor windows (Mesh + Material). Runs after the
	// Content Browser so a double-click this frame opens the
	// editor modal on the same frame.
	RenderAllOpenAssetEditors();

	// Mesh thumbnail refresh poll (mesh-thumbnail-disk-cache).
	// Re-checks live mesh tiles against the disk cache every
	// ~30 frames and re-enqueues hash + re-render for any mesh
	// whose dirty-transition was observed. The first frame also
	// warms the disk-cache index in case Editor::Initialize ran
	// before the GL subsystem came up.
	{
		static int s_ThumbPollFrame = 0;
		static bool s_ThumbIndexWarmed = false;
		if (!s_ThumbIndexWarmed) {
			Editor::WarmDiskCacheIndex();
			s_ThumbIndexWarmed = true;
		}
		if (++s_ThumbPollFrame >= 30) {
			s_ThumbPollFrame = 0;
			Editor::RefreshMeshThumbnailCache();
		}
	}

	// If the Viewport window is hidden, the editor has no offscreen
	// render target -- tell the pipeline to render to the default
	// framebuffer so the 3D scene doesn't silently keep rendering into
	// a stale RT. RenderViewportWindow sets the RT when visible.
	if (!g_ShowViewport && Pipeline::Active)
		Pipeline::Active->SetRenderTarget(nullptr);

	// The 3D scene now renders into the Viewport window's offscreen
	// render target, so the gizmo and picking operate in the Viewport
	// window's content-rect space (captured by RenderViewportWindow),
	// NOT full-window space. When the Viewport window is hidden or
	// collapsed, g_ViewportVisible is false and gizmo + picking no-op.
	const ImVec2 vp_min = g_ViewportContentMin;
	const ImVec2 vp_size = g_ViewportContentSize;

	// The gizmo is rendered inside RenderViewportWindow (into the
	// Viewport window's draw list, on top of the image), so it runs
	// before this point. ImGuizmo::IsOver()/IsUsing() reflect the
	// gizmo state for THIS frame for the click-resolution below.

	// Click-vs-drag disambiguation for viewport picking. A pick is
	// scheduled only on a true click (press + release at the same
	// spot, no intervening drag). A drag (camera-look) does NOT pick.
	//
	// NOTE: we deliberately do NOT gate on io.WantCaptureMouse here.
	// The Viewport window is a real ImGui window now, so hovering it
	// sets WantCaptureMouse=true -- but that's exactly when we WANT
	// picking to work. g_ViewportHovered (IsWindowHovered on the
	// Viewport window) is the correct gate: it's true only over the
	// viewport, false over docked panels.
	const ImGuiIO& io = ImGui::GetIO();
	const float threshold = io.MouseDragThreshold;
	const bool gizmo_busy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

	// Helper: is the cursor inside the viewport content rect?
	auto cursor_in_viewport = [&](const ImVec2& mp) -> bool {
		return g_ViewportVisible && mp.x >= vp_min.x && mp.x <= vp_min.x + vp_size.x && mp.y >= vp_min.y && mp.y <= vp_min.y + vp_size.y;
	};

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		ImVec2 mp = io.MousePos;
		if (g_ViewportHovered && cursor_in_viewport(mp) && !gizmo_busy) {
			s_PickDownValid = true;
			s_PickDownPos = mp;
			s_PickIsDrag = false;
		} else {
			s_PickDownValid = false;
		}
	}

	// While LMB is held, track displacement; flag as drag past threshold.
	if (s_PickDownValid && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		ImVec2 mp = io.MousePos;
		float dx = mp.x - s_PickDownPos.x;
		float dy = mp.y - s_PickDownPos.y;
		if ((dx * dx + dy * dy) > (threshold * threshold))
			s_PickIsDrag = true;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s_PickDownValid) {
		ImVec2 mp = io.MousePos;
		const bool release_in_vp = cursor_in_viewport(mp);
		if (!s_PickIsDrag && release_in_vp && !gizmo_busy) {
			// True click -- schedule a pick. Coordinates are relative to
			// the viewport content rect (the picking FBO's space).
			Picking::RequestPickAt(ImVec2(mp.x - vp_min.x, mp.y - vp_min.y));
		}
		s_PickDownValid = false;
		s_PickIsDrag = false;
	}
}

void Shutdown() {
	UnhookEngineLog();
	ClearSceneIO();
	ClearSceneTreeProvider();
	ClearCommandHandler();
	ClearCameraControls();
	SetFrameSelectionHandler(nullptr);
	ClearCurrentScene();
	SetSelectedSceneNode(nullptr);
	NFD_Quit();
}

bool GetPersistedWindowSize(int &width, int &height, int &posX, int &posY) {
	std::ifstream in("imgui.ini");
	if (!in.good()) return false;

	std::string line;
	bool in_section = false;
	while (std::getline(in, line))
	{
		if (line.rfind("[FuryEditor][Editor]", 0) == 0) { in_section = true; continue; }
		if (!in_section) continue;
		if (!line.empty() && line[0] == '[') break;

		int pw = 0, ph = 0, px2 = 0, py2 = 0;
		if (std::sscanf(line.c_str(), "Window=%d,%d,%d,%d",
				&pw, &ph, &px2, &py2) == 4 && pw > 0 && ph > 0)
		{
			width = pw;
			height = ph;
			posX = px2;
			posY = py2;
			return true;
		}
	}
	return false;
}

void SetWindowForPersistence(sf::Window *window) {
	g_WindowForPersistence = window;
}

SceneNode* GetSelectedSceneNode() {
	return g_SelectedSceneNode;
}

const SceneNodeSet& GetSelectionSet() {
	return g_SelectionSet;
}

void SetSelectedSceneNode(SceneNode* node) {
	SceneNodeSet next;
	if (node) {
		next.members.push_back(node);
		next.anchor = node;
	}
	if (g_SelectedSceneNode == next.anchor &&
		g_SelectionSet.members.size() == next.members.size())
		return;
	g_SelectionSet = std::move(next);
	g_SelectedSceneNode = g_SelectionSet.anchor;
	if (auto sig = OnSelectionChanged())
		sig->Emit(std::move(g_SelectedSceneNode));
}

void SetSelectionSet(const SceneNodeSet& set) {
	if (set.anchor == g_SelectionSet.anchor &&
		set.members.size() == g_SelectionSet.members.size())
	{
		bool same = true;
		for (size_t i = 0; i < set.members.size(); ++i) {
			if (set.members[i] != g_SelectionSet.members[i]) { same = false; break; }
		}
		if (same) return;
	}
	g_SelectionSet = set;
	g_SelectedSceneNode = g_SelectionSet.anchor;
	if (auto sig = OnSelectionChanged())
		sig->Emit(std::move(g_SelectedSceneNode));
}

void ClearSelection() {
	if (g_SelectionSet.members.empty() && g_SelectionSet.anchor == nullptr)
		return;
	g_SelectionSet = SceneNodeSet{};
	g_SelectedSceneNode = nullptr;
	if (auto sig = OnSelectionChanged())
		sig->Emit(std::move(g_SelectedSceneNode));
}

void SetNodeOpen(SceneNode* node, bool open) {
	if (!node) return;
	if (open) g_OpenedNodes.insert(node);
	else      g_OpenedNodes.erase(node);
}

bool IsNodeOpen(SceneNode* node) {
	if (!node) return false;
	return g_OpenedNodes.count(node) != 0;
}

void RevealInInspector(SceneNode* node) {
	// Open every collapsed ancestor so the picked node is visible.
	if (!node) return;
	for (auto p = node->GetParent().get(); p != nullptr; p = p->GetParent().get())
		if (!IsNodeOpen(p)) SetNodeOpen(p, true);
}

void SelectAssetInBrowser(std::type_index type, const std::string& name) {
	g_SelectedAsset = std::make_pair(type, name);
	g_PendingScrollToAsset = name;
}

std::shared_ptr<Signal<SceneNode*>> OnSelectionChanged() {
	// Function-local static: survives across frames, destroyed at
	// program exit. Avoids static-init ordering hazards with other
	// file-scope globals.
	static auto sig = Signal<SceneNode*>::Create();
	return sig;
}

bool IsPickInFlight() {
	return Picking::IsPickInFlight();
}

bool IsViewportHovered() {
	return g_ViewportHovered;
}

bool IsViewportContentHovered() {
	// True ONLY when:
	//   (1) the Viewport window is visible and sized (not zero),
	//   (2) the cursor is inside the Viewport's content rect (NOT
	//       the title bar / resize borders), AND
	//   (3) the Viewport is the topmost ImGui window at the cursor
	//       position (g_ViewportHovered, set by RenderViewportWindow
	//       via ImGui::IsWindowHovered).
	//
	// All three checks are required. Without check (3) the function
	// would return true whenever a window covering the viewport's
	// content rect (e.g. the Mesh editor's preview pane, the Node
	// Properties panel when it's docked over the viewport) is at
	// the cursor's bounding-rect location - and the camera-input
	// gate in Editor.lua would let the main scene viewer's camera
	// respond to drags intended for the covering window.
	if (!g_ViewportVisible)
		return false;
	if (!g_ViewportHovered)
		return false;
	if (g_ViewportContentSize.x <= 0.0f || g_ViewportContentSize.y <= 0.0f)
		return false;
	const ImVec2 mp = ImGui::GetIO().MousePos;
	return mp.x >= g_ViewportContentMin.x && mp.x <= g_ViewportContentMin.x + g_ViewportContentSize.x && mp.y >= g_ViewportContentMin.y && mp.y <= g_ViewportContentMin.y + g_ViewportContentSize.y;
}

void SetWindowVisible(const char* name, bool visible) {
	if (!name) return;
	if (std::strcmp(name, "Viewport") == 0)
		g_ShowViewport = visible;
	else if (std::strcmp(name, "Settings") == 0)
		g_ShowSettings = visible;
	else if (std::strcmp(name, "Profiler") == 0)
		g_ShowProfiler = visible;
	else if (std::strcmp(name, "SceneInspector") == 0)
		g_ShowSceneInspector = visible;
	else if (std::strcmp(name, "NodeProperties") == 0)
		g_ShowNodeProperties = visible;
	else if (std::strcmp(name, "Console") == 0)
		g_ShowConsole = visible;
	else if (std::strcmp(name, "ContentBrowser") == 0)
		g_ShowContentBrowser = visible;
	else if (std::strcmp(name, "Animation") == 0)
		g_ShowAnimation = visible;
}

bool GetWindowVisible(const char* name) {
	if (!name) return false;
	if (std::strcmp(name, "Viewport") == 0)
		return g_ShowViewport;
	else if (std::strcmp(name, "Settings") == 0)
		return g_ShowSettings;
	else if (std::strcmp(name, "Profiler") == 0)
		return g_ShowProfiler;
	else if (std::strcmp(name, "SceneInspector") == 0)
		return g_ShowSceneInspector;
	else if (std::strcmp(name, "NodeProperties") == 0)
		return g_ShowNodeProperties;
	else if (std::strcmp(name, "Console") == 0)
		return g_ShowConsole;
	else if (std::strcmp(name, "ContentBrowser") == 0)
		return g_ShowContentBrowser;
	else if (std::strcmp(name, "Animation") == 0)
		return g_ShowAnimation;
	return false;
}

void SetImportFlag(const char* name, bool value) {
	if (!name) return;
	g_ImportFlags[name] = value;
}

bool GetImportFlag(const char* name, bool default_value) {
	if (!name) return default_value;
	auto it = g_ImportFlags.find(name);
	if (it == g_ImportFlags.end()) return default_value;
	return it->second;
}

void Log(const char* level, const char* text) {
	if (!text) return;
	GlobalLogBuffer().Push(LevelFromString(level), text);
}

void SetSceneIO(SceneIO io) {
	g_SceneIO = std::move(io);
}
void ClearSceneIO() {
	g_SceneIO = {};
}

void SetCurrentScene(const std::string& path, bool is_native) {
	g_CurrentScenePath = path;
	g_CurrentSceneIsNative = is_native;
	g_SceneDirty = false;
}

void ClearCurrentScene() {
	g_CurrentScenePath.clear();
	g_CurrentSceneIsNative = false;
	g_SceneDirty = false;
}

std::string GetCurrentScenePath() {
	return g_CurrentScenePath;
}

void MarkSceneDirty() {
	g_SceneDirty = true;
}

bool IsSceneDirty() {
	return g_SceneDirty;
}

void ClearSceneDirty() {
	g_SceneDirty = false;
}

std::string GetSceneDir() {
	if (g_SceneIO.scene_dir) {
		try {
			std::string dir = g_SceneIO.scene_dir();
			if (!dir.empty()) return dir;
		} catch (...) {}
	}
	return FileUtil::GetAbsPath("Resource/Scene/");
}

void SetSceneTreeProvider(SceneTreeProvider p) {
	g_TreeProvider = std::move(p);
}
void ClearSceneTreeProvider() {
	g_TreeProvider = {};
}

void SetCommandHandler(CommandHandler h) {
	g_CommandHandler = std::move(h);
}
void ClearCommandHandler() {
	g_CommandHandler = {};
}

void SetFrameSelectionHandler(std::function<void(SceneNode*)> handler) {
	g_FrameSelectionHandler = std::move(handler);
}

void FrameSelection(SceneNode* node) {
	if (!node || !g_FrameSelectionHandler) return;
	// Reject orphans: after a scene reload the previous selection
	// is detached and its world-space values are stale.
	if (!node->GetParent()) return;
	try {
		g_FrameSelectionHandler(node);
	} catch (...) {
		// Handler errors must not propagate into the editor's tick --
		// the Lua binding layer already traps protected_function errors
		// and logs via FURYE, but defense in depth.
	}
}

void SetCameraControls(std::vector<CameraControl> controls) {
	g_CameraControls = std::move(controls);
}
void ClearCameraControls() {
	g_CameraControls.clear();
}

} // namespace Editor
} // namespace fury

#endif // WITH_EDITOR
