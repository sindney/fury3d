## 1. ImGui v1.92.8-docking upgrade

- [x] 1.1 Download ImGui v1.92.8-docking source archive from `https://github.com/ocornut/imgui/releases/tag/v1.92.8-docking` (zip or git tag) and extract.
- [x] 1.2 Snapshot the current `engine/ThirdParty/ImGui/` (e.g. via `git mv` to a temporary `ImGui_legacy/`) so the legacy `imgui_fury.{cpp,h}` PlotVar helper is preserved while we move it.
- [x] 1.3 Replace `engine/ThirdParty/ImGui/` with the v1.92.8-docking sources: `imgui.cpp`, `imgui.h`, `imgui_demo.cpp`, `imgui_draw.cpp`, `imgui_internal.h`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imconfig.h`, `imstb_*.h`.
- [x] 1.4 Move the legacy `PlotVar` helper from `imgui_fury.{cpp,h}` into a new `engine/Fury/Editor/EditorPlotVar.{cpp,h}` (or, if WITH_EDITOR is OFF, keep it as a free helper in `engine/Fury/EditorPlotVar.{cpp,h}` outside the Editor/ dir — but only if any non-editor code still calls it). Confirm `imgui_fury.{cpp,h}` are no longer referenced and delete them.
- [x] 1.5 Delete the redundant lowercase `engine/ThirdParty/imgui/` directory.
- [x] 1.6 Add `engine/ThirdParty/ImGui/backends/imgui_impl_opengl3.{cpp,h}` from the upstream `backends/` folder (verbatim).
- [x] 1.7 Write `engine/ThirdParty/ImGui/backends/imgui_impl_sfml3.{cpp,h}` (initialize, shutdown, new-frame, process-event for SFML 3.1; keystroke / mouse-button / mouse-wheel / mouse-position / focus / clipboard wiring; modeled after upstream's SDL backend, with SFML 3.1 event-variant API).
- [x] 1.8 Update `engine/CMakeLists.txt` ImGui glob to pick up the new file list (`imgui_tables.cpp` and `imgui_widgets.cpp` join the build automatically; `backends/*.cpp` becomes a separate glob so the editor target compiles against them).
- [x] 1.9 Verify `engine/ThirdParty/ImGui/imconfig.h` enables `IMGUI_DISABLE_OBSOLETE_FUNCTIONS=0` (or comment) — we want backward-compatible function names available for the existing `Gui::*` forwarders during the port.

## 2. Port `Gui.cpp` to the new ImGui API

- [x] 2.1 Replace the hand-written renderer in `Gui::Initialize` / `Gui::RenderDrawLists` with `ImGui_ImplOpenGL3_Init("#version 330 core")` + `ImGui_ImplSFML3_Init(window)`. Keep the public `Gui::Initialize(window, scale, fontScale)` signature unchanged.
- [x] 2.2 Set `ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable` after `ImGui::CreateContext()`.
- [x] 2.3 Replace `Gui::HandleEvent` body with `ImGui_ImplSFML3_ProcessEvent(event)`.
- [x] 2.4 Replace `Gui::NewFrame` body with `ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSFML3_NewFrame(window, dt); ImGui::NewFrame()`.
- [x] 2.5 Replace `Gui::Render` body with `ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData())`.
- [x] 2.6 Update `Gui::Shutdown` to call `ImGui_ImplOpenGL3_Shutdown()` then `ImGui_ImplSFML3_Shutdown()` then `ImGui::DestroyContext()`.
- [x] 2.7 Verify each existing `Gui::*` forwarder (`Begin`, `End`, `SliderFloat`, `Checkbox`, `Button`, `Separator`, `Text`, `BeginMenu`, `EndMenu`, `MenuItem`, `InputText`, `SetMenuBarCallback`, `CloseWindow`, `WantCaptureMouse`, `WantCaptureKeyboard`, `ShowDefault`) compiles and behaves the same against the new API. `Begin`'s `(still_open, visible)` shape MUST be preserved.
- [x] 2.8 Remove the duplicate `InputText` declaration in `Gui.h` (`Gui.h:73-77` has the same prototype written twice — clean up while we're in there).
- [x] 2.9 Move the GBuffer / Shadow Buffer / Profiler logic out of `Gui::ShowDefault` (we'll relocate it to the editor's Profiler window in section 6). After this task, `Gui::ShowDefault` SHALL only emit the script menu callback (`m_MenuBarCallback`) inside `BeginMainMenuBar` if WITH_EDITOR is OFF.
- [x] 2.10 Smoke test: build with WITH_EDITOR=OFF (next phase introduces the option), launch `./fury Demo.lua`, confirm the existing camera flythrough + menus still render and dispatch correctly with the new ImGui backend.

## 3. Add WITH_EDITOR cmake option and skeleton

- [x] 3.1 In `engine/CMakeLists.txt`, add `option(WITH_EDITOR "Build the C++ editor shell." ON)`. After parsing options: `if(WITH_EDITOR AND NOT GUI_IMP) message(FATAL_ERROR "WITH_EDITOR=ON requires GUI_IMP=ON") endif()`.
- [x] 3.2 Add `if(WITH_EDITOR) add_definitions(-DWITH_EDITOR) endif()` and append `engine/Fury/Editor/*.cpp` to the engine source glob with an `if(WITH_EDITOR)` guard.
- [x] 3.3 Create `engine/Fury/Editor/Editor.h` exposing the public API: `void Initialize(); void Tick(); void Shutdown(); SceneNode* GetSelectedSceneNode(); void SetWindowVisible(const char*, bool); bool GetWindowVisible(const char*);`. When `WITH_EDITOR` is undefined, every function is `inline` and returns immediately.
- [x] 3.4 Create `engine/Fury/Editor/Editor.cpp` (stub) — `Initialize` no-ops for now, `Tick` calls placeholder `RenderDockspace()` that just begins/ends an empty `BeginMainMenuBar` so we can verify the call site.
- [x] 3.5 In `engine/Fury/Engine.cpp`, add `Editor::Initialize()` / `Editor::Tick()` / `Editor::Shutdown()` calls at the right points in the main loop. `Editor::Tick()` must run AFTER `Gui::NewFrame` and BEFORE `Gui::Render`.
- [x] 3.6 Build with WITH_EDITOR=ON and WITH_EDITOR=OFF — both must link without unresolved symbols.

## 4. Theme registry + Settings window

- [x] 4.1 Create `engine/Fury/Editor/EditorThemes.h` with the `ETheme` enum and a `void ApplyTheme(ETheme)` function plus an `extern const ThemeEntry kThemes[]; extern const std::size_t kThemesCount;`.
- [x] 4.2 Create `engine/Fury/Editor/EditorThemes.cpp` with the 12 `Setup<Name>Style()` functions copied verbatim from `themes-by-TheAncientOwl.md` (sections 1-12), the `kThemes[]` table mapping display name → function, and `ApplyTheme(ETheme)` dispatch.
- [x] 4.3 Register an ImGui custom settings handler (`ImGui::AddSettingsHandler`) keyed `"FuryEditor"` that reads/writes a single `Theme=<index>` line from/to `imgui.ini`.
- [x] 4.4 In `Editor::Initialize`, after `ImGui::CreateContext` (i.e. inside or after `Gui::Initialize`), call `ApplyTheme(persisted_theme)` so the chosen theme is in effect before any window draws.
- [x] 4.5 Create `engine/Fury/Editor/EditorWindows.cpp` with a `RenderSettingsWindow(bool* open)` function. Build the `CollapsingHeader` sections in this order: Camera, Import, Themes.
- [x] 4.6 The Themes section renders an `ImGui::Combo` over `kThemes[]`; on selection, call `ApplyTheme(...)` and mark settings dirty so ImGui persists the choice.
- [x] 4.7 The Camera section renders nothing other than `ImGui::TextDisabled("(no camera settings registered)")` until task 8.5 plugs in `Editor.SetCameraSettings`.
- [x] 4.8 The Import section renders one checkbox `Auto-Add Default Sun` whose state is project-supplied (default true) — wire it through a static `g_AutoDefaultSun` flag exposed via `Editor::GetImportFlag(name)/SetImportFlag(name, value)` so Editor.lua can read it for `import_scene` / `replace_active_scene`.

## 5. Dockspace + default layout + Window menu

- [x] 5.1 In `Editor::Tick`, between `BeginMainMenuBar`/`EndMainMenuBar` and the dockspace, render the editor menu bar: File menu (placeholder for now), Window menu (toggles), then call `Gui::SetMenuBarCallback`'s registered Lua function so `Camera` etc. render after.
- [x] 5.2 Render an `ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode)` so the 3D scene renders through the empty central area.
- [x] 5.3 Implement `BuildDefaultLayout(ImGuiID dockspace_id)` using `DockBuilderRemoveNode / AddNode / SetNodeSize / SplitNode / DockWindow / Finish`: split LEFT 0.20 → "Scene Inspector"; split BOTTOM 0.30 of remainder → tab "Console" + "Content Browser"; do NOT dock Settings or Profiler.
- [x] 5.4 Call `BuildDefaultLayout` once on first Tick (when no `imgui.ini` exists OR after the user clicks `Window → Reset Layout`).
- [x] 5.5 Add the Window menu: `MenuItem("Profiler", nullptr, &g_show_profiler)`, same for SceneInspector / Console / ContentBrowser; separator; `MenuItem("Reset Layout")` calls `BuildDefaultLayout`. Settings is reachable via `File → Settings`, not Window.
- [x] 5.6 Verify on first launch: Scene Inspector docked left, Console + Content Browser tabbed bottom, Settings/Profiler hidden until toggled.

## 6. Profiler / GBuffer / Shadows consolidated window

- [x] 6.1 In `EditorWindows.cpp`, add `RenderProfilerWindow(bool* open)` rendered when `*open` is true.
- [x] 6.2 Inside the window, render an `ImGui::BeginTabBar("ProfilerTabs")` with three `BeginTabItem` blocks: `FPS`, `GBuffer`, `Shadows`.
- [x] 6.3 Move the FPS plot + memory + drawcall + bounds checkboxes (originally in `Gui.cpp:514-566`) into the FPS tab. The `Use Cascaded Shadow Map` checkbox stays in this tab.
- [x] 6.4 Move the GBuffer texture previews (originally `Gui.cpp:739-765`) into the GBuffer tab, using `ImVec2(window_size.x / 4, window_size.y / 4)` for sizing rather than the global display size.
- [x] 6.5 Move the shadow-map / cube-map / 2D-array previews (originally `Gui.cpp:568-738`) into the Shadows tab.
- [x] 6.6 Add the Profiler window to the Window-menu toggles (already done in 5.5; just connect `g_show_profiler` to this function).
- [ ] 6.7 Verify each tab renders correctly while the window is floating; verify the same when the user docks the Profiler window manually.

## 7. Scene Inspector / Console / Content Browser windows

- [x] 7.1 Implement `RenderSceneInspectorWindow(bool* open)` — recursive `ImGui::TreeNodeEx` walk over `Scene::Active->GetRootNode()` (or the Lua-supplied tree provider once 8.4 lands). Track `g_selected_node` on click; render selected nodes with `ImGuiTreeNodeFlags_Selected`. Clear `g_selected_node` if it points to a freed node (after Scene::Clear).
- [x] 7.2 Implement a fixed-size ring buffer log sink in `engine/Fury/Editor/EditorLog.{cpp,h}`: capacity 4096, push thread-safely, color per level. Hook the engine's `Log` macros to push into this sink in addition to writing to `Log.txt`.
- [x] 7.3 Implement `RenderConsoleWindow(bool* open)` — top section is a scrolling `ImGui::BeginChild("log", ..., ImGuiWindowFlags_HorizontalScrollbar)` rendering each entry with its level color via `ImGui::PushStyleColor(ImGuiCol_Text, color)`. Auto-scroll only if the scroll position is at the bottom on entry.
- [x] 7.4 Add the bottom `ImGui::InputText("##cmd", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)`. On Enter: push `> <line>` (Info), invoke registered command handler if any, clear `buf`, `ImGui::SetKeyboardFocusHere(-1)`.
- [x] 7.5 Add a `Clear` button at the top of the Console window that empties the log ring buffer.
- [x] 7.6 Implement `RenderContentBrowserWindow(bool* open)` — call `FileUtil::ListDirectory(scene_dir(), {})` each frame the window is visible; render results as `ImGui::Selectable` rows with a `[J]/[B]/[G]/[F]/[?]` extension prefix. Track `g_selected_file`.
- [ ] 7.7 Verify `File → Save As… → foo.json` is followed by `foo.json` appearing in the Content Browser on the next frame.

## 8. Lua bindings for Editor

- [x] 8.1 In `engine/Fury/LuaBindings.cpp`, add a `RegisterEditor(sol::state_view lua)` block guarded by `#ifdef WITH_EDITOR`. When undefined, the `Editor` table is registered with no-op stubs so user scripts can call `Editor.Log` etc. without conditional compilation.
- [x] 8.2 Bind `Editor.SetSceneIO(table)` — store the `list_files / on_new / on_open / on_import / on_save_as / scene_dir` callbacks in static `std::function` slots in the editor.
- [x] 8.3 Bind `Editor.SetSceneTreeProvider(fn)` and `Editor.SetCommandHandler(fn)`. Tree provider is invoked from `RenderSceneInspectorWindow` when set; command handler from the Console InputText Enter path.
- [x] 8.4 Bind `Editor.SetCameraSettings(table)` — store control list, render in Settings → Camera section as labeled `SliderFloat`s (or `Checkbox`es for kind=="checkbox") that call back into Lua via the registered `set` function.
- [x] 8.5 Bind `Editor.Log(level, text)`, `Editor.GetSelectedSceneNode()`, `Editor.SetWindowVisible(name, bool)`, `Editor.GetWindowVisible(name)`. Map name strings to `g_show_*` flags.
- [ ] 8.6 Verify the Lua-level `print(Editor.Log)` prints a function (not nil) when `WITH_EDITOR=ON`, and prints a no-op function when `WITH_EDITOR=OFF`.
- [x] 8.7 Make sure the editor clears its registered Lua callbacks before sol2 destroys the Lua state (mirror `Gui::SetMenuBarCallback({})` cleanup that already exists at shutdown).

## 9. Rename Demo.lua → Editor.lua and refactor

- [x] 9.1 `git mv examples/Demo.lua examples/Editor.lua`.
- [x] 9.2 Edit `examples/main.cpp` argv resolution: change the default-script-path constant from `"Demo.lua"` to `"Editor.lua"` (the `arg[0]` fallback string and the on-disk lookup).
- [x] 9.3 In `Editor.lua`, replace the local `build_menu_bar` File menu emission with a call to `Editor.SetSceneIO({list_files=..., on_new=..., on_open=..., on_import=..., on_save_as=..., scene_dir=...})`. The existing `open_scene / import_scene / save_active_scene / list_scene_files` helpers stay; they're called from the SetSceneIO callbacks.
- [x] 9.4 Remove the `show_save_modal` / `Gui.Begin("Save Scene As", ...)` block — the editor's modal handles it now.
- [x] 9.5 Move the `Auto-Add Default Sun` toggle out of the File menu; replace with a project-side `Editor.SetImportFlag("auto_default_sun", auto_default_sun)` initial sync, then read it from `Editor.GetImportFlag("auto_default_sun")` inside `import_scene` / `replace_active_scene`.
- [x] 9.6 Replace the `show_camera_window` window with `Editor.SetCameraSettings({controls = { {label="Move Speed", get=function() return move_speed end, set=function(v) move_speed = v end, kind="slider", min=0.5, max=50.0}, {label="Mouse Sensitivity", get=..., set=..., kind="slider", min=0.0005, max=0.02}, }})`. The Camera top-level menu's `Settings` item now toggles the editor's Settings window via `Editor.SetWindowVisible("Settings", not Editor.GetWindowVisible("Settings"))`.
- [x] 9.7 Wire `Editor.SetCommandHandler(function(line) -- evaluate as Lua, fall back to logging end)` so the Console accepts ad-hoc Lua snippets (use `loadstring` / `load` with pcall protection).
- [x] 9.8 Wire `Editor.SetSceneTreeProvider(function() return walk(Scene.GetActive():GetRootNode()) end)` if the C++ default `Scene::Active`-walk is not sufficient — defer if it works without the Lua hook.
- [x] 9.9 The `set_status` helper SHALL push to the editor log via `Editor.Log("info", msg)` instead of (or in addition to) the floating "status" window. Remove the floating status window once console output is verified.

## 10. Docs & cleanup

- [x] 10.1 Update `docs/CLI.md` references from `Demo.lua` to `Editor.lua`.
- [x] 10.2 Update `docs/LUA.md` — add a section documenting the `Editor.*` API surface (SetSceneIO, SetCommandHandler, SetSceneTreeProvider, SetCameraSettings, Log, GetSelectedSceneNode, SetWindowVisible / GetWindowVisible). Cross-reference the Settings window's three sections.
- [x] 10.3 Remove the residual `engine/Fury/Editor/EditorPlotVar.cpp` move from 1.4 if it's unused after the Profiler tab refactor — the new ImGui ships its own `PlotLines` / `PlotHistogram` and the editor Profiler tab uses those instead.
- [x] 10.4 Run `openspec validate upgrade-imgui-and-add-editor --strict` and resolve any reported issues.

## 11. Smoke test (manual)

- [x] 11.1 Build `WITH_EDITOR=ON GUI_IMP=ON` (default). Launch `./fury` from `examples/`. Confirm Editor.lua loads, the dockspace renders with Scene Inspector left + Console/Content Browser bottom + center transparent showing scene.bin.
- [x] 11.2 Click `Window → Profiler` — Profiler window opens, FPS tab shows the curve, GBuffer + Shadows tabs render textures.
- [x] 11.3 Click `File → Settings` — Settings window opens; switch to each of the 12 themes; Cyberpunk's neon-pink borders are visible. Restart the engine; the chosen theme persists.
- [x] 11.4 Click `File → Open → tank.fbx` — the tank renders textured. The Scene Inspector tree updates with the imported nodes.
- [x] 11.5 Click `File → Save As…`, type `foo.json`, click Save — the Content Browser shows `foo.json` on the next frame.
- [x] 11.6 Type `print("hello")` in the Console input field, press Enter — `> print("hello")` shows in the log; the Lua print result follows.
- [x] 11.7 Drag a window to verify docking previews render. Drag a window out to verify it can float.
- [x] 11.8 Click `Window → Reset Layout` — windows snap back to their default positions.
- [x] 11.9 Build `WITH_EDITOR=OFF`. Launch `./fury` — Editor.lua loads, no editor windows render, no menu bar (or only the script-emitted Camera menu via `Gui.SetMenuBarCallback`). Camera flythrough still works.
