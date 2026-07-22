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
- A RIGHT region (~20% of width) hosting the **Node Properties** window.
- A BOTTOM region (~30% of height of the central area, between left and right) hosting the **Console** and **Content Browser** windows in the same tab group.
- The CENTER region hosts the **Viewport** window (see editor-viewport-window spec), which displays the 3D scene rendered to its offscreen render target.
- The **Settings** and **Profiler** windows SHALL NOT be pre-docked; they SHALL appear floating when first toggled visible.

The split order SHALL be: left first, right second, bottom third — so the bottom region spans the central area without overlapping the side panels.

The dockspace SHALL NOT use `ImGuiDockNodeFlags_PassthruCentralNode`. The 3D scene is rendered into the Viewport window's offscreen render target and presented via `ImGui::Image` inside the Viewport window (see editor-viewport-window spec), so the central dock node is occupied by a real window and no passthru is needed.

#### Scenario: Default layout on first run

- **WHEN** the engine starts with `WITH_EDITOR=ON` and no `imgui.ini` exists in the working directory
- **THEN** the Scene Inspector window is docked along the left edge
- **AND** the Node Properties window is docked along the right edge
- **AND** the Console and Content Browser windows are tabbed together along the bottom (between the two side panels)
- **AND** the Viewport window is docked in the central region and displays the rendered 3D scene

#### Scenario: Reset Layout restores defaults

- **WHEN** the user has rearranged windows and clicks `Window → Reset Layout`
- **THEN** the dockspace is rebuilt with the default left/right/bottom/center split
- **AND** Scene Inspector, Node Properties, Console, Content Browser, and Viewport snap back to their default docks
- **AND** Settings and Profiler windows close (visibility flags cleared)

#### Scenario: Layout persists across restarts

- **WHEN** the user docks a previously-floating window and closes the engine
- **AND** the engine is launched again from the same working directory
- **THEN** the window opens in the position it was docked

#### Scenario: Pre-existing imgui.ini without Viewport dock

- **WHEN** the engine starts with `WITH_EDITOR=ON` and an `imgui.ini` from a prior version that has no entry for `Viewport`
- **THEN** the editor does not auto-rebuild the layout (preserves the user's other dock arrangement)
- **AND** the Viewport window opens floating until the user clicks `Window → Reset Layout` or docks it manually

### Requirement: The editor SHALL own the top-level menu bar with File and Window menus

The editor (not Lua scripts) SHALL emit the `File` menu and the `Window` menu in the main menu bar. Menu structure:

- **File**:
  - `New` — `Ctrl+N` shortcut. Invokes the registered `on_new` Lua callback if any; otherwise clears the active scene. Also calls `Editor::ClearCurrentScene()`.
  - `Open ▸` — submenu listing entries returned by the registered `list_files` Lua callback (default: `FileUtil::ListDirectory("Resource/Scene/", {".json",".bin",".gltf",".glb",".fbx"})`). Selecting an entry invokes the `on_open` callback with the chosen filename.
  - `Open…` — `Ctrl+O` shortcut. Opens the Open Scene modal listing the same files as the submenu (see the new modal requirement).
  - `Import ▸` — submenu mirroring `Open ▸` but invoking the `on_import` callback.
  - `Import…` — `Ctrl+Shift+I` shortcut. Opens the Import Scene modal.
  - `Save` — `Ctrl+S` shortcut. Routes to in-place save when the current scene is native, otherwise opens the Save As modal (see the Save requirement).
  - `Save As…` — `Ctrl+Shift+S` shortcut. Opens a modal with an `InputText` field; on confirm, invokes the `on_save_as` callback with the user-typed filename.
  - (Separator)
  - `Settings` — toggles the Settings window's visibility.
  - (Separator)
  - `Quit` — `Ctrl+Q` shortcut. Closes the engine window via `Gui::CloseWindow` (idempotent; the OS window-close button does the same thing).
- **Window**: bullet-button style toggles (`ImGui::MenuItem(..., nullptr, &show)`) for each built-in window:
  - `Viewport`
  - `Profiler`
  - `Scene Inspector`
  - `Node Properties`
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

#### Scenario: File menu shows shortcuts on each item

- **WHEN** the user opens the `File` menu on macOS
- **THEN** `New`, `Open…`, `Import…`, `Save`, `Save As…`, `Quit` each show their shortcut text right-aligned (`Cmd+N`, `Cmd+O`, etc.)
- **AND** the submenu entries `Open ▸` and `Import ▸` do not show shortcuts (they're aggregate items)

#### Scenario: Window menu includes Node Properties toggle

- **WHEN** the user opens the `Window` menu
- **THEN** `Node Properties` appears between `Scene Inspector` and `Console` (or in the documented order above)
- **AND** clicking it toggles the Node Properties window's visibility
- **AND** a checkmark / bullet glyph next to `Node Properties` reflects the visible state

#### Scenario: Window menu includes Viewport toggle

- **WHEN** the user opens the `Window` menu
- **THEN** `Viewport` appears at the top of the built-in-window list
- **AND** clicking it toggles the Viewport window's visibility
- **AND** a checkmark / bullet glyph next to `Viewport` reflects the visible state

#### Scenario: File → Save As opens a modal

- **WHEN** the user clicks `File → Save As…` (or presses `Ctrl+Shift+S`)
- **THEN** an ImGui modal with an `InputText` pre-filled with `scene_saved.json` opens
- **AND** clicking `Save` invokes the registered `on_save_as` callback with the entered filename
- **AND** the modal closes after the callback returns

#### Scenario: File → New clears the tracked scene path

- **WHEN** the user clicks `File → New` (or presses `Ctrl+N`)
- **THEN** the active scene is cleared (or the registered `on_new` callback runs)
- **AND** `Editor::GetCurrentScenePath()` returns "" on the next frame
- **AND** subsequent `File → Save` opens the Save As modal

#### Scenario: Window menu toggles use bullet-style checkmarks

- **WHEN** the user clicks `Window → Console` while the Console window is hidden
- **THEN** the Console window becomes visible
- **AND** a checkmark / bullet glyph next to `Console` indicates its visible state on the next frame

#### Scenario: Old standalone View menu is gone

- **WHEN** the engine renders the menu bar with `WITH_EDITOR=ON`
- **THEN** no top-level menu labeled `View` is present
- **AND** the previous `View → Profiler / GBuffer / Shadow Buffers` toggles are reachable via the Profiler window's tabs

### Requirement: The editor SHALL ship a Settings window with Editor, Import, and Engine sections

The Settings window SHALL be hidden by default and is opened via `File → Settings`. When visible, the window SHALL render three collapsible sections (`ImGui::CollapsingHeader`) in this order:

- **Editor** — the project-supplied camera controls (sliders for move speed and mouse sensitivity via `Editor::SetCameraSettings(...)`, or the `"(no camera settings registered)"` placeholder), the `Show Grid` toggle (see the `editor-reference-grid` capability), the gizmo snap-step sizes (three `DragFloat` widgets for translate / rotate / scale step — the `Snap` toggle itself lives in the Viewport toolbar), and the theme `Combo` listing all 12 themes from the registry (`Dark`, `Forest Green`, `Amethyst`, `Sapphire`, `AmberYellow`, `Dracula`, `CatppuccinMocha`, `GruvboxHard`, `CrimsonVesuvius`, `RoseQuartz`, `Cyberpunk`, `PaperAndInk`). Selecting a theme SHALL apply the corresponding `Setup<Theme>Style()` function immediately and persist the choice across restarts.
- **Import** — toggles for import-time options: the `auto_default_sun` flag and the `auto_scale_detect` flag (see the `import-unit-scale` capability), each as a checkbox, plus the `Normal Gen` combo (`Smooth (default)` / `Flat`) backed by the `normals_smooth` flag (see the `gltf-importer` capability).
- **Engine** — the read-only unit/coordinate info and the **Cascaded Shadow Map (CSM)** checkbox driving `PipelineSwitch::CASCADED_SHADOW_MAP`.

**Only the Editor section SHALL carry `ImGuiTreeNodeFlags_DefaultOpen`**; the Import and Engine sections SHALL render collapsed on first use (no prior imgui.ini state). Section open-state thereafter follows ImGui's normal persisted tree state.

The selected theme SHALL be persisted via an ImGui custom settings handler (`ImGui::AddSettingsHandler`) so it ends up in the same `imgui.ini` file as window layout / open-state. On startup the editor SHALL apply the persisted theme before rendering any window.

#### Scenario: Settings window opens via File menu

- **WHEN** the user clicks `File → Settings`
- **THEN** the Settings window becomes visible (floating, by default)
- **AND** clicking it again toggles it closed

#### Scenario: Only Editor is expanded on first use

- **WHEN** the Settings window opens with no prior persisted imgui.ini state
- **THEN** the Editor section is expanded
- **AND** the Import and Engine sections are collapsed

#### Scenario: Themes section applies styles immediately

- **WHEN** the user opens Settings and selects `Forest Green` from the theme combo
- **THEN** the next frame's UI uses the Forest Green palette (`ImGuiCol_WindowBg ≈ {0.06,0.09,0.06,1.0}`)
- **AND** the selection is stored in `imgui.ini`

#### Scenario: Theme persists across restart

- **WHEN** the user picks `Dracula` and restarts the engine
- **THEN** the next launch starts up rendered with the Dracula palette before any window draws

### Requirement: The editor SHALL ship a Profiler window combining FPS, GBuffer, and shadow-buffer debug

The standalone `Profiler`, `GBuffer`, and `Shadow Buffers` windows previously emitted by `Gui::ShowDefault` SHALL be consolidated into one editor-owned **Profiler** window with three tabs (`ImGui::BeginTabBar` + `BeginTabItem`):

- **Perf** — the existing `PlotVar`-based FPS graph plus CPU/GPU memory readouts plus drawcall / triangle / mesh / light counts plus the **LOD Debug** section (added by the `mesh-lod-debug-view` capability: live histogram of `MeshRender::GetActiveLod()` counts) plus the OcTree Spatial readout (added by the `octree-spatial` capability). The tab is named `Perf` (not `FPS`) because it carries performance counters and debug sections well beyond the FPS graph. The **Debug Overlay multi-select combo formerly in this tab is removed** — it now lives in the Viewport top toolbar (see the `editor-viewport-window` capability); the `LOD Debug Colors` toggle moves with it.
- **GBuffer** — the depth/normal/diffuse/light texture previews currently in `Gui.cpp`.
- **Shadows** — the **Use Cascaded Shadow Maps** checkbox plus one preview section **per shadow-casting light** in the active scene, populated by iterating every `Light` with `GetCastShadows() == true` and pulling the per-frame shadow texture via `Pipeline::GetLastShadowTexture(*lightNode)`. When more than one shadow-casting light exists, a `BeginCombo` dropdown at the top of the tab lets the user focus on one light (the first shadow-casting light is selected by default; there is no "All lights" entry). See the `shadow-debug-per-light` capability for full behavior.

The window SHALL be hidden by default and toggled from `Window → Profiler`.

#### Scenario: Profiler window has three tabs

- **WHEN** the user clicks `Window → Profiler`
- **THEN** a window titled `Profiler` is visible
- **AND** the window contains a tab bar with `Perf`, `GBuffer`, and `Shadows` tabs in that order

#### Scenario: GBuffer tab shows the deferred render targets

- **WHEN** the Profiler window is visible and the user clicks the `GBuffer` tab
- **THEN** the depth, normal, diffuse, and light buffers render as ImGui images
- **AND** the textures match what `View → GBuffer` showed in the prior implementation

#### Scenario: Shadows tab iterates every shadow-casting light

- **WHEN** the Profiler window is visible and the user clicks the `Shadows` tab
- **AND** the active scene contains two shadow-casting lights (one directional `Sun`, one point `Lamp`)
- **THEN** the tab renders two sections — `Shadow — DIRECTIONAL Sun` and `Shadow — POINT Lamp`
- **AND** each section renders the live shadow texture returned by `Pipeline::Active->GetLastShadowTexture(*lightNode)`

#### Scenario: Shadows tab dropdown filters to one light

- **WHEN** the Shadows tab is visible and the user picks `POINT — Lamp` from the dropdown
- **THEN** only the `Lamp` section renders
- **AND** the `Sun` section does not render

#### Scenario: Shadows tab handles no shadow-casting lights

- **WHEN** the Profiler window is visible, the user opens the Shadows tab, and the active scene has no shadow-casting lights
- **THEN** the tab renders the placeholder `"(no shadow-casting lights)"`

#### Scenario: Perf tab shows the LOD Debug section without the overlays combo

- **WHEN** the Profiler window is visible and the user clicks the `Perf` tab
- **THEN** the LOD Debug section renders after the Spatial section with its per-LOD histogram
- **AND** no `Debug Overlays` combo is rendered in the tab

### Requirement: The editor SHALL ship a Scene Inspector window rendering the active scene as a tree

The Scene Inspector window SHALL render the active scene's node hierarchy as an ImGui tree (`ImGui::TreeNodeEx` with `ImGuiTreeNodeFlags_OpenOnArrow | OpenOnDoubleClick | DefaultOpen` for the root). Each node SHALL display its name (`SceneNode::GetName()`); selecting a node (single click) SHALL update the shared selection (`g_SelectedSceneNode`).

The tree data SHALL be obtained via the registered `Editor::SetSceneTreeProvider` Lua callback when one is registered; otherwise it SHALL walk `Scene::Active->GetRootNode()` directly.

The window SHALL render the selected row with `ImGuiTreeNodeFlags_Selected`. Selection state is shared with viewport picking and the Node Properties panel — clicking in the viewport SHALL also update the highlighted row in the Inspector, and clicking in the Inspector SHALL also drive the gizmo's render position.

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

#### Scenario: Viewport pick syncs the Inspector highlight

- **WHEN** the user left-clicks a renderable node in the central viewport
- **AND** the picking pass resolves to that node
- **THEN** the Scene Inspector renders that node's row with the selected highlight on the next frame
- **AND** `Editor::GetSelectedSceneNode()` returns the picked node

#### Scenario: Inspector click is respected by the gizmo

- **WHEN** the user clicks a node in the Scene Inspector
- **THEN** the gizmo (if visible) renders at that node's world position on the next frame

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

The Content Browser window SHALL render the in-scene assets of the active `Scene`'s `EntityManager` as an icon grid (Unity/UE4-style rectangle tiles in a wrapping `ImGui` layout), NOT a single-column `Selectable` file list. The window SHALL enumerate two asset categories from `Scene::Active->GetEntityManager()`: every registered `Mesh` (`ForEach<Mesh>`) and every registered `Material` (`ForEach<Material>`). When no scene is active or the `EntityManager` has no Meshes or Materials, the window SHALL display the placeholder text `"(no assets in active scene)"`.

Each tile SHALL render at a fixed icon size (default 64×64 thumbnail area + label row below) and the grid SHALL wrap to fill the available content region width. Tiles SHALL be selectable (single selection); clicking a tile SHALL set an internal `selected_asset` pair of `(type_index, name)` and replace the previous selection. Selection state SHALL be per-frame (no persistence across editor sessions).

Each tile's thumbnail SHALL be:

- **For a Material**: a 64×64 `ImGui::Image` of the material's diffuse texture (or first non-null texture if no diffuse), or a flat color swatch derived from the material's `DIFFUSE_COLOR` uniform if no textures are bound, or a checkerboard placeholder if neither is available. The texture GL handle SHALL be cast via `(ImTextureID)(intptr_t)tex->GetID()` with UVs `ImVec2(0,1), ImVec2(1,0)` (matching the existing `EditorNodeProperties.cpp:304` pattern).
- **For a Mesh**: a 128×128 mini-3D render of the mesh, rendered off-screen into a cached FBO per mesh (keyed on the mesh's `BufferId`), drawn with the simple Lambert shader defined in the `mesh-thumbnail-disk-cache` capability (flat `vec3(0.7)` albedo, fixed directional light `(0.4, 0.8, 0.3)`, half-lambert floor `0.2`, opaque black background matching the 3D scene viewport). The FBO SHALL be regenerated when the mesh's content hash changes (computed off-thread over vertex positions + indices per the `mesh-thumbnail-disk-cache` capability), NOT merely when `BufferId` changes. The FBO SHALL also be warmed from the disk cache (`Resource/.thumbcache/furye_<hash>.png`) when a cache hit occurs. The camera SHALL be framed by the mesh's AABB. The thumbnail texture GL handle SHALL be cast via `(ImTextureID)(intptr_t)entry.colorRT->GetID()` with UVs `ImVec2(0,1), ImVec2(1,0)`. The shader SHALL be robust against meshes with missing or zero normals (falling back to `vec3(0, 1, 0)`) so glTF meshes without a normal attribute still produce a non-white silhouette.

Each tile's label row SHALL display the asset's name truncated to fit the tile width (ellipsis on overflow). The tile SHALL additionally render a small type badge (`M` for Mesh, `Mat` for Material) in the top-left corner of the thumbnail.

The Content Browser SHALL support the following interactions:

- **Double-click** on a tile SHALL open the per-asset editor window (see `asset-editor-windows` capability). If the editor window for that asset is already open, double-click SHALL focus the existing window.
- **Right-click** on a tile SHALL open a context menu with the items: `Duplicate`, `Rename`, `Delete`, `Refresh` (the last forces a thumbnail re-render for that mesh by invalidating its in-memory FBO entry and enqueuing a fresh hash + render). Right-clicking empty space in the grid SHALL open a context menu with the items: `Refresh` (re-runs `ForEach` next frame).
- **F2** on a selected tile SHALL activate inline rename (same as `Rename` menu item).

The window SHALL be hidden by default and toggled from `Window → Content Browser`.

The Content Browser SHALL expose a `Editor::SelectAssetInBrowser(type_index, name)` API (C++ side, called by the Node Properties inspector) that sets `selected_asset` and scrolls the grid so the matching tile is visible on the next frame. This is the "jump to asset" mechanism used by the inspector's mesh/material rows.

The legacy file-listing behavior (enumerating `Resource/Scene/` via `FileUtil::ListDirectory` with `[J]`/`[B]`/`[G]`/`[F]` extension prefixes) SHALL be removed from this window. File open/import/save remain reachable via the File menu and the Open/Import modals.

#### Scenario: Browser shows in-scene assets as a wrapping icon grid

- **WHEN** Editor.lua opens `Resource/Scene/scene.bin` containing 3 meshes and 5 materials
- **AND** the Content Browser window is visible
- **THEN** the window renders 8 tiles in a wrapping grid
- **AND** each tile shows a thumbnail, a name label, and a type badge (`M` or `Mat`)

#### Scenario: Empty scene shows placeholder

- **WHEN** the engine starts with no startup scene
- **AND** the Content Browser window is visible
- **THEN** the window displays the placeholder text `"(no assets in active scene)"`

#### Scenario: Selecting a tile updates selected_asset

- **WHEN** the user clicks the "Cube" Mesh tile
- **THEN** `selected_asset` becomes `(typeid(Mesh), "Cube")`
- **AND** the previously selected tile (if any) is deselected

#### Scenario: Material tile shows diffuse thumbnail

- **WHEN** a material named "Material_Ground" has a diffuse_texture bound to `grass.jpg`
- **THEN** the "Material_Ground" tile renders a 64×64 thumbnail of `grass.jpg`

#### Scenario: Material tile falls back to color swatch

- **WHEN** a material has no textures but has a `DIFFUSE_COLOR` uniform of `(0.8, 0.2, 0.2, 1.0)`
- **THEN** the tile's thumbnail area is filled with a flat `(0.8, 0.2, 0.2)` swatch

#### Scenario: Mesh tile shows a simple Lambert render

- **WHEN** a mesh named "Cube" has a textured PBR material bound to its first submesh
- **AND** the Content Browser renders a tile for "Cube"
- **THEN** the tile's thumbnail is a 128×128 render of the Cube mesh with the simple Lambert shader (flat 0.7 grey albedo, fixed directional light, opaque black background)
- **AND** the mesh's bound material is NOT sampled in the thumbnail

#### Scenario: Double-click opens the asset editor

- **WHEN** the user double-clicks the "Cube" Mesh tile
- **THEN** a `Mesh: Cube` editor window opens (per the `asset-editor-windows` capability)

#### Scenario: Right-click opens the Duplicate/Rename/Delete/Refresh context menu

- **WHEN** the user right-clicks the "Cube" Mesh tile
- **THEN** a context menu opens with the items `Duplicate`, `Rename`, `Delete`, `Refresh` enabled

#### Scenario: Refresh forces a thumbnail re-render

- **WHEN** the user right-clicks the "Cube" Mesh tile and selects `Refresh`
- **THEN** the in-memory FBO entry for "Cube" is invalidated
- **AND** on the next periodic refresh poll, the editor enqueues a fresh hash + render for "Cube"

#### Scenario: Refresh re-runs ForEach next frame

- **WHEN** the user right-clicks empty grid space and selects `Refresh`
- **THEN** on the next frame the grid re-enumerates `Scene::Active->GetEntityManager()->ForEach<Mesh>()` and `ForEach<Material>()`

#### Scenario: External SelectAssetInBrowser focuses a tile

- **WHEN** the Node Properties inspector calls `Editor::SelectAssetInBrowser(typeid(Mesh), "Cube")`
- **THEN** on the next frame `selected_asset` is `(typeid(Mesh), "Cube")`
- **AND** the grid scrolls so the "Cube" tile is visible

#### Scenario: Mesh thumbnail FBO is cached on content hash and warmed from disk

- **WHEN** the Content Browser first renders a tile for "Cube" with `BufferId == 42`
- **AND** the disk cache contains `Resource/.thumbcache/furye_<hash_of_cube>.png`
- **THEN** the FBO is populated by loading the PNG from disk (no GL render of the mesh is performed)
- **AND** on subsequent frames, the FBO is reused without re-loading the PNG
- **AND** no new FBO is allocated until "Cube"'s content hash changes (which may or may not coincide with a `BufferId` change)

### Requirement: The editor SHALL register a `Gui::Editor` Lua table for project extension

The Lua bindings registered by `LuaBindings::Register` SHALL include an `Editor` global table with the following functions when `WITH_EDITOR` is defined:

- `Editor.SetSceneIO(table)` — accepts a Lua table with optional fields `list_files`, `on_new`, `on_open`, `on_import`, `on_save`, `on_save_as`, `scene_dir`. Each is a Lua function (or nil to clear). The editor's File menu / shortcuts / Content Browser route through these callbacks. The new `on_save` field is invoked by `File → Save` when the current scene is native; otherwise Save falls through to Save As.
- `Editor.SetCurrentScene(path, is_native)` — records the file the user is currently editing and whether it can be written in place. Called by Editor.lua after each successful open / save_as. `is_native` SHALL be true for `.json` / `.bin`, false for `.gltf` / `.glb` / `.fbx`.
- `Editor.ClearCurrentScene()` — wipes the tracked path / native flag. Called by Editor.lua's `on_new`.
- `Editor.SetSceneTreeProvider(fn)` — `fn()` returns a Lua tree representation `{name=..., children={...}}`. Cleared by passing `nil`.
- `Editor.SetCommandHandler(fn)` — `fn(line)` is invoked for each Console-submitted command. Cleared with `nil`.
- `Editor.SetCameraSettings(table)` — accepts a Lua table that drives the Camera section of the Settings window. The shape is `{controls = { {label="Move Speed", get=fn, set=fn, kind="slider", min=..., max=...}, ... }}`. Each control entry produces one widget in the Settings → Camera section.
- `Editor.Log(level, text)` — push a line into the Console log view. `level` is one of `"info"`, `"warn"`, `"error"`, `"debug"`.
- `Editor.GetSelectedSceneNode()` — returns the `SceneNode` currently selected in the Scene Inspector, or nil.
- `Editor.SetWindowVisible(name, bool)` / `Editor.GetWindowVisible(name)` — programmatic control of the built-in window visibility flags. Valid names: `"Viewport"`, `"Profiler"`, `"SceneInspector"`, `"NodeProperties"`, `"Console"`, `"ContentBrowser"`, `"Settings"`.
- `Editor.IsPickInFlight()` — returns true while the picking state machine is not `Idle` (see viewport-picking spec). Used by `Editor.lua`'s camera-drag to short-circuit during an in-flight pick.

When `WITH_EDITOR` is not defined, the `Editor` table SHALL still exist but every function SHALL be a safe no-op (so user scripts compose with both builds). `Editor.IsPickInFlight()` SHALL return `false` in that case.

#### Scenario: Editor.SetSceneIO drives the File menu

- **WHEN** Editor.lua calls `Editor.SetSceneIO({on_open = function(p) print("open " .. p) end, list_files = function() return {"a.json","b.gltf"} end})`
- **AND** the user clicks `File → Open ▸ → a.json`
- **THEN** the registered `on_open` callback runs with the argument `"a.json"`

#### Scenario: Editor.SetSceneIO accepts on_save

- **WHEN** Editor.lua calls `Editor.SetSceneIO({on_save = function(p) write(p) end, …})`
- **AND** Editor.lua calls `Editor.SetCurrentScene("/abs/scene.json", true)`
- **AND** the user presses `Ctrl+S`
- **THEN** the registered `on_save` callback runs with the argument `"/abs/scene.json"`

#### Scenario: Editor.SetCurrentScene routes Save to in-place vs. modal

- **WHEN** Editor.lua opens a `.fbx` and calls `Editor.SetCurrentScene("/abs/tank.fbx", false)`
- **AND** the user presses `Ctrl+S`
- **THEN** the Save As modal opens
- **AND** the registered `on_save` callback is NOT invoked

#### Scenario: Editor.SetWindowVisible accepts Viewport

- **WHEN** Editor.lua calls `Editor.SetWindowVisible("Viewport", false)`
- **THEN** the Viewport window is hidden on the next frame
- **AND** `Editor.GetWindowVisible("Viewport")` returns false

#### Scenario: Editor.SetWindowVisible accepts NodeProperties

- **WHEN** Editor.lua calls `Editor.SetWindowVisible("NodeProperties", false)`
- **THEN** the Node Properties window is hidden on the next frame
- **AND** `Editor.GetWindowVisible("NodeProperties")` returns false

#### Scenario: Editor.Log appears in the Console window

- **WHEN** Editor.lua calls `Editor.Log("warn", "low memory")`
- **AND** the Console window is visible
- **THEN** a new entry `low memory` rendered in the warn (orange) color appears in the log view

#### Scenario: Editor.SetCommandHandler receives Console input

- **WHEN** Editor.lua calls `Editor.SetCommandHandler(function(s) ran = s end)`
- **AND** the user types `quit` into the Console input and presses Enter
- **THEN** the global `ran` equals `"quit"`

#### Scenario: Editor table is a no-op when WITH_EDITOR=OFF

- **WHEN** the engine is built with `WITH_EDITOR=OFF` and Editor.lua calls `Editor.SetSceneIO(...)` and `Editor.SetCurrentScene(...)` and `Editor.Log(...)` and `Editor.IsPickInFlight()`
- **THEN** none of the calls throws or logs an error
- **AND** `Editor.IsPickInFlight()` returns `false`
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

### Requirement: The editor SHALL ship a Node Properties window with reflection-driven editors

When `WITH_EDITOR` is enabled, the editor SHALL render a window titled `Node Properties` that displays editable widgets for the currently selected SceneNode (`Editor::GetSelectedSceneNode()`) and any reflected components attached to it. Edits SHALL apply to the in-memory scene immediately on each frame they are performed; the editor SHALL NOT serialize to disk except via `File → Save` / `File → Save As…`.

The window SHALL render the following sections, in order, when a node is selected:

- **Node** — read-only `Name`; editable `Local Position` (Vector4 → 3 floats), `Local Rotation` (shown as Euler XYZ in degrees, internally Quaternion via `MathUtil::EulerRadToQuat`), `Local Scale` (Vector4 → 3 floats). After any of these change, the editor SHALL call `SceneNode::Recompose(false)` on the node so the world transforms update.
- **Light** — rendered only when the node has a `Light` component. Fields: `Type` (LightType enum dropdown), `Color` (RGBA color picker), `Intensity` (float drag), `Inner Angle` (degrees in UI; radians in storage), `Outer Angle` (degrees in UI; radians in storage), `Falloff` (float drag), `Radius` (float drag), `Cast Shadows` (checkbox). After any field that affects light geometry changes (`Type`, `InnerAngle`, `OuterAngle`, `Radius`), the editor SHALL call `Light::CalculateAABB()`.

When no node is selected (or the previously-selected pointer is no longer reachable from `Scene::Active->GetRootNode()`), the window SHALL render the placeholder text `"(no node selected)"` and SHALL clear `g_SelectedSceneNode` if the pointer is dangling.

The window SHALL be visible by default. The `Window → Node Properties` menu item SHALL toggle its visibility. `Editor::SetWindowVisible("NodeProperties", …)` and `Editor::GetWindowVisible("NodeProperties")` SHALL accept the canonical name `"NodeProperties"`.

The window SHALL be docked to the right region (~20% of viewport width) of the default dock layout (see the modified default-layout requirement). It SHALL be floating only if the user undocks it manually.

#### Scenario: Editing local position updates the live scene

- **WHEN** the user selects a node and drags the `Local Position X` field from `0.0` to `5.0`
- **THEN** `SceneNode::GetLocalPosition().x` returns `5.0` on the next frame
- **AND** `SceneNode::GetWorldPosition()` reflects the new position (Recompose was called)
- **AND** no file on disk is modified

#### Scenario: Editing rotation uses Euler degrees in the UI

- **WHEN** the user opens the Node Properties window for a node and reads the `Local Rotation` widget
- **THEN** the three values shown are Euler angles in degrees (not raw quaternion components)
- **WHEN** the user enters `90.0` in the Y field
- **THEN** the node's local quaternion is updated via `MathUtil::EulerRadToQuat(rad_x, rad_y, rad_z)` with the Y component converted to radians

#### Scenario: Editing light color updates the rendered scene

- **WHEN** a node with a Light component is selected
- **AND** the user picks a new color via the `Color` widget
- **THEN** `Light::GetColor()` returns the new color on the next frame
- **AND** the rendered viewport reflects the lighting change immediately

#### Scenario: Light type change triggers AABB recalculation

- **WHEN** the selected node's Light has type `POINT`
- **AND** the user picks `SPOT` from the `Type` combo
- **THEN** `Light::GetType()` returns `SPOT` on the next frame
- **AND** `Light::CalculateAABB()` is invoked exactly once for that change

#### Scenario: Selection cleared when the underlying node is removed

- **WHEN** the user has a node selected and the scene is cleared via `File → New`
- **THEN** the Node Properties window renders `"(no node selected)"`
- **AND** `Editor::GetSelectedSceneNode()` returns nullptr

#### Scenario: No node selected renders the placeholder

- **WHEN** the editor starts up with a scene loaded but no node clicked
- **AND** the Node Properties window is visible
- **THEN** the window contents are the placeholder text `"(no node selected)"`

#### Scenario: Toggle via Window menu

- **WHEN** the user clicks `Window → Node Properties`
- **THEN** the window's visibility flips
- **AND** the bullet/checkmark glyph next to the menu item reflects the new state on the next frame

### Requirement: The editor SHALL provide a File → Save menu item that writes in-place to the current native scene

The editor SHALL render a `File → Save` menu item between `Import ▸` and `Save As…`. Its behavior SHALL be:

- If a current scene path is tracked AND `is_native` is true AND `Editor::SceneIO::on_save` is registered: invoke `on_save(current_path)` directly. No modal opens.
- Otherwise (no path tracked, non-native source like `.gltf` / `.fbx`, or `on_save` not registered): open the existing Save As… modal so the user picks a writable destination.

The current scene path is tracked via `Editor::SetCurrentScene(path, is_native)`. `is_native` SHALL be true for `.json` / `.bin` and false for any other extension. Paths are absolute filesystem paths.

The Save menu item SHALL display the keyboard shortcut text appropriate for the platform (`"Cmd+S"` on macOS, `"Ctrl+S"` elsewhere) via the third argument to `ImGui::MenuItem`.

The menu item SHALL be disabled (greyed out) when `Scene::Active == nullptr`.

The Save As… menu item SHALL be augmented to display its shortcut (`"Cmd+Shift+S"` / `"Ctrl+Shift+S"`).

#### Scenario: Save writes to the open .json scene in place

- **WHEN** Editor.lua opens `Resource/Scene/scene.json`
- **AND** Editor.lua calls `Editor.SetCurrentScene("/abs/Resource/Scene/scene.json", true)`
- **AND** the user clicks `File → Save`
- **THEN** the registered `on_save` callback is invoked with `/abs/Resource/Scene/scene.json`
- **AND** the file on disk reflects any property edits made since the last save
- **AND** no Save As modal is opened

#### Scenario: Save on an imported FBX falls through to Save As

- **WHEN** Editor.lua opens `Resource/Scene/tank.fbx`
- **AND** Editor.lua calls `Editor.SetCurrentScene("/abs/Resource/Scene/tank.fbx", false)`
- **AND** the user clicks `File → Save`
- **THEN** the Save As… modal opens with the default filename `scene_saved.json`
- **AND** `on_save` is NOT invoked

#### Scenario: Save on a fresh scene falls through to Save As

- **WHEN** the editor has just started, no scene has been opened, and `File → New` has been used (or no current scene is tracked)
- **AND** the user clicks `File → Save`
- **THEN** the Save As… modal opens
- **AND** `on_save` is NOT invoked

#### Scenario: Save is disabled with no active scene

- **WHEN** `Scene::Active` is null
- **THEN** the `File → Save` menu item is rendered as disabled (`ImGui::MenuItem(... false)`)
- **AND** clicking it has no effect

### Requirement: The editor SHALL bind keyboard shortcuts to file menu actions and display them in menu labels

When `WITH_EDITOR` is enabled, the editor SHALL bind the following shortcuts via `ImGui::Shortcut(... ImGuiInputFlags_RouteGlobal)` once per `Editor::Tick`. The modifier `ImGuiMod_Ctrl` SHALL be used (which ImGui maps to Cmd on macOS when `ConfigMacOSXBehaviors` is true, default).

| Action | Shortcut | Display (macOS / other) |
| --- | --- | --- |
| File → New | `Ctrl+N` | `Cmd+N` / `Ctrl+N` |
| File → Open… | `Ctrl+O` | `Cmd+O` / `Ctrl+O` |
| File → Import… | `Ctrl+Shift+I` | `Cmd+Shift+I` / `Ctrl+Shift+I` |
| File → Save | `Ctrl+S` | `Cmd+S` / `Ctrl+S` |
| File → Save As… | `Ctrl+Shift+S` | `Cmd+Shift+S` / `Ctrl+Shift+S` |
| File → Quit | `Ctrl+Q` | `Cmd+Q` / `Ctrl+Q` |

Each `ImGui::MenuItem` for the corresponding action SHALL pass the platform-appropriate shortcut string as its `shortcut` argument so it renders right-aligned in the menu.

The `Ctrl+O` and `Ctrl+Shift+I` shortcuts SHALL open the new Open / Import modals (described below). They SHALL NOT operate on the existing `Open ▸` / `Import ▸` submenus' first entry or otherwise auto-pick a file.

The `Ctrl+Q` shortcut SHALL invoke `Gui::CloseWindow()`. It SHALL NOT attempt to override the OS-level Cmd+Q on macOS — the OS still owns the global keystroke; this binding only fires while the engine window has keyboard focus.

Shortcut routing SHALL happen unconditionally near the top of `Editor::Tick`, before any window or modal renders, so shortcuts work whether the menu bar is open or not.

#### Scenario: Ctrl+S triggers Save behavior

- **WHEN** the editor has a native scene tracked
- **AND** the user presses `Ctrl+S` (or `Cmd+S` on macOS) while the engine window has focus
- **THEN** `Editor::SceneIO::on_save(current_path)` is invoked
- **AND** the menu does not need to be open

#### Scenario: Ctrl+Shift+S opens the Save As modal

- **WHEN** the user presses `Ctrl+Shift+S` (or `Cmd+Shift+S` on macOS)
- **THEN** the existing Save As modal opens
- **AND** subsequent behavior matches `File → Save As…`

#### Scenario: Ctrl+O opens the Open modal

- **WHEN** the user presses `Ctrl+O` (or `Cmd+O` on macOS)
- **THEN** the new Open modal opens, listing the same files as the `File → Open ▸` submenu

#### Scenario: Ctrl+Q closes the engine window

- **WHEN** the user presses `Ctrl+Q` (or `Cmd+Q` on macOS) while the engine window is focused
- **THEN** `Gui::CloseWindow()` is invoked
- **AND** the engine begins shutdown

#### Scenario: Menu items display platform-appropriate shortcut text

- **WHEN** the engine runs on macOS with `ImGui::GetIO().ConfigMacOSXBehaviors == true`
- **AND** the user opens the `File` menu
- **THEN** the `Save` item displays `Cmd+S` right-aligned
- **AND** the `Save As…` item displays `Cmd+Shift+S`

- **WHEN** the engine runs on Windows / Linux
- **AND** the user opens the `File` menu
- **THEN** the same items display `Ctrl+S` and `Ctrl+Shift+S` respectively

#### Scenario: Shortcut while typing in a text field is captured by ImGui

- **WHEN** the Save As modal is open and the user is typing in the filename `InputText`
- **AND** the user presses `Cmd+S`
- **THEN** the shortcut does not fire (ImGui's text input has focus priority)
- **AND** the text field continues to receive keystrokes

### Requirement: The editor SHALL provide Open and Import modals reachable by keyboard shortcut

The editor SHALL render two new modals — `Open Scene` and `Import Scene` — opened by `Ctrl+O` and `Ctrl+Shift+I` respectively. Each modal SHALL contain:

1. A header row: `ImGui::TextDisabled` showing the directory returned by `Editor::GetSceneDir()`.
2. A scrollable selectable list (rendered inside `ImGui::BeginChild`, fixed height ~240px) of files returned by `g_SceneIO.list_files()`. Each entry uses an `ImGui::Selectable` with the filename as label. A double-click on an entry confirms the modal.
3. Bottom buttons: `Open` (or `Import`), `Cancel`. The `Open` / `Import` button SHALL be disabled when no entry is highlighted.

On confirm, the modal SHALL invoke `g_SceneIO.on_open(filename)` (or `on_import(filename)`) with the highlighted entry, then close. On cancel or Escape, the modal SHALL close without invoking any callback.

The submenus `File → Open ▸` and `File → Import ▸` SHALL continue to exist and behave as today (mouse-friendly hover-to-pick path); the modals are an additional access path opened by the shortcuts.

#### Scenario: Ctrl+O opens a list of scene files

- **WHEN** the user presses `Ctrl+O` and `Resource/Scene/` contains `scene.bin`, `scene.json`, `tank.fbx`
- **THEN** the Open Scene modal opens
- **AND** all three files are listed as selectables
- **AND** the directory path is shown as a disabled header

#### Scenario: Selecting a file and clicking Open invokes on_open

- **WHEN** the Open Scene modal is open and the user clicks `scene.bin` then clicks `Open`
- **THEN** `g_SceneIO.on_open("scene.bin")` is invoked
- **AND** the modal closes

#### Scenario: Double-click confirms the selection

- **WHEN** the Open Scene modal is open and the user double-clicks `scene.json`
- **THEN** `g_SceneIO.on_open("scene.json")` is invoked
- **AND** the modal closes

#### Scenario: Cancel dismisses without callback

- **WHEN** the Open Scene modal is open and the user clicks `Cancel`
- **THEN** no callback is invoked
- **AND** the modal closes

#### Scenario: Import shortcut opens the import modal

- **WHEN** the user presses `Ctrl+Shift+I` and confirms a selection
- **THEN** `g_SceneIO.on_import(filename)` is invoked (not `on_open`)

### Requirement: The editor SHALL track the current scene path and native flag for Save routing

The `Editor::SceneIO` struct SHALL gain an additional field:

```cpp
std::function<void(const std::string&)> on_save;
```

Two new functions SHALL be exposed in the `Editor` namespace:

```cpp
void Editor::SetCurrentScene(const std::string& path, bool is_native);
void Editor::ClearCurrentScene();
```

The editor SHALL store `g_CurrentScenePath` (`std::string`) and `g_CurrentSceneIsNative` (`bool`) as internal state. `SetCurrentScene` SHALL overwrite both. `ClearCurrentScene` SHALL set the path to empty and the native flag to false.

`Editor::GetCurrentScenePath()` SHALL return the tracked path (or empty string if none).

When `ClearSceneIO` is called (e.g., during `Shutdown`), the on_save callback SHALL be cleared along with the rest of the SceneIO struct.

The Lua surface SHALL expose:
- `Editor.SetCurrentScene(path, is_native)` — accepts a string path and a boolean.
- `Editor.SetSceneIO({…, on_save = function(path) … end})` — the new optional `on_save` field.

#### Scenario: SetCurrentScene records path and native flag

- **WHEN** Lua calls `Editor.SetCurrentScene("/abs/Resource/Scene/scene.bin", true)`
- **THEN** subsequent `Editor::GetCurrentScenePath()` returns `/abs/Resource/Scene/scene.bin`
- **AND** internal `g_CurrentSceneIsNative` is `true`

#### Scenario: ClearCurrentScene resets state

- **WHEN** a scene path is tracked and the user invokes `File → New`
- **AND** Editor.lua's `on_new` callback calls `Editor.ClearCurrentScene()` (or sets it to empty/false)
- **THEN** subsequent Save invocations route through the Save As modal
- **AND** `Editor::GetCurrentScenePath()` returns ""

#### Scenario: on_save SceneIO field is invoked by Save

- **WHEN** Editor.lua calls `Editor.SetSceneIO({on_save = function(p) saved = p end, …})`
- **AND** the editor has a native scene tracked at `/abs/x.json`
- **AND** the user invokes File → Save
- **THEN** the Lua global `saved` equals `/abs/x.json`

### Requirement: The editor SHALL render an in-viewport TRS gizmo on the selected SceneNode

When `WITH_EDITOR` is enabled and `Editor::GetSelectedSceneNode()` is non-null, the editor SHALL render an ImGuizmo-driven TRS (translate / rotate / scale) gizmo over the selected node's world position. The gizmo SHALL be drawn into ImGui's draw lists during `Editor::Tick` (before `Gui::Render`), inside an invisible ImGui window whose rect matches the **Viewport window's content rect** (the same rect into which the 3D pipeline renders via the Viewport window's render target — see editor-viewport-window spec). `ImGuizmo::SetRect` SHALL be called with the Viewport window's content-rect min and size, NOT the full SFML window's pos/size.

When the Viewport window is hidden, collapsed, or has a zero-size content rect, the gizmo SHALL NOT be rendered (skip entirely that frame).

The gizmo SHALL receive:
- `view = inverse(camera_node->GetWorldMatrix())` — the active pipeline camera's view matrix.
- `projection = camera->GetProjectionMatrix()` — already in OpenGL column-major layout via `Matrix4::Raw[16]`, and computed with the aspect derived from the Viewport window's content rect (see editor-viewport-window spec).
- `matrix = node->GetWorldMatrix()` — the selected node's world transform.
- `operation` — TRANSLATE, ROTATE, or SCALE per `g_GizmoOp`.
- `mode` — `ImGuizmo::WORLD` unconditionally in v1 (see "gizmo controls" requirement below for the rationale).
- `snap` — `&snap_value` when `g_SnapEnabled == true` (snap_value = `g_SnapTranslate` for TRANSLATE, `g_SnapRotate` for ROTATE, `g_SnapScale` for SCALE), else `nullptr`.

When the gizmo reports a change (`ImGuizmo::IsUsing()`), the editor SHALL:

1. Compute the new local matrix as `Ml = inverse(Mp) * Mw`, where `Mp` is the selected node's parent's world matrix (identity when no parent) and `Mw` is the gizmo-modified world matrix.
2. Decompose `Ml` into translation `t`, rotation `r` (Quaternion), scale `s` (Vector4).
3. Write back ONLY the components that the current operation affects:
   - TRANSLATE → `node->SetLocalPosition(t)`.
   - ROTATE → `node->SetLocalRoattion(r)`.
   - SCALE → `node->SetLocalScale(s)`.
4. Call `node->Recompose(false)` so world transforms re-derive on the next frame.

The gizmo SHALL be hidden (skip rendering) when:
- `Editor::GetSelectedSceneNode() == nullptr`, OR
- `Pipeline::Active == nullptr`, OR
- `Pipeline::Active->GetCurrentCamera() == nullptr`, OR
- the Viewport window is hidden, collapsed, or has a zero-size content rect.

#### Scenario: Gizmo appears on the selected node inside the Viewport window

- **WHEN** the user clicks a node in the Scene Inspector (or true-clicks it in the Viewport window)
- **AND** the engine has an active pipeline + camera
- **AND** the Viewport window is visible
- **THEN** an ImGuizmo TRS gizmo is rendered at the node's world position on the next frame, positioned inside the Viewport window's content rect

#### Scenario: Gizmo follows the selected node when its transform changes

- **WHEN** a node is selected and the user edits its `Local Position X` to `5.0` in the Node Properties panel
- **THEN** the gizmo reanchors to the new world position on the next frame

#### Scenario: Translate drag updates the local position only

- **WHEN** the gizmo mode is TRANSLATE and the user drags the gizmo's X handle by 2 world units
- **THEN** `node->GetLocalPosition()` reflects the new position
- **AND** `node->GetLocalRoattion()` is unchanged
- **AND** `node->GetLocalScale()` is unchanged

#### Scenario: Rotate drag updates only the rotation

- **WHEN** the gizmo mode is ROTATE and the user rotates 45 degrees around the Y axis
- **THEN** `node->GetLocalRoattion()` reflects the new rotation
- **AND** `node->GetLocalPosition()` is unchanged
- **AND** `node->GetLocalScale()` is unchanged

#### Scenario: Drag a child node — only local transform writes

- **WHEN** a node has a non-identity parent transform
- **AND** the user drags the gizmo (in WORLD space) to a new world position
- **THEN** the new local position is computed as `inverse(parent_world) * new_world`
- **AND** the parent's world transform is NOT modified

#### Scenario: Snap toggle quantizes drag deltas

- **WHEN** `g_SnapEnabled == true` with `g_SnapTranslate == 1.0`
- **AND** the user drags the translate gizmo
- **THEN** the resulting local position changes only in 1.0-unit increments

#### Scenario: Gizmo hidden when no node is selected

- **WHEN** `Editor::GetSelectedSceneNode() == nullptr`
- **THEN** no ImGuizmo manipulator is drawn on the next frame

#### Scenario: Gizmo hidden when the Viewport window is hidden

- **WHEN** the Viewport window is hidden (visibility off) or collapsed
- **THEN** no ImGuizmo manipulator is drawn on the next frame, even if a node is selected

#### Scenario: Gizmo only interacts inside the Viewport window

- **WHEN** the user moves the cursor over a docked panel (Scene Inspector / Console / etc.) that is not the Viewport window
- **THEN** ImGuizmo's hover state for the gizmo is false
- **AND** clicking the panel does not start a gizmo drag

### Requirement: The editor SHALL maintain gizmo mode, space, and snap state with persistence

The editor SHALL maintain the following editor-global state:

```cpp
GizmoMode  g_GizmoMode  = GizmoMode::Translate;   // Translate | Rotate | Scale
GizmoSpace g_GizmoSpace = GizmoSpace::World;      // Local | World
bool  g_SnapEnabled  = false;
float g_SnapTranslate = 1.0f;
float g_SnapRotate    = 15.0f;   // degrees
float g_SnapScale     = 0.1f;
```

The state SHALL be persisted via the existing FuryEditor `imgui.ini` settings handler (a single line `Gizmo=mode,space,snap_enabled,snap_t,snap_r,snap_s` appended to the `[FuryEditor][Editor]` block). On startup the persisted values SHALL be applied before any window renders.

The editor SHALL expose:

```cpp
void Editor::SetGizmoMode(const char* name);   // "translate" | "rotate" | "scale"
void Editor::SetGizmoSpace(const char* name);  // "local" | "world"
void Editor::SetSnapEnabled(bool enabled);
const char* Editor::GetGizmoMode();
const char* Editor::GetGizmoSpace();
bool        Editor::GetSnapEnabled();
```

Unknown name strings SHALL be silently ignored (no exception, no log entry — same convention as `Editor::SetWindowVisible`).

The Lua bindings SHALL expose `Editor.SetGizmoMode(name)`, `Editor.SetGizmoSpace(name)`, `Editor.SetSnapEnabled(bool)`. These are optional — Editor.lua does NOT require updates for v1; the C++ defaults plus user UI suffice.

#### Scenario: Mode change is reflected by the gizmo on the next frame

- **WHEN** `Editor::SetGizmoMode("rotate")` is invoked
- **THEN** the next frame's gizmo renders rotation handles instead of translate handles

#### Scenario: Mode persists across editor restarts

- **WHEN** the user picks ROTATE from the Node Properties panel and closes the editor
- **AND** the editor is launched again from the same working directory
- **THEN** the gizmo starts in ROTATE mode

#### Scenario: Snap state persists across editor restarts

- **WHEN** the user enables snap with translate = 0.5 and closes the editor
- **AND** the editor is launched again
- **THEN** snap is still enabled with translate = 0.5

#### Scenario: Unknown mode name is ignored

- **WHEN** `Editor::SetGizmoMode("rocket")` is invoked
- **THEN** `g_GizmoMode` is unchanged
- **AND** no log entry is emitted

### Requirement: The Viewport top toolbar SHALL host the gizmo mode + snap controls

The gizmo mode + snap controls SHALL render in the Viewport window's top toolbar (see the `editor-viewport-window` capability), NOT in the Node Properties window. The Node Properties window SHALL NOT render a "Gizmo" section. The toolbar's left group SHALL contain:

1. A 3-button row (Translate / Rotate / Scale) implemented as `ImGui::RadioButton` driven by `g_GizmoOp`.
2. A single `Snap` checkbox driven by `g_SnapEnabled`. The snap-step sizes (`g_SnapTranslate`, `g_SnapRotate`, `g_SnapScale`) SHALL NOT live in the toolbar — they SHALL render as three `DragFloat` widgets in the Settings → Editor section instead (visible regardless of the Snap toggle state).

The gizmo SHALL always operate in world space. The Local/World concept is NOT surfaced as a toolbar toggle — surfacing it produced confusing UX (SCALE silently forced LOCAL; LOCAL TRANSLATE/ROTATE drags along the node's rotated axes which most users don't expect by default). The persisted `g_GizmoSpace` value is retained for forward-compat with imgui.ini files and is exposed through `Editor::SetGizmoSpace` for scripts that want to opt in.

The "Node" section of the Node Properties window SHALL render a Local / World radio at its top that switches the position/rotation/scale read-out:

- **Local** (default) — the position/rotation/scale widgets are bound to `node->GetLocal*` / `node->SetLocal*` (the canonical state).
- **World** — the position/rotation/scale widgets are bound to `node->GetWorld*` and rendered read-only (`ImGui::BeginDisabled` / `EndDisabled`). Editing world transforms when a parent has non-uniform scale produces shear that the local TRS slot cannot represent, so the cleanest UX is to expose World as inspect-only.

The toolbar's gizmo group SHALL render whenever the Viewport window is visible, independent of selection (the gizmo doesn't appear without a selection, but the user can still configure mode / snap ahead of selecting). The Node section's Local/World radio MAY only render when a node is selected — it edits node-bound state.

#### Scenario: Mode buttons reflect and update state

- **WHEN** the user clicks the `Rotate` radio button in the Viewport toolbar
- **THEN** `g_GizmoOp == ImGuizmo::ROTATE` on the next frame
- **AND** the active gizmo (if a node is selected) renders rotation handles

#### Scenario: Snap-step widgets live in Settings

- **WHEN** the user opens Settings → Editor
- **THEN** three snap-step `DragFloat` widgets (Translate / Rotate / Scale Step) render in the section
- **AND** editing them updates `g_SnapTranslate` / `g_SnapRotate` / `g_SnapScale` for subsequent gizmo drags

#### Scenario: Gizmo mode persists across selections

- **WHEN** the user selects node A, picks ROTATE in the toolbar, then selects node B
- **THEN** the toolbar still shows ROTATE
- **AND** the gizmo on node B is in ROTATE mode

#### Scenario: Node Properties has no Gizmo section

- **WHEN** the Node Properties window renders with any selection state
- **THEN** no "Gizmo" `CollapsingHeader` is present

#### Scenario: Node section Local/World toggle switches the readout

- **WHEN** a node is selected with `Local` chosen in the Node section
- **THEN** the position/rotation/scale widgets show `node->GetLocal*` and edits commit through `SetLocal*`

### Requirement: The Content Browser SHALL filter its tile grid by asset type and fuzzy name search

The Content Browser SHALL render a filter toolbar row above the tile grid containing:

- A **type filter** combo with one entry per asset category the browser enumerates (`All`, `Mesh`, `Material`, `Texture`, `AnimationClip`). Selecting a category SHALL restrict the grid to tiles of that type; `All` SHALL show every category.
- A **text filter** input with a hint (`Search…`). While non-empty, the grid SHALL be restricted to tiles whose asset name matches the text by case-insensitive subsequence (fuzzy) match — every character of the filter text appears in the name in order, not necessarily contiguously.

The two filters SHALL compose (a tile must pass both). Filter state SHALL be window-local (no persistence across editor sessions). When the active filters match no assets, the grid area SHALL show the same `"(no assets in active scene)"` placeholder used for an empty scene. Filtering SHALL NOT affect selection state of hidden tiles and SHALL NOT re-order the grid (name sort is preserved).

#### Scenario: Type filter narrows the grid

- **GIVEN** a scene with 3 meshes and 5 materials
- **WHEN** the user picks `Material` in the type filter
- **THEN** exactly 5 tiles render and none of them is a Mesh tile

#### Scenario: Fuzzy text matches non-contiguous subsequences

- **GIVEN** a mesh named `Sponza_Atrium`
- **WHEN** the user types `spz` in the search box
- **THEN** the `Sponza_Atrium` tile remains visible

#### Scenario: Filters compose

- **GIVEN** meshes `Rock_A`, `Rock_B` and a material `Rock_Mat`
- **WHEN** the type filter is `Mesh` and the search text is `rock`
- **THEN** only `Rock_A` and `Rock_B` render

#### Scenario: No matches shows the placeholder

- **WHEN** the search text matches no asset in the active scene
- **THEN** the grid area shows `"(no assets in active scene)"`
