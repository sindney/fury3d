## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: The editor SHALL render a top-level dockspace with default layout

When `WITH_EDITOR` is enabled, every frame the engine SHALL invoke `Editor::Tick()` which SHALL render an ImGui DockSpace covering the entire main viewport (excluding the menu bar). The dockspace ID SHALL be stable across frames so docked windows persist.

On first run (no `imgui.ini` present, or after the user invokes `Window → Reset Layout`), the editor SHALL build a default dock layout via `ImGui::DockBuilder*` APIs:

- A LEFT region (~20% of width) hosting the **Scene Inspector** window.
- A RIGHT region (~20% of width) hosting the **Node Properties** window.
- A BOTTOM region (~30% of height of the central area, between left and right) hosting the **Console** and **Content Browser** windows in the same tab group.
- The remaining CENTER region is the un-docked viewport area (no built-in window docks here — the 3D scene renders behind the dockspace).
- The **Settings** and **Profiler** windows SHALL NOT be pre-docked; they SHALL appear floating when first toggled visible.

The split order SHALL be: left first, right second, bottom third — so the bottom region spans the central area without overlapping the side panels.

The dockspace SHALL use `ImGuiDockNodeFlags_PassthruCentralNode` so the 3D scene rendered by `Pipeline::Execute` is visible through the empty center region.

#### Scenario: Default layout on first run

- **WHEN** the engine starts with `WITH_EDITOR=ON` and no `imgui.ini` exists in the working directory
- **THEN** the Scene Inspector window is docked along the left edge
- **AND** the Node Properties window is docked along the right edge
- **AND** the Console and Content Browser windows are tabbed together along the bottom (between the two side panels)
- **AND** the central area is transparent so the rendered 3D scene is visible

#### Scenario: Reset Layout restores defaults

- **WHEN** the user has rearranged windows and clicks `Window → Reset Layout`
- **THEN** the dockspace is rebuilt with the default left/right/bottom split
- **AND** Scene Inspector, Node Properties, Console, Content Browser snap back to their default docks
- **AND** Settings and Profiler windows close (visibility flags cleared)

#### Scenario: Layout persists across restarts

- **WHEN** the user docks a previously-floating window and closes the engine
- **AND** the engine is launched again from the same working directory
- **THEN** the window opens in the position it was docked

#### Scenario: Pre-existing imgui.ini without Node Properties dock

- **WHEN** the engine starts with `WITH_EDITOR=ON` and an `imgui.ini` from a prior version that has no entry for `Node Properties`
- **THEN** the editor does not auto-rebuild the layout (preserves the user's other dock arrangement)
- **AND** the Node Properties window opens floating until the user clicks `Window → Reset Layout` or docks it manually

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
- `Editor.SetWindowVisible(name, bool)` / `Editor.GetWindowVisible(name)` — programmatic control of the built-in window visibility flags. Valid names: `"Profiler"`, `"SceneInspector"`, `"NodeProperties"`, `"Console"`, `"ContentBrowser"`, `"Settings"`.

When `WITH_EDITOR` is not defined, the `Editor` table SHALL still exist but every function SHALL be a safe no-op (so user scripts compose with both builds).

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

- **WHEN** the engine is built with `WITH_EDITOR=OFF` and Editor.lua calls `Editor.SetSceneIO(...)` and `Editor.SetCurrentScene(...)` and `Editor.Log(...)`
- **THEN** none of the calls throws or logs an error
- **AND** subsequent script logic continues normally
