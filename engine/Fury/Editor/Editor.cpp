#ifdef WITH_EDITOR

#include "Fury/Editor/Editor.h"

#include "Fury/Editor/EditorAssetWindows.h"
#include "Fury/Editor/EditorConfirmDialog.h"
#include "Fury/Editor/EditorLog.h"
#include "Fury/Editor/EditorPicking.hpp"
#include "Fury/Editor/EditorThemes.h"
#include "Fury/FileUtil.h"
#include "Fury/Gui.h"
#include "Fury/Log.h"
#include "Fury/MeshRender.h"
#include "Fury/Pipeline.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGuizmo.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <vector>

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
extern ImVec2 g_ViewportContentMin;
extern ImVec2 g_ViewportContentSize;
extern bool g_ViewportHovered;
extern bool g_ViewportVisible;
extern bool g_SaveAsModalOpen;
extern bool g_OpenModalOpen;
extern bool g_ImportModalOpen;
extern std::string g_CurrentScenePath;
extern bool g_CurrentSceneIsNative;

// Defined in EditorWindows.cpp — set by SelectAssetInBrowser,
// read by RenderContentBrowserWindow.
extern std::optional<std::pair<std::type_index, std::string>> g_SelectedAsset;
extern std::optional<std::string> g_PendingScrollToAsset;

// Per-TU storage. Defined here, declared as extern above so
// EditorWindows.cpp can read them without a header dependency.
SceneIO g_SceneIO;
SceneTreeProvider g_TreeProvider;
CommandHandler g_CommandHandler;
std::vector<CameraControl> g_CameraControls;
std::unordered_map<std::string, bool> g_ImportFlags;
SceneNode* g_SelectedSceneNode = nullptr;
bool g_ShowSettings = false;
bool g_ShowProfiler = false;
bool g_ShowSceneInspector = true; // visible by default — docked left
bool g_ShowNodeProperties = true; // visible by default — docked right
bool g_ShowConsole = true;		  // visible by default — bottom dock
bool g_ShowContentBrowser = true; // visible by default — bottom dock
bool g_ShowViewport = true;		  // visible by default — docked center
// Captured each frame by RenderViewportWindow so the gizmo and
// picking operate in viewport-content-rect space (not full-window
// space). When the Viewport window is hidden/collapsed, size is
// zero and g_ViewportVisible is false — gizmo + picking no-op.
ImVec2 g_ViewportContentMin(0, 0);
ImVec2 g_ViewportContentSize(0, 0);
bool g_ViewportHovered = false;
bool g_ViewportVisible = false;
bool g_SaveAsModalOpen = false;
bool g_OpenModalOpen = false;
bool g_ImportModalOpen = false;
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
const int kCurrentLayoutVersion = 2;
int s_LoadedLayoutVersion = 0;

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

// imgui.ini settings handler — persists the active theme index
// alongside ImGui's window layout state. Single key under
// [FuryEditor][Editor]: `Theme=N`.
void* SettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/) {
	return (void*)1; // non-null sentinel — we have a single entry
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
	}
}

void SettingsHandler_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*) {
	// Apply the persisted theme once ImGui finishes loading the ini.
	ApplyPersistedTheme();

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
	const int op_idx =
		(g_GizmoOp == ImGuizmo::ROTATE) ? 1 : (g_GizmoOp == ImGuizmo::SCALE) ? 2
																			 : 0;
	const int space_idx = (g_GizmoSpace == ImGuizmo::LOCAL) ? 0 : 1;
	buf->appendf("Gizmo=%d,%d,%d,%.6f,%.6f,%.6f\n",
				 op_idx, space_idx, g_SnapEnabled ? 1 : 0,
				 g_SnapTranslate, g_SnapRotate, g_SnapScale);
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

	// Split order: left → right → bottom. The bottom region then
	// spans the central area between the two side panels (Unity /
	// Godot convention). The center node hosts the Viewport window.
	ImGuiID center = dockspace_id;
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

	ImGui::DockBuilderDockWindow("Scene Inspector", left);
	ImGui::DockBuilderDockWindow("Node Properties", right);
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

void TriggerNew() {
	if (g_SceneIO.on_new) try {
			g_SceneIO.on_new();
		} catch (...) {}
	ClearCurrentScene();
	SetSelectedSceneNode(nullptr);
}

void TriggerSave() {
	if (Scene::Active == nullptr) return;
	// In-place save is only available when we have a tracked
	// native (.json / .bin) path. The dirty flag tracks whether
	// the in-memory scene has diverged from the on-disk file,
	// but does NOT gate Save — the user can always save when
	// they want. Non-native paths (FBX, glTF) always go
	// through Save As since the engine can't write those formats.
	const bool path_ok = !g_CurrentScenePath.empty() && g_CurrentSceneIsNative;
	if (path_ok && g_SceneIO.on_save) {
		try {
			g_SceneIO.on_save(g_CurrentScenePath);
		} catch (...) {}
	} else {
		g_SaveAsModalOpen = true;
	}
}

// Filename portion of the tracked scene path, or empty when no
// scene is tracked. Used by the menu-bar status and by the Save
// As modal's default filename seed.
std::string CurrentSceneBasename() {
	if (g_CurrentScenePath.empty()) return {};
	std::error_code ec;
	return std::filesystem::path(g_CurrentScenePath).filename().string();
}

void RenderSaveAsModal() {
	static char filename[256] = "scene_saved.json";

	if (g_SaveAsModalOpen) {
		// Seed with the current scene's basename when it's already
		// native; otherwise propose <stem>.json so an imported FBX
		// like "tank.fbx" suggests "tank.json".
		std::string base = CurrentSceneBasename();
		if (!base.empty()) {
			std::filesystem::path p(base);
			if (g_CurrentSceneIsNative) {
				std::snprintf(filename, sizeof(filename), "%s", base.c_str());
			} else {
				std::snprintf(filename, sizeof(filename), "%s.json",
							  p.stem().string().c_str());
			}
		}
		ImGui::OpenPopup("Save Scene As");
		g_SaveAsModalOpen = false;
	}

	if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (!g_CurrentScenePath.empty()) {
			ImGui::TextDisabled("Current: %s", g_CurrentScenePath.c_str());
		}
		ImGui::Text("Filename (under %s):", GetSceneDir().c_str());
		ImGui::InputText("##save_as_name", filename, IM_ARRAYSIZE(filename));
		ImGui::Separator();

		if (ImGui::Button("Save", ImVec2(120, 0))) {
			if (g_SceneIO.on_save_as) {
				try {
					g_SceneIO.on_save_as(filename);
				} catch (...) {}
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// Shared body for the Open / Import modals. Reuses g_SceneIO.list_files
// so the listing matches the corresponding submenu.
void RenderOpenImportModal(
	const char* popup_id,
	const char* confirm_label,
	bool* open_request,
	const std::function<void(const std::string&)>& on_pick) {
	static int s_SelectedIndex = -1;

	if (*open_request) {
		ImGui::OpenPopup(popup_id);
		*open_request = false;
		s_SelectedIndex = -1;
	}

	if (ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		std::vector<std::string> files;
		if (g_SceneIO.list_files) {
			try {
				files = g_SceneIO.list_files();
			} catch (...) {}
		}

		ImGui::TextDisabled("%s", GetSceneDir().c_str());
		ImGui::Separator();

		ImGui::BeginChild("##files", ImVec2(360, 240), true);
		for (int i = 0; i < (int)files.size(); ++i) {
			bool selected = (s_SelectedIndex == i);
			if (ImGui::Selectable(files[i].c_str(), selected,
								  ImGuiSelectableFlags_AllowDoubleClick)) {
				s_SelectedIndex = i;
				if (ImGui::IsMouseDoubleClicked(0)) {
					if (on_pick) try {
							on_pick(files[i]);
						} catch (...) {}
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::EndChild();

		ImGui::Separator();
		const bool can_confirm = (s_SelectedIndex >= 0 && s_SelectedIndex < (int)files.size());

		if (!can_confirm) ImGui::BeginDisabled();
		if (ImGui::Button(confirm_label, ImVec2(120, 0))) {
			if (can_confirm && on_pick) {
				try {
					on_pick(files[s_SelectedIndex]);
				} catch (...) {}
			}
			ImGui::CloseCurrentPopup();
		}
		if (!can_confirm) ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void RenderMenuBar() {
	if (!ImGui::BeginMainMenuBar()) return;

	const bool has_scene = (Scene::Active != nullptr);

	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("New", PlatformShortcut("Cmd+N", "Ctrl+N"))) {
			TriggerNew();
		}

		std::vector<std::string> files;
		if (g_SceneIO.list_files) {
			try {
				files = g_SceneIO.list_files();
			} catch (...) {}
		}

		if (ImGui::BeginMenu("Open", !files.empty())) {
			for (const auto& f : files) {
				if (ImGui::MenuItem(f.c_str())) {
					if (g_SceneIO.on_open) try {
							g_SceneIO.on_open(f);
						} catch (...) {}
					SetSelectedSceneNode(nullptr);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Open...", PlatformShortcut("Cmd+O", "Ctrl+O"))) {
			g_OpenModalOpen = true;
		}

		if (ImGui::BeginMenu("Import", !files.empty())) {
			for (const auto& f : files) {
				if (ImGui::MenuItem(f.c_str())) {
					if (g_SceneIO.on_import) try {
							g_SceneIO.on_import(f);
						} catch (...) {}
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Import...", PlatformShortcut("Cmd+Shift+I", "Ctrl+Shift+I"))) {
			g_ImportModalOpen = true;
		}

		if (ImGui::MenuItem("Save", PlatformShortcut("Cmd+S", "Ctrl+S"), false, has_scene)) {
			TriggerSave();
		}

		if (ImGui::MenuItem("Save As...", PlatformShortcut("Cmd+Shift+S", "Ctrl+Shift+S"), false, has_scene)) {
			g_SaveAsModalOpen = true;
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
		ImGui::Separator();
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

	// Right-aligned current-scene status — what the user is
	// editing right now. Empty path → "(no scene)" hint so the
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

		const float text_w = ImGui::CalcTextSize(status.c_str()).x;
		const float avail = ImGui::GetContentRegionAvail().x;
		if (avail > text_w + 8.0f) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - text_w - 8.0f));
		}
		ImGui::TextDisabled("%s", status.c_str());
		if (!g_CurrentScenePath.empty() && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", g_CurrentScenePath.c_str());
		}
	}

	ImGui::EndMainMenuBar();
}

void HandleShortcuts() {
	// Unconditional global routing — works whether the menu is open or
	// not. ImGui's text-input fields take focus priority and won't
	// fire these.
	const ImGuiInputFlags route = ImGuiInputFlags_RouteGlobal;

	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, route)) {
		TriggerNew();
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, route)) {
		g_OpenModalOpen = true;
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_I, route)) {
		g_ImportModalOpen = true;
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, route)) {
		if (Scene::Active != nullptr) g_SaveAsModalOpen = true;
	} else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, route)) {
		TriggerSave();
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, route)) {
		Gui::CloseWindow();
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

	RenderSaveAsModal();
	RenderOpenImportModal("Open Scene", "Open", &g_OpenModalOpen,
						  [](const std::string& f) {
							  if (g_SceneIO.on_open) try {
									  g_SceneIO.on_open(f);
								  } catch (...) {}
							  SetSelectedSceneNode(nullptr);
						  });
	RenderOpenImportModal("Import Scene", "Import", &g_ImportModalOpen,
						  [](const std::string& f) {
							  if (g_SceneIO.on_import) try {
									  g_SceneIO.on_import(f);
								  } catch (...) {}
						  });

	if (g_ShowSettings) RenderSettingsWindow(&g_ShowSettings);
	if (g_ShowProfiler) RenderProfilerWindow(&g_ShowProfiler);
	if (g_ShowSceneInspector) RenderSceneInspectorWindow(&g_ShowSceneInspector);
	if (g_ShowNodeProperties) RenderNodePropertiesWindow(&g_ShowNodeProperties);
	if (g_ShowConsole) RenderConsoleWindow(&g_ShowConsole);
	if (g_ShowContentBrowser) RenderContentBrowserWindow(&g_ShowContentBrowser);
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

	// If the Viewport window is hidden, the editor has no offscreen
	// render target — tell the pipeline to render to the default
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
	// sets WantCaptureMouse=true — but that's exactly when we WANT
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
			// True click — schedule a pick. Coordinates are relative to
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
	ClearCurrentScene();
	SetSelectedSceneNode(nullptr);
}

SceneNode* GetSelectedSceneNode() {
	return g_SelectedSceneNode;
}

void SetSelectedSceneNode(SceneNode* node) {
	if (g_SelectedSceneNode == node)
		return;
	g_SelectedSceneNode = node;
	if (auto sig = OnSelectionChanged())
		sig->Emit(std::move(node));
}

void SelectAssetInBrowser(std::type_index type, const std::string& name) {
	// Sets the Content Browser's selected asset (type_index, name)
	// and flags a scroll-to-selection for the next frame. This is
	// the "jump to asset" mechanism used by the Node Properties
	// inspector's mesh / material / texture rows. No-op if the
	// asset doesn't actually exist in the EntityManager — the
	// grid just won't find a matching tile and the scroll flag
	// clears next frame.
	g_SelectedAsset = std::make_pair(type, name);
	g_PendingScrollToAsset = name;
}

std::shared_ptr<Signal<SceneNode*>> OnSelectionChanged() {
	// Function-local static: initialized on first call, survives
	// across frames, destroyed at program exit. Avoids static-init
	// ordering hazards with other file-scope globals.
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
	// Same predicate the picking / cursor_in_viewport helper in
	// Editor::Tick uses: must be visible AND sized AND the mouse
	// must be inside the content rect (not the title bar / borders).
	// g_ViewportContentMin/Size are written each frame by
	// RenderViewportWindow; if the viewport is hidden or collapsed,
	// size is zero and this short-circuits to false.
	if (!g_ViewportVisible)
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

void SetCameraControls(std::vector<CameraControl> controls) {
	g_CameraControls = std::move(controls);
}
void ClearCameraControls() {
	g_CameraControls.clear();
}
} // namespace Editor
} // namespace fury

#endif // WITH_EDITOR
