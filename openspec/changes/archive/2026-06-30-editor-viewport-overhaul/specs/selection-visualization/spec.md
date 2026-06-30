## ADDED Requirements

### Requirement: The editor SHALL draw a wireframe overlay on the currently selected SceneNode

When `WITH_EDITOR` is enabled and `Editor::GetSelectedSceneNode()` returns a non-null `SceneNode*`, the editor SHALL draw a wireframe selection overlay for that node each frame, using the existing `RenderUtil` debug-draw primitives (`DrawBoxBounds` / `DrawMesh` between `BeginDrawMeshs` and `EndDrawMeshes`), rendered with the active pipeline camera. The overlay SHALL be drawn during `Editor::TickPostRender` (after `Pipeline::Execute`, before `Gui::Render`) so it composites over the 3D scene rendered into the Viewport window's render target.

The overlay SHALL NOT be gated by any `PipelineSwitch` flag (it is not part of the Profiler's debug-overlay system); it draws whenever a node is selected, regardless of whether `LIGHT_BOUNDS` / `MESH_BOUNDS` / `OCTREE_BOUNDS` are on.

The overlay SHALL be drawn in a single editor-defined selection color (`kSelectionColor`), distinct from the per-light-color tinting used by the Profiler's `LIGHT_BOUNDS` overlay, so the two overlays are visually distinguishable when both are enabled.

When `Editor::GetSelectedSceneNode()` returns `nullptr`, no overlay SHALL be drawn and no `RenderUtil` draw calls SHALL be issued for selection.

#### Scenario: Selecting a mesh node draws its world AABB

- **WHEN** the user selects a `SceneNode` with a renderable mesh component and a non-empty world AABB
- **THEN** on the next frame a wireframe box matching `node->GetWorldAABB()` is drawn in `kSelectionColor`
- **AND** the box is visible over the mesh in the Viewport window

#### Scenario: Selecting a node with no renderable geometry draws nothing

- **WHEN** the user selects a `SceneNode` that has no mesh and no `Light` component
- **AND** the node's world AABB is empty (zero volume)
- **THEN** no selection overlay is drawn for that node

#### Scenario: Deselecting removes the overlay

- **WHEN** a node was selected and the user clicks empty viewport space (deselecting per the viewport-picking spec)
- **THEN** on the next frame no wireframe selection overlay is drawn

#### Scenario: Overlay draws regardless of Profiler debug-overlay flags

- **WHEN** a node is selected
- **AND** all Profiler debug-overlay toggles (`Draw Light Bounds`, `Draw Mesh Bounds`, `Draw Custom Bounds`, `Draw OcTree Bounds`) are off
- **THEN** the selection overlay for the selected node is still drawn

### Requirement: The selection overlay for a Point Light SHALL be a wireframe sphere scaled by the light's radius

When the selected `SceneNode` has a `Light` component with `LightType::POINT`, the editor SHALL draw a wireframe sphere using `light->GetMesh()` (the unit ico-sphere from `MeshUtil::GetUnitIcoSphere`) scaled by `light->GetRadius()` on all three axes. The sphere SHALL be drawn at the light node's world position via `RenderUtil::DrawMesh(light->GetMesh(), scaledWorldMatrix, kSelectionColor)`, where `scaledWorldMatrix` is the node's world matrix with an additional scale of `Vector4(light->GetRadius(), light->GetRadius(), light->GetRadius(), 1.0)` applied.

This matches the visual reference in `Pipeline::DrawDebug`'s `LIGHT_BOUNDS` path for point lights.

#### Scenario: Point light selection draws a sphere of the correct radius

- **WHEN** the user selects a `SceneNode` whose `Light` component has `LightType::POINT` and `Radius == 5.0`
- **THEN** on the next frame a wireframe sphere is drawn centered on the light node's world position
- **AND** the sphere's radius is `5.0` world units

#### Scenario: Editing the point light radius updates the sphere

- **WHEN** a point light with `Radius == 5.0` is selected
- **AND** the user edits `Radius` to `10.0` in the Node Properties panel
- **THEN** on the next frame the wireframe sphere's radius is `10.0` world units

### Requirement: The selection overlay for a Spot Light SHALL be a wireframe cone matching the light's volume

When the selected `SceneNode` has a `Light` component with `LightType::SPOT`, the editor SHALL draw a wireframe cone using `light->GetMesh()`, which is pre-shaped by `Light::EvaluateVolume()` to a cone of `height = light->GetRadius()` and `bottomRadius = tan(light->GetOutterAngle() * 0.5) * height`, translated so the cone hangs below the light node's origin (light default direction `(0,-1,0)`). The cone SHALL be drawn at the light node's world matrix via `RenderUtil::DrawMesh(light->GetMesh(), node->GetWorldMatrix(), kSelectionColor)` (no additional scaling — the mesh is already shaped).

This matches the visual reference in `Pipeline::DrawDebug`'s `LIGHT_BOUNDS` path for spot lights.

#### Scenario: Spot light selection draws a cone of the correct radius and height

- **WHEN** the user selects a `SceneNode` whose `Light` component has `LightType::SPOT`, `Radius == 8.0`, and `OutterAngle == 1.0472` radians (60°)
- **THEN** on the next frame a wireframe cone is drawn at the light node's world position
- **AND** the cone's height is `8.0` world units
- **AND** the cone's base radius is approximately `tan(0.5236) * 8.0 ≈ 4.62` world units

#### Scenario: Editing the spot light outer angle updates the cone

- **WHEN** a spot light with `OutterAngle == 1.0472` is selected
- **AND** the user narrows `Outer Angle` to `0.5236` radians (30°) in the Node Properties panel
- **THEN** on the next frame the wireframe cone's base radius shrinks to approximately `tan(0.2618) * height` world units

### Requirement: The editor SHALL keep the Light volume mesh in sync with the Light's geometry fields

The Node Properties panel SHALL call `Light::EvaluateVolume()` (in addition to the existing `Light::CalculateAABB()` call) whenever a geometry-affecting field (`Type`, `InnerAngle`, `OutterAngle`, `Radius`) changes. This ensures `Light::GetMesh()` returns a mesh consistent with the light's current radius and angles for both the selection overlay and the Profiler's `LIGHT_BOUNDS` debug overlay.

#### Scenario: Editing radius rebuilds the volume mesh

- **WHEN** the user edits a spot light's `Radius` from `5.0` to `10.0` in the Node Properties panel
- **THEN** `Light::EvaluateVolume()` is invoked for that light
- **AND** `Light::GetMesh()` returns a cone of height `10.0` on the next access

#### Scenario: Editing outer angle rebuilds the volume mesh

- **WHEN** the user edits a point light's `Type` to `SPOT` in the Node Properties panel
- **THEN** `Light::EvaluateVolume()` is invoked for that light
- **AND** `Light::GetMesh()` returns a cone (not the unit ico-sphere) on the next access

### Requirement: The editor SHALL emit a selection-changed signal when the selected SceneNode changes

The `Editor` namespace SHALL expose `Signal<SceneNode*>& Editor::OnSelectionChanged()`. Every write to the internal selected-node pointer SHALL go through `Editor::SetSelectedSceneNode(SceneNode*)`, which SHALL update the pointer and emit the signal with the new pointer (which may be `nullptr` for deselect). The signal SHALL fire exactly once per selection change.

Existing selection write sites (viewport picking readback, Scene Inspector tree click, File → New / Open / Shutdown clears, dangling-pointer cleanup in the Node Properties panel) SHALL be routed through `Editor::SetSelectedSceneNode` so the signal fires uniformly.

When `WITH_EDITOR` is undefined, `Editor::OnSelectionChanged()` SHALL still exist as a no-op signal accessor so user scripts compile against both builds.

#### Scenario: Selecting via the viewport emits the signal

- **WHEN** the user left-clicks a renderable node in the Viewport window and the pick resolves
- **THEN** `Editor::OnSelectionChanged()` fires exactly once with the picked `SceneNode*`
- **AND** `Editor::GetSelectedSceneNode()` returns the same pointer

#### Scenario: Deselecting via the viewport emits the signal with nullptr

- **WHEN** a node is selected and the user clicks empty viewport space
- **THEN** `Editor::OnSelectionChanged()` fires exactly once with `nullptr`
- **AND** `Editor::GetSelectedSceneNode()` returns `nullptr`

#### Scenario: Selecting via the Scene Inspector emits the signal

- **WHEN** the user clicks a node row in the Scene Inspector
- **THEN** `Editor::OnSelectionChanged()` fires exactly once with that `SceneNode*`

#### Scenario: File → New emits the signal with nullptr

- **WHEN** a node is selected and the user invokes `File → New` (clearing the scene)
- **THEN** `Editor::OnSelectionChanged()` fires exactly once with `nullptr`
