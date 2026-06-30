## MODIFIED Requirements

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

#### Scenario: Window menu includes Viewport toggle

- **WHEN** the user opens the `Window` menu
- **THEN** `Viewport` appears at the top of the built-in-window list
- **AND** clicking it toggles the Viewport window's visibility
- **AND** a checkmark / bullet glyph next to `Viewport` reflects the visible state

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
