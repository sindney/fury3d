# editor-shell

## Purpose

The C++ editor shell built on top of the engine's ImGui integration: vendoring of ImGui v1.92.8-docking, the `WITH_EDITOR` CMake gate, the top-level dockspace and default layout, the editor-owned File / Window menus, and the four built-in windows (Settings, Profiler, Scene Inspector, Console, Content Browser) plus the `Editor` Lua extension surface and the bundled theme registry. Defines the contract between the engine and `Editor.lua` so scripts plug into the editor by registering callbacks rather than emitting menu UI directly.

## Requirements

### Requirement: The engine SHALL vendor ImGui v1.92.8-docking with docking enabled

The `engine/ThirdParty/ImGui/` directory SHALL contain the upstream
ImGui v1.92.8 docking branch sources (`imgui.cpp`, `imgui.h`,
`imgui_demo.cpp`, `imgui_draw.cpp`, `imgui_internal.h`,
`imgui_tables.cpp`, `imgui_widgets.cpp`, `imconfig.h`, the `imstb_*.h`
helpers) plus a `backends/` subdirectory containing
`imgui_impl_opengl3.{cpp,h}` (upstream verbatim) and a custom
`imgui_impl_sfml3.{cpp,h}` written for SFML 3.

`Gui::Initialize` SHALL set `ImGuiConfigFlags_DockingEnable` on
`ImGui::GetIO().ConfigFlags`. Multi-viewport
(`ImGuiConfigFlags_ViewportsEnable`) SHALL NOT be set in this change.

The legacy `engine/ThirdParty/imgui/` lowercase directory SHALL be
removed; only the canonical `ImGui/` (capital I) directory is kept.

The deprecated `RenderDrawListsFn` callback path SHALL be replaced
with the explicit `ImGui::Render()` →
`ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData())` flow inside
`Gui::Render`.

#### Scenario: ImGui version reports 1.92.8

- **WHEN** the engine starts up with `_FURY_GUI_IMP_` defined
- **THEN** `IMGUI_VERSION` resolves to a string starting with `"1.92.8"`
- **AND** `IMGUI_VERSION_NUM` is at least `19280`

#### Scenario: Docking config flag is enabled

- **WHEN** `Gui::Initialize` returns successfully
- **THEN** `ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable` is non-zero
- **AND** dragging a window's title bar onto another window produces a docking preview overlay

#### Scenario: Legacy lowercase imgui directory is removed

- **WHEN** the repository is checked out fresh
- **THEN** `engine/ThirdParty/imgui/` does not exist
- **AND** `engine/ThirdParty/ImGui/` exists with v1.92.8-docking sources

### Requirement: The engine SHALL gate the editor subsystem behind a `WITH_EDITOR` CMake option

`engine/CMakeLists.txt` SHALL declare an `option(WITH_EDITOR "Build the C++ editor shell." ON)`. When ON it SHALL `add_definitions(-DWITH_EDITOR)` and append `engine/Fury/Editor/*.cpp` to the engine source list. When OFF the editor sources SHALL NOT be compiled and the engine SHALL link without unresolved references to the `fury::Editor::*` symbols.

`WITH_EDITOR=ON` SHALL require `GUI_IMP=ON`. CMake SHALL emit a fatal error if `WITH_EDITOR=ON` and `GUI_IMP=OFF`.

A no-op `inline` definition of `Editor::Tick()` (and any other call sites the engine reaches into) SHALL be provided in the editor header (guarded by `#ifndef WITH_EDITOR`) so callers compile and link cleanly with `WITH_EDITOR=OFF`.

#### Scenario: Default build enables the editor

- **WHEN** the user runs `cmake -S . -B build` without overriding `WITH_EDITOR`
- **THEN** `WITH_EDITOR` defaults to `ON`
- **AND** `WITH_EDITOR` is defined as a preprocessor symbol in the engine target's compile definitions

#### Scenario: Editor can be disabled at configure time

- **WHEN** the user runs `cmake -S . -B build -DWITH_EDITOR=OFF`
- **THEN** the engine builds successfully
- **AND** no source under `engine/Fury/Editor/` is compiled into the engine target
- **AND** `Editor::Tick()` resolves to a no-op inline function

#### Scenario: Editor without GUI errors at configure time

- **WHEN** the user runs `cmake -S . -B build -DWITH_EDITOR=ON -DGUI_IMP=OFF`
- **THEN** CMake emits a fatal error explaining that `WITH_EDITOR=ON` requires `GUI_IMP=ON`

### Requirement: The editor SHALL render a top-level dockspace with default layout

When `WITH_EDITOR` is enabled, every frame the engine SHALL invoke `Editor::Tick()` which SHALL render an ImGui DockSpace covering the entire main viewport (excluding the menu bar). The dockspace ID SHALL be stable across frames so docked windows persist.

On first run (no `imgui.ini` present, or after the user invokes `Window → Reset Layout`), the editor SHALL build a default dock layout via `ImGui::DockBuilder*` APIs:

- A LEFT region (~20% of width) hosting the **Scene Inspector** window.
- A BOTTOM region (~30% of height of the central area) hosting the **Console** and **Content Browser** windows in the same tab group.
- The remaining CENTER region is the un-docked viewport area (no built-in window docks here — the 3D scene renders behind the dockspace).
- The **Settings** and **Profiler** windows SHALL NOT be pre-docked; they SHALL appear floating when first toggled visible.

The dockspace SHALL use `ImGuiDockNodeFlags_PassthruCentralNode` so the 3D scene rendered by `Pipeline::Execute` is visible through the empty center region.

#### Scenario: Default layout on first run

- **WHEN** the engine starts with `WITH_EDITOR=ON` and no `imgui.ini` exists in the working directory
- **THEN** the Scene Inspector window is docked along the left edge
- **AND** the Console and Content Browser windows are tabbed together along the bottom
- **AND** the central area is transparent so the rendered 3D scene is visible

#### Scenario: Reset Layout restores defaults

- **WHEN** the user has rearranged windows and clicks `Window → Reset Layout`
- **THEN** the dockspace is rebuilt with the default left/bottom split
- **AND** Scene Inspector, Console, Content Browser snap back to their default docks
- **AND** Settings and Profiler windows close (visibility flags cleared)

#### Scenario: Layout persists across restarts

- **WHEN** the user docks a previously-floating window and closes the engine
- **AND** the engine is launched again from the same working directory
- **THEN** the window opens in the position it was docked

### Requirement: The editor SHALL own the top-level menu bar with File and Window menus

The editor (not Lua scripts) SHALL emit the `File` menu and the `Window` menu in the main menu bar. Menu structure:

- **File**:
  - `New` — invokes the registered `on_new` Lua callback if any; otherwise clears the active scene.
  - `Open ▸` — submenu listing entries returned by the registered `list_files` Lua callback (default: `FileUtil::ListDirectory("Resource/Scene/", {".json",".bin",".gltf",".glb",".fbx"})`). Selecting an entry invokes the `on_open` callback with the chosen filename.
  - `Import ▸` — submenu mirroring `Open` but invoking the `on_import` callback.
  - `Save As…` — opens a modal with an `InputText` field; on confirm, invokes the `on_save_as` callback with the user-typed filename.
  - (Separator)
  - `Settings` — toggles the Settings window's visibility.
  - (Separator)
  - `Quit` — closes the engine window via `Gui::CloseWindow` (idempotent; the OS window-close button does the same thing).
- **Window**: bullet-button style toggles (`ImGui::MenuItem(..., nullptr, &show)`) for each built-in window:
  - `Profiler`
  - `Scene Inspector`
  - `Console`
  - `Content Browser`
  - (Separator)
  - `Reset Layout`
- After the editor's File and Window menus, the optional Lua-set menu callback (`Gui::SetMenuBarCallback`) SHALL run, so script-emitted menus (e.g. `Camera` from Editor.lua) render between `Window` and the engine's `View` menu.
- The engine's existing `View` menu (Profiler / GBuffer / Shadow Buffers debug toggles) SHALL be replaced by the consolidated **Profiler** window described below; the standalone `View` menu SHALL be removed.

#### Scenario: Menu bar shows File / Window first

- **WHEN** the engine renders the main menu bar
- **THEN** `File` is the leftmost menu and `Window` is to its right
- **AND** any Lua-registered menu callback renders after `Window`

#### Scenario: File → Save As opens a modal

- **WHEN** the user clicks `File → Save As…`
- **THEN** an ImGui modal with an `InputText` pre-filled with `scene_saved.json` opens
- **AND** clicking `Save` invokes the registered `on_save_as` callback with the entered filename
- **AND** the modal closes after the callback returns

#### Scenario: Window menu toggles use bullet-style checkmarks

- **WHEN** the user clicks `Window → Console` while the Console window is hidden
- **THEN** the Console window becomes visible
- **AND** a checkmark / bullet glyph next to `Console` indicates its visible state on the next frame

#### Scenario: Old standalone View menu is gone

- **WHEN** the engine renders the menu bar with `WITH_EDITOR=ON`
- **THEN** no top-level menu labeled `View` is present
- **AND** the previous `View → Profiler / GBuffer / Shadow Buffers` toggles are reachable via the Profiler window's tabs

### Requirement: The editor SHALL ship a Settings window with Camera, Import, and Themes sections

The Settings window SHALL be hidden by default and is opened via `File → Settings`. When visible, the window SHALL render three collapsible sections (`ImGui::CollapsingHeader`) in this order:

- **Camera** — sliders for move speed and mouse sensitivity. The exact bindings are project-supplied via `Editor::SetCameraSettings(...)` (Lua surface: `Editor.SetCameraSettings`); when no project hooks are registered, the section SHALL render a one-line "(no camera settings registered)" placeholder.
- **Import** — toggles for import-time options. At minimum, the existing `auto_default_sun` flag (currently a File-menu toggle in `Demo.lua`) SHALL move into this section as a checkbox.
- **Themes** — a `Combo` listing all 12 themes from the registry (`Dark`, `Forest Green`, `Amethyst`, `Sapphire`, `AmberYellow`, `Dracula`, `CatppuccinMocha`, `GruvboxHard`, `CrimsonVesuvius`, `RoseQuartz`, `Cyberpunk`, `PaperAndInk`). Selecting an entry SHALL apply the corresponding `Setup<Theme>Style()` function immediately and persist the choice across restarts.

The selected theme SHALL be persisted via an ImGui custom settings handler (`ImGui::AddSettingsHandler`) so it ends up in the same `imgui.ini` file as window layout / open-state. On startup the editor SHALL apply the persisted theme before rendering any window.

#### Scenario: Settings window opens via File menu

- **WHEN** the user clicks `File → Settings`
- **THEN** the Settings window becomes visible (floating, by default)
- **AND** clicking it again toggles it closed

#### Scenario: Themes section applies styles immediately

- **WHEN** the user opens Settings and selects `Forest Green` from the theme combo
- **THEN** the next frame's UI uses the Forest Green palette (`ImGuiCol_WindowBg ≈ {0.06,0.09,0.06,1.0}`)
- **AND** the selection is stored in `imgui.ini`

#### Scenario: Theme persists across restart

- **WHEN** the user picks `Dracula` and restarts the engine
- **THEN** the next launch starts up rendered with the Dracula palette before any window draws

### Requirement: The editor SHALL ship a Profiler window combining FPS, GBuffer, and shadow-buffer debug

The standalone `Profiler`, `GBuffer`, and `Shadow Buffers` windows previously emitted by `Gui::ShowDefault` SHALL be consolidated into one editor-owned **Profiler** window with three tabs (`ImGui::BeginTabBar` + `BeginTabItem`):

- **FPS** — the existing `PlotVar`-based FPS graph plus CPU/GPU memory readouts plus drawcall / triangle / mesh / light counts plus the existing checkbox toggles (`Draw Light Bounds`, `Draw Mesh Bounds`, `Draw Custom Bounds`, `Use Cascaded Shadow Map`).
- **GBuffer** — the depth/normal/diffuse/light texture previews currently in `Gui.cpp`.
- **Shadows** — the 2D shadow map + cube map + 2D-array shadow-map previews currently in `Gui.cpp`.

The window SHALL be hidden by default and toggled from `Window → Profiler`.

#### Scenario: Profiler window has three tabs

- **WHEN** the user clicks `Window → Profiler`
- **THEN** a window titled `Profiler` is visible
- **AND** the window contains a tab bar with `FPS`, `GBuffer`, and `Shadows` tabs in that order

#### Scenario: GBuffer tab shows the deferred render targets

- **WHEN** the Profiler window is visible and the user clicks the `GBuffer` tab
- **THEN** the depth, normal, diffuse, and light buffers render as ImGui images
- **AND** the textures match what `View → GBuffer` showed in the prior implementation

#### Scenario: Shadows tab renders the existing previews

- **WHEN** the Profiler window is visible and the user clicks the `Shadows` tab
- **THEN** the 2D shadow map, cube shadow map (six faces), and 2D-array shadow map previews are visible
- **AND** the previews match what `View → Shadow Buffers` showed in the prior implementation

### Requirement: The editor SHALL ship a Scene Inspector window rendering the active scene as a tree

The Scene Inspector window SHALL render the active scene's node hierarchy as an ImGui tree (`ImGui::TreeNodeEx` with `ImGuiTreeNodeFlags_OpenOnArrow | OpenOnDoubleClick | DefaultOpen` for the root). Each node SHALL display its name (`SceneNode::GetName()`); selecting a node (single click) SHALL update an internal `selected_node` reference.

The tree data SHALL be obtained via the registered `Editor::SetSceneTreeProvider` Lua callback when one is registered; otherwise it SHALL walk `Scene::Active->GetRootNode()` directly.

The window SHALL NOT yet render a property panel for the selected node — property editing is a follow-up. The selected node reference is exposed via `Editor::GetSelectedSceneNode()` (also bound to Lua) so future panels can plug in.

The window SHALL be hidden by default; it is enabled by default in the dock layout but the `Window → Scene Inspector` toggle controls visibility.

#### Scenario: Scene Inspector mirrors the active scene tree

- **WHEN** Editor.lua opens `Resource/Scene/scene.bin`
- **AND** the Scene Inspector window is visible
- **THEN** the window renders a tree node for the scene root
- **AND** the root contains child nodes for each top-level node in `scene.bin` (the tank, grass, etc.)

#### Scenario: Selection survives across frames

- **WHEN** the user clicks a node in the tree
- **THEN** subsequent frames render that node with the selected highlight (`ImGuiTreeNodeFlags_Selected`)
- **AND** `Editor::GetSelectedSceneNode()` returns the matching `SceneNode` pointer

#### Scenario: Selection clears when the underlying node is removed

- **WHEN** the user has selected a node and `Scene::Active->Clear()` is called (via `File → New`)
- **THEN** `Editor::GetSelectedSceneNode()` returns null
- **AND** the inspector renders only the (now-empty) root

### Requirement: The editor SHALL ship a Console window with a single-line input and a multi-line log view

The Console window SHALL contain:

1. A multi-line scrollable log view (top, ~80% of window height) rendered with `ImGui::TextUnformatted` per entry, color-coded by level: Debug=gray, Info=cyan, Warn=orange, Error=red, Critical=dark-red. The view SHALL auto-scroll to the bottom when new entries arrive unless the user has scrolled up manually.
2. A single-line `ImGui::InputText` field (bottom) with `ImGuiInputTextFlags_EnterReturnsTrue`. On Enter, the typed text SHALL be:
   - echoed into the log view with a `> ` prefix (Info level), and
   - dispatched to the registered command handler via `Editor::SetCommandHandler` (Lua: `Editor.SetCommandHandler`).
   The input field is then cleared, ready for the next command.
3. A header-row `Clear` button that empties the log buffer.

The log buffer SHALL be a fixed-size ring (capacity 4096 entries). When full, the oldest entry is dropped on each new push.

The editor SHALL hook the engine's `Log` macros (`FURYI`, `FURYW`, `FURYE`, `FURYD`) so engine-emitted log lines appear in the same view automatically. Lua scripts SHALL be able to push lines via `Editor.Log(level, text)` where `level ∈ {"info","warn","error","debug"}`.

The window SHALL be hidden by default and toggled from `Window → Console`.

#### Scenario: Console renders a tail of the engine log

- **WHEN** the engine logs three lines via `FURYI / FURYW / FURYE` while the Console is open
- **THEN** the three lines appear in the log view in order, color-coded by level

#### Scenario: Pressing Enter dispatches to the command handler

- **WHEN** the user types `hello world` into the Console input field and presses Enter
- **AND** Editor.lua has registered a command handler `function(line) Log.info("got: " .. line) end`
- **THEN** the log view shows `> hello world`
- **AND** the next line shows `got: hello world`
- **AND** the input field is cleared

#### Scenario: Auto-scroll respects user scrollback

- **WHEN** the user scrolls the Console log view up to read older entries
- **AND** new entries arrive
- **THEN** the view does not auto-jump to the bottom (manual scroll position is preserved until the user scrolls back to the bottom)

#### Scenario: Clear empties the buffer

- **WHEN** the Console contains 100 entries and the user clicks `Clear`
- **THEN** the log view becomes empty
- **AND** subsequent log lines accumulate from zero

### Requirement: The editor SHALL ship a Content Browser mirroring the active scene's directory

The Content Browser window SHALL list the contents of the directory that contains the most-recently opened or newly-created scene, as reported by `Editor::SetSceneIO`'s `scene_dir` callback. When no callback is registered or no scene has been opened, the directory SHALL default to `Resource/Scene/`.

Each frame the window is visible, it SHALL re-enumerate the directory via `FileUtil::ListDirectory` (no caching). Files SHALL be displayed with a one-character extension prefix (`[J] foo.json`, `[B] foo.bin`, `[G] foo.gltf`, `[F] foo.fbx`) and rendered as selectables (`ImGui::Selectable`). Selecting a file SHALL update an internal `selected_file` reference (no further action — detailed view is a follow-up).

Subdirectories and hidden files (leading `.`) SHALL be excluded.

The window SHALL be hidden by default and toggled from `Window → Content Browser`.

#### Scenario: Browser shows files in the current scene directory

- **WHEN** Editor.lua opens `Resource/Scene/scene.bin`
- **AND** the Content Browser window is visible
- **THEN** the window lists every non-hidden file in `Resource/Scene/`
- **AND** subdirectories are not listed

#### Scenario: Browser updates after Save As writes a new file

- **WHEN** the user uses `File → Save As…` to write `Resource/Scene/foo.json`
- **AND** the Content Browser window is visible on the next frame
- **THEN** the new file `foo.json` appears in the listing without restarting

#### Scenario: Browser falls back to Resource/Scene when no scene is loaded

- **WHEN** the engine starts with no startup scene
- **AND** the Content Browser window is visible
- **THEN** the listing reflects `Resource/Scene/` contents

### Requirement: The editor SHALL register a `Gui::Editor` Lua table for project extension

The Lua bindings registered by `LuaBindings::Register` SHALL include an `Editor` global table with the following functions when `WITH_EDITOR` is defined:

- `Editor.SetSceneIO(table)` — accepts a Lua table with optional fields `list_files`, `on_new`, `on_open`, `on_import`, `on_save_as`, `scene_dir`. Each is a Lua function (or nil to clear). The editor's File menu and Content Browser route through these callbacks.
- `Editor.SetSceneTreeProvider(fn)` — `fn()` returns a Lua tree representation `{name=..., children={...}}`. Cleared by passing `nil`.
- `Editor.SetCommandHandler(fn)` — `fn(line)` is invoked for each Console-submitted command. Cleared with `nil`.
- `Editor.SetCameraSettings(table)` — accepts a Lua table that drives the Camera section of the Settings window. The shape is `{controls = { {label="Move Speed", get=fn, set=fn, kind="slider", min=..., max=...}, ... }}`. Each control entry produces one widget in the Settings → Camera section.
- `Editor.Log(level, text)` — push a line into the Console log view. `level` is one of `"info"`, `"warn"`, `"error"`, `"debug"`.
- `Editor.GetSelectedSceneNode()` — returns the `SceneNode` currently selected in the Scene Inspector, or nil.
- `Editor.SetWindowVisible(name, bool)` / `Editor.GetWindowVisible(name)` — programmatic control of the four built-in window visibility flags. Valid names: `"Profiler"`, `"SceneInspector"`, `"Console"`, `"ContentBrowser"`, `"Settings"`.

When `WITH_EDITOR` is not defined, the `Editor` table SHALL still exist but every function SHALL be a safe no-op (so user scripts compose with both builds).

#### Scenario: Editor.SetSceneIO drives the File menu

- **WHEN** Editor.lua calls `Editor.SetSceneIO({on_open = function(p) print("open " .. p) end, list_files = function() return {"a.json","b.gltf"} end})`
- **AND** the user clicks `File → Open → a.json`
- **THEN** the registered `on_open` callback runs with the argument `"a.json"`

#### Scenario: Editor.Log appears in the Console window

- **WHEN** Editor.lua calls `Editor.Log("warn", "low memory")`
- **AND** the Console window is visible
- **THEN** a new entry `low memory` rendered in the warn (orange) color appears in the log view

#### Scenario: Editor.SetCommandHandler receives Console input

- **WHEN** Editor.lua calls `Editor.SetCommandHandler(function(s) ran = s end)`
- **AND** the user types `quit` into the Console input and presses Enter
- **THEN** the global `ran` equals `"quit"`

#### Scenario: Editor table is a no-op when WITH_EDITOR=OFF

- **WHEN** the engine is built with `WITH_EDITOR=OFF` and Editor.lua calls `Editor.SetSceneIO(...)` and `Editor.Log(...)`
- **THEN** neither call throws or logs an error
- **AND** subsequent script logic continues normally

### Requirement: The editor SHALL bundle 12 themes from `themes-by-TheAncientOwl.md`

The editor SHALL register the following 12 themes as `void Setup<Name>Style()` functions in `engine/Fury/Editor/EditorThemes.cpp`, with the exact color and sizing values specified in
`/Users/sindney/Documents/git/furyengine/imgui_styles/themes-by-TheAncientOwl.md`:

1. `Dark`
2. `ForestGreen`
3. `Amethyst`
4. `Sapphire`
5. `AmberYellow`
6. `Dracula`
7. `CatppuccinMocha`
8. `GruvboxHard`
9. `CrimsonVesuvius`
10. `RoseQuartz`
11. `Cyberpunk`
12. `PaperAndInk`

A static `kThemes[]` table SHALL list each theme with a display name (the human-readable form, e.g. `"Forest Green"`, `"Catppuccin Mocha"`) and a function pointer. The Settings window's Themes combo SHALL render directly from this table; adding a new theme is a single edit to `kThemes[]` plus its `Setup*Style()` body.

#### Scenario: All 12 themes are selectable

- **WHEN** the user opens Settings and clicks the Themes combo
- **THEN** the combo lists exactly 12 entries in the order specified above

#### Scenario: A theme applies the documented values

- **WHEN** the user selects `Cyberpunk` from the Themes combo
- **THEN** `ImGui::GetStyle().Colors[ImGuiCol_WindowBg]` equals `ImVec4(0.02f, 0.02f, 0.04f, 1.00f)` (within float epsilon)
- **AND** `ImGui::GetStyle().WindowRounding` equals `0.0f`
