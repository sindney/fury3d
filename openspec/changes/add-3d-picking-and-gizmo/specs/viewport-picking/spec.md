## ADDED Requirements

### Requirement: The editor SHALL render an offscreen R32UI ID texture to resolve viewport clicks

When `WITH_EDITOR` is enabled, the editor SHALL maintain an offscreen framebuffer with one `R32UI` color attachment and one `DEPTH24` attachment, sized to the engine's drawable area. The framebuffer SHALL be allocated lazily on the first pick request and SHALL be re-created when the drawable size changes between pick requests. When no pick is in flight, no per-frame GPU cost SHALL be incurred (the FBO sits idle).

The editor SHALL maintain a per-pick `id_table: std::vector<std::weak_ptr<SceneNode>>` rebuilt at the start of each pick frame from the active scene's renderable nodes. Picking IDs are 1-indexed (`array_index + 1`). ID `0` is reserved as the "no hit" sentinel and SHALL be the cleared color of the R32UI attachment.

The editor SHALL implement a picking-only shader pair (`id_pass.vs` / `id_pass.fs`) that takes per-draw uniforms `view_matrix`, `projection_matrix`, `world_matrix`, and `node_id (uint)`, and writes `node_id` unchanged to the R32UI color attachment. A skinned variant (`id_pass_skinned.vs`) SHALL be provided, mirroring the existing skinned-mesh shader's bone-matrix bind path.

When the user left-clicks inside the central viewport region (see editor-shell modified requirement), the editor SHALL:

1. Capture the cursor position relative to the central node's rect.
2. Mark its internal `pick_state = RenderRequested`.

On the next `Editor::TickPostRender` call where `pick_state == RenderRequested`, the editor SHALL:

1. Resize / create the picking FBO if needed.
2. Bind the picking FBO. Clear color to `0` and clear depth.
3. Rebuild `id_table` from `Scene::Active->GetSceneManager()`'s renderable nodes (or an equivalent walk).
4. For each entry, bind the id-pass shader, set `node_id = id_table_index + 1`, set `world_matrix = node->GetWorldMatrix()`, set the camera's view + projection matrices, and draw the node's mesh (skinned variant when applicable).
5. Issue a 1×1 `glReadPixels(GL_RED_INTEGER, GL_UNSIGNED_INT)` at the captured cursor position into a staging buffer.
6. Set `pick_state = AwaitingReadback`.

On the next `Editor::TickPostRender` call where `pick_state == AwaitingReadback`, the editor SHALL:

1. Read the staging buffer's uint value.
2. If the value is `0`: set `g_SelectedSceneNode = nullptr`.
3. Else if the value (1-indexed) maps to a valid `id_table` slot AND the `weak_ptr` still locks to a non-null `SceneNode`: set `g_SelectedSceneNode = locked.get()`.
4. Else: leave `g_SelectedSceneNode` unchanged (the picked node was destroyed between request and readback — silently no-op).
5. Set `pick_state = Idle`.

The picking pass SHALL be skipped when `Scene::Active == nullptr` or `Pipeline::Active == nullptr` or `Pipeline::Active->GetCurrentCamera() == nullptr`. In those cases the pick request SHALL be discarded (`pick_state = Idle`) and `g_SelectedSceneNode` SHALL remain unchanged.

#### Scenario: Steady-state has zero picking cost

- **WHEN** the editor is running and no clicks occur
- **THEN** the picking FBO is not bound during the frame
- **AND** the picking shader pair does not bind
- **AND** `glReadPixels` is not called

#### Scenario: Click on a renderable node selects it

- **WHEN** the active scene contains a renderable node `tank` with a non-zero world AABB
- **AND** the user left-clicks at a screen pixel that lies inside the projected silhouette of `tank`
- **THEN** within two frames `Editor::GetSelectedSceneNode()` returns the `SceneNode*` for `tank`
- **AND** the Scene Inspector's selection highlight reflects the new selection
- **AND** the Node Properties panel reflects the new selection

#### Scenario: Click on empty viewport space deselects

- **WHEN** the user left-clicks at a screen pixel that doesn't intersect any renderable node
- **THEN** within two frames `Editor::GetSelectedSceneNode()` returns nullptr
- **AND** the Scene Inspector renders no row as selected
- **AND** the Node Properties panel renders the `(no node selected)` placeholder

#### Scenario: Click outside the central viewport region is ignored by picking

- **WHEN** the user left-clicks inside the Settings window (a docked panel)
- **THEN** no picking pass is scheduled
- **AND** `g_SelectedSceneNode` is unchanged

#### Scenario: Picking respects depth — front-most node wins

- **WHEN** two renderable nodes overlap in screen space, with `front` closer to the camera than `back`
- **AND** the user clicks at a pixel where both nodes' silhouettes overlap
- **THEN** `Editor::GetSelectedSceneNode()` returns `front`

#### Scenario: Picking handles dangling references

- **WHEN** a pick request is in flight (`pick_state == AwaitingReadback`)
- **AND** the resolved id maps to a node whose `weak_ptr` no longer locks (the node was destroyed)
- **THEN** `g_SelectedSceneNode` is left unchanged
- **AND** the editor logs no error

#### Scenario: Window resize between picks

- **WHEN** a pick has completed and the engine window is then resized
- **AND** a new click occurs after the resize
- **THEN** the picking FBO is recreated at the new drawable size before the next pick render
- **AND** the new pick produces a correct selection

#### Scenario: Skinned-mesh picking

- **WHEN** the active scene contains a renderable skinned mesh and the user clicks on its rendered silhouette
- **THEN** the id-pass uses the skinned variant shader with the same bone-matrix bind layout as the forward pass
- **AND** the click resolves to the skinned-mesh's SceneNode

### Requirement: The editor SHALL expose `Editor::TickPostRender` to drive picking after the main pipeline draws

The `Editor` namespace SHALL gain a public `void Editor::TickPostRender();` function. The engine's main loop (`engine/Fury/Engine.cpp`) SHALL invoke it once per frame after `Pipeline::Active->Execute(...)` and before `Gui::Render()`.

When `WITH_EDITOR` is undefined, `Editor::TickPostRender` SHALL be an inline no-op. The engine SHALL still call it unconditionally — the no-op stub provides the link-time stability.

`TickPostRender` is responsible for:
- Advancing the picking-pass state machine (`Idle → RenderRequested → AwaitingReadback → Idle`).
- Running the id-pass and the readback when state demands it.
- NOTHING else — gizmo rendering happens in the existing `Editor::Tick` (which runs before user pipeline) so it can submit ImGui draw lists.

#### Scenario: TickPostRender runs after the user pipeline

- **WHEN** the engine renders one frame
- **THEN** the call order is: `Gui::NewFrame` → `Editor::Tick` → user `on_update` (which calls `Pipeline::Execute`) → `Editor::TickPostRender` → `Gui::Render`
- **AND** the picking FBO bind / id-pass / readback all execute inside `TickPostRender`

#### Scenario: TickPostRender is a safe no-op when no pick is requested

- **WHEN** `pick_state == Idle`
- **THEN** `TickPostRender` performs no GL state changes, no FBO bind, no readback
- **AND** returns within microseconds

#### Scenario: TickPostRender stub when WITH_EDITOR=OFF

- **WHEN** the engine is built with `WITH_EDITOR=OFF`
- **THEN** `Editor::TickPostRender` is an inline no-op function
- **AND** the engine builds and links cleanly without referring to `EditorPicking.cpp`
