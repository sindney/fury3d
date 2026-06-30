## MODIFIED Requirements

### Requirement: The editor SHALL render an offscreen R32UI ID texture to resolve viewport clicks

When `WITH_EDITOR` is enabled, the editor SHALL maintain an offscreen framebuffer with one `R32UI` color attachment and one `DEPTH24` attachment, sized to the **Viewport window's content rect** (see editor-viewport-window spec). The framebuffer SHALL be allocated lazily on the first pick request and SHALL be re-created when the Viewport window's content-rect size changes between pick requests. When no pick is in flight, no per-frame GPU cost SHALL be incurred (the FBO sits idle).

The editor SHALL maintain a per-pick `id_table: std::vector<std::weak_ptr<SceneNode>>` rebuilt at the start of each pick frame from the active scene's renderable nodes. Picking IDs are 1-indexed (`array_index + 1`). ID `0` is reserved as the "no hit" sentinel and SHALL be the cleared color of the R32UI attachment.

The editor SHALL implement a picking-only shader pair (`id_pass.vs` / `id_pass.fs`) that takes per-draw uniforms `view_matrix`, `projection_matrix`, `world_matrix`, and `node_id (uint)`, and writes `node_id` unchanged to the R32UI color attachment. A skinned variant (`id_pass_skinned.vs`) SHALL be provided, mirroring the existing skinned-mesh shader's bone-matrix bind path.

A pick SHALL be scheduled only on a **true click** inside the Viewport window's content rect. A true click is defined as a left-mouse-button press and release that occur at (approximately) the same screen position, with no intervening drag. Concretely, the editor SHALL implement a click-vs-drag state machine:

1. On `IsMouseClicked(Left)` inside the Viewport window's content rect (with `!WantCaptureMouse`, no gizmo in use — see editor-viewport-window input-routing requirement): record the press position and frame, and mark a pending-click flag. Do NOT schedule a pick yet.
2. While the left button is held: track the cursor's displacement from the press position. If the displacement exceeds `ImGui::GetIO().MouseDragThreshold` (default 4 px on macOS, 6 px elsewhere) at any point, mark the pending click as "consumed by drag" so no pick will fire on release.
3. On `IsMouseReleased(Left)`: if the pending click was NOT consumed by drag AND the release position is within `MouseDragThreshold` of the press position AND the Viewport window is still hovered: schedule a pick via `RequestPickAt` with the release position. Reset the pending-click state.

If the left button is released and the pending click was consumed by drag, no pick SHALL be scheduled. If the release occurs outside the Viewport window's content rect, no pick SHALL be scheduled.

The pick position captured by `RequestPickAt` SHALL be in **Viewport window content-rect-relative** pixel coordinates (origin = top-left of the Viewport window's content rect). This matches the coordinate space of the picking FBO, which is sized to the Viewport window's content rect because the 3D pipeline renders into the Viewport window's render target (see editor-viewport-window spec). Capturing in any other coordinate space would sample the wrong pixel.

When a pick is scheduled, the editor SHALL mark its internal `pick_state = RenderRequested`.

On the next `Editor::TickPostRender` call where `pick_state == RenderRequested`, the editor SHALL:

1. Resize / create the picking FBO to match the Viewport window's current content-rect size (clamped to ≥1×1).
2. Bind the picking FBO. Clear color to `0` and clear depth.
3. Rebuild `id_table` from `Scene::Active->GetSceneManager()`'s renderable nodes (or an equivalent walk).
4. For each entry, bind the id-pass shader, set `node_id = id_table_index + 1`, set `world_matrix = node->GetWorldMatrix()`, set the camera's view + projection matrices (the same matrices the main pipeline used to render into the Viewport render target this frame — including the viewport-content-rect-derived aspect), and draw the node's mesh (skinned variant when applicable).
5. Issue a 1×1 `glReadPixels(GL_RED_INTEGER, GL_UNSIGNED_INT)` at the captured cursor position (Y-flipped against the Viewport window's content-rect height: `y = content_rect_height - 1 - captured_y`) into a staging buffer.
6. Set `pick_state = AwaitingReadback`.

On the next `Editor::TickPostRender` call where `pick_state == AwaitingReadback`, the editor SHALL:

1. Read the staging buffer's uint value.
2. If the value is `0`: set `g_SelectedSceneNode = nullptr` (deselect).
3. Else if the value (1-indexed) maps to a valid `id_table` slot AND the `weak_ptr` still locks to a non-null `SceneNode`: set `g_SelectedSceneNode = locked.get()`.
4. Else: leave `g_SelectedSceneNode` unchanged (the picked node was destroyed between request and readback — silently no-op).
5. Set `pick_state = Idle`.

Every write to `g_SelectedSceneNode` SHALL go through `Editor::SetSelectedSceneNode(SceneNode*)` so the `OnSelectionChanged` signal fires (see selection-visualization spec).

The picking pass SHALL be skipped when `Scene::Active == nullptr` or `Pipeline::Active == nullptr` or `Pipeline::Active->GetCurrentCamera() == nullptr`, OR when the Viewport window is hidden or has a zero-size content rect. In those cases the pick request SHALL be discarded (`pick_state = Idle`) and `g_SelectedSceneNode` SHALL remain unchanged.

The editor SHALL expose `bool Editor::IsPickInFlight()` returning true while `pick_state != Idle`. This SHALL be exposed to Lua as `Editor.IsPickInFlight()` so `Editor.lua`'s camera-drag can short-circuit while a pick is resolving.

#### Scenario: Steady-state has zero picking cost

- **WHEN** the editor is running and no clicks occur
- **THEN** the picking FBO is not bound during the frame
- **AND** the picking shader pair does not bind
- **AND** `glReadPixels` is not called

#### Scenario: True click on a renderable node selects it

- **WHEN** the active scene contains a renderable node `tank` with a non-zero world AABB
- **AND** the user left-clicks (press + release at the same position, no drag) at a content-rect-relative pixel that lies inside the projected silhouette of `tank` inside the Viewport window
- **THEN** within two frames `Editor::GetSelectedSceneNode()` returns the `SceneNode*` for `tank`
- **AND** the Scene Inspector's selection highlight reflects the new selection
- **AND** the Node Properties panel reflects the new selection

#### Scenario: Drag does not schedule a pick

- **WHEN** the user presses the left mouse button inside the Viewport window's content rect
- **AND** moves the cursor more than `MouseDragThreshold` pixels before releasing (a camera-drag)
- **THEN** no pick is scheduled on release
- **AND** `g_SelectedSceneNode` is unchanged by the drag
- **AND** the camera yaws/pitches per `Editor.lua`'s camera-drag logic

#### Scenario: Click on empty viewport space deselects

- **WHEN** a node is currently selected
- **AND** the user left-clicks (press + release at the same position, no drag) at a content-rect-relative pixel that doesn't intersect any renderable node
- **THEN** within two frames `Editor::GetSelectedSceneNode()` returns nullptr
- **AND** the Scene Inspector renders no row as selected
- **AND** the Node Properties panel renders the `(no node selected)` placeholder
- **AND** `Editor::OnSelectionChanged()` fired with `nullptr`

#### Scenario: Click outside the Viewport window content rect is ignored by picking

- **WHEN** the user left-clicks inside the Settings window (a docked or floating panel)
- **THEN** no picking pass is scheduled
- **AND** `g_SelectedSceneNode` is unchanged

#### Scenario: Release outside the Viewport window does not pick

- **WHEN** the user presses the left mouse button inside the Viewport window's content rect
- **AND** releases it outside the Viewport window (without exceeding the drag threshold)
- **THEN** no pick is scheduled
- **AND** `g_SelectedSceneNode` is unchanged

#### Scenario: Picking respects depth — front-most node wins

- **WHEN** two renderable nodes overlap in screen space, with `front` closer to the camera than `back`
- **AND** the user true-clicks at a pixel where both nodes' silhouettes overlap
- **THEN** `Editor::GetSelectedSceneNode()` returns `front`

#### Scenario: Picking handles dangling references

- **WHEN** a pick request is in flight (`pick_state == AwaitingReadback`)
- **AND** the resolved id maps to a node whose `weak_ptr` no longer locks (the node was destroyed)
- **THEN** `g_SelectedSceneNode` is left unchanged
- **AND** the editor logs no error

#### Scenario: Viewport window resize between picks

- **WHEN** a pick has completed and the Viewport window is then resized
- **AND** a new true click occurs after the resize
- **THEN** the picking FBO is recreated at the new content-rect size before the next pick render
- **AND** the new pick produces a correct selection

#### Scenario: Skinned-mesh picking

- **WHEN** the active scene contains a renderable skinned mesh and the user true-clicks on its rendered silhouette
- **THEN** the id-pass uses the skinned variant shader with the same bone-matrix bind layout as the forward pass
- **AND** the click resolves to the skinned-mesh's SceneNode

#### Scenario: Pick is skipped when the Viewport window is hidden

- **WHEN** the Viewport window is hidden (visibility off)
- **AND** the user left-clicks where the Viewport window would be
- **THEN** no picking pass is scheduled
- **AND** `g_SelectedSceneNode` is unchanged

#### Scenario: Camera-drag short-circuits during in-flight pick

- **WHEN** a true click has scheduled a pick (`pick_state == RenderRequested` or `AwaitingReadback`)
- **AND** `Editor.lua`'s camera-drag gate calls `Editor.IsPickInFlight()`
- **THEN** the camera-drag does not arm on the immediately following LMB-down until the pick resolves
- **AND** the in-flight pick completes and updates selection normally

## ADDED Requirements

### Requirement: The editor SHALL expose `Picking::IsPickInFlight` to Lua

The `Editor` Lua table SHALL gain `Editor.IsPickInFlight()` returning a boolean that is true while the picking state machine is not `Idle` (i.e., a pick render or readback is pending). This lets `Editor.lua`'s camera-drag logic short-circuit while a pick is resolving, as a defense in depth alongside the click-vs-drag gate.

When `WITH_EDITOR` is undefined, `Editor.IsPickInFlight()` SHALL return `false` unconditionally (so user scripts compose with both builds).

#### Scenario: IsPickInFlight is true while a pick is resolving

- **WHEN** the user true-clicks a renderable node and `pick_state` transitions to `RenderRequested`
- **THEN** `Editor.IsPickInFlight()` returns `true` until the readback completes
- **AND** returns `false` on the frame after `pick_state` returns to `Idle`

#### Scenario: IsPickInFlight is false at steady state

- **WHEN** no pick has been requested in the last two frames
- **THEN** `Editor.IsPickInFlight()` returns `false`

#### Scenario: IsPickInFlight is a no-op when WITH_EDITOR=OFF

- **WHEN** the engine is built with `WITH_EDITOR=OFF`
- **AND** Editor.lua calls `Editor.IsPickInFlight()`
- **THEN** the call returns `false` without error
