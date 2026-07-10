## ADDED Requirements

### Requirement: The Mesh editor SHALL use a two-pane layout with the viewer on the left and the metadata on the right

The Mesh editor window SHALL be organized into two panes side-by-side:

- **Left pane** (the main region, ~70% of available width): the 3D viewer pane (rendered per the "centered 3D preview with rotate/zoom" requirement, as modified by this change to add pan, simple Lambert look, and the disk-cache hookup), filling the available height of the content region.
- **Right pane** (~30% of available width, minimum 240px): a scrollable metadata panel rendered via `ImGui::BeginChild("metadata", ...)` containing the existing `RenderMeshMetadata` block (name, LOD dropdown, LOD thresholds, Generate LODs modal, stats, AABB, cast shadows, joint summary).

The two panes SHALL be laid out via `ImGui::BeginChild` + `ImGui::SameLine` (not via a second dockable window, not via `ImGui::Columns`, not via tabs). The metadata panel's lifetime SHALL be tied to the Mesh editor window — closing the editor closes both panes.

The metadata panel's contents SHALL remain unchanged from the existing `RenderMeshMetadata` implementation (`EditorAssetWindows.cpp:357-581`): name (read-only), LOD dropdown (only when `GetLodCount() > 1`), LOD Thresholds collapsible, Generate LODs button + modal, vertices/indices/triangles/submeshes counts, submeshes tree, AABB min/max/size/center, cast shadows checkbox, skinned mesh joint summary.

The Mesh editor window SHALL NOT use `ImGuiWindowFlags_AlwaysAutoResize` (it needs a fixed reasonable size to accommodate the two-pane layout). The first-use size SHALL be 640×480 or larger.

#### Scenario: Two panes render side-by-side

- **WHEN** the user opens the Mesh editor for a mesh named "Cube"
- **THEN** the editor window contains a left pane showing the 3D viewer
- **AND** a right pane showing the metadata (name, LOD dropdown, stats, AABB, etc.)
- **AND** the two panes are separated horizontally (not stacked vertically)

#### Scenario: Right pane has a minimum width

- **WHEN** the user resizes the Mesh editor window to be very narrow
- **THEN** the right metadata pane maintains at least 240px of width
- **AND** the left viewer pane shrinks to fill the remaining width

#### Scenario: Metadata panel scrolls independently

- **WHEN** the metadata panel's contents exceed the available height (e.g. a mesh with many submeshes and an expanded LOD Thresholds header)
- **THEN** the right pane shows a vertical scrollbar
- **AND** the left viewer pane does NOT scroll (it always fills its region)

#### Scenario: Closing the editor closes both panes

- **WHEN** the user closes the Mesh editor window
- **THEN** both the viewer pane and the metadata pane are removed
- **AND** the orbit state for that mesh's `popup_id` is retained in the `g_OrbitState` map until the mesh is evicted from the scene (so reopening the editor restores the previous camera angle)

## MODIFIED Requirements

### Requirement: The Mesh editor SHALL render a centered 3D preview with rotate/zoom and an `ImGuizmo::ViewManipulate` axis-cube

The Mesh editor window SHALL render a 3D viewer pane that renders the mesh in the simple Lambert look defined in the `mesh-thumbnail-disk-cache` capability (flat `vec3(0.7)` albedo, fixed directional light `(0.4, 0.8, 0.3)`, half-lambert floor `0.2`, opaque black background matching the 3D scene viewport) — identical to the Content Browser's mesh thumbnail look so the editor preview and the tile icon are visually consistent.

The default camera SHALL be positioned so the mesh's AABB fills ~60% of the viewer's shorter axis: the camera distance SHALL be computed from `mesh->GetAABB()`'s radius and the viewer's aspect ratio via the existing `ComputeInitialDistance(mesh, previewSize)` helper. The camera SHALL start focused on the mesh's AABB center.

The viewer SHALL support the following interactions:

- **Rotate**: left-mouse-drag orbits the camera around the mesh's AABB center (azimuth + elevation), using the existing `OrbitState` map keyed on `popup_id`.
- **Zoom**: mouse wheel moves the camera closer to / further from the AABB center, clamped to `[0.1 × default_distance, 10 × default_distance]`.
- **Pan**: right-mouse-drag OR middle-mouse-drag translates the orbit target point in screen-space (so the user can move the camera around the mesh without rotating it). Panning DOES NOT rotate the mesh; it shifts the orbit center.

The viewer SHALL render an `ImGuizmo::ViewManipulate` axis-cube in the top-right corner of the viewer pane (using the overload `ViewManipulate(view, length, position, size, backgroundColor)`), and clicking a face SHALL snap the orbit camera to the corresponding axis-aligned view (front/back/left/right/top/bottom). The cube SHALL reflect the current orbit orientation in real time.

The viewer SHALL render the mesh with the simple Lambert shader (NOT the mesh's first bound material). The viewer SHALL NOT participate in the scene's main pipeline — it is rendered into its own FBO/viewport using the `PreviewRT` entry from the `g_PreviewRTs` map (already declared at `EditorAssetWindows.cpp:590-597`), with the same `Texture::GetTemporary` / blit pattern used by the Profiler Shadows tab. The shader SHALL be robust against meshes with missing or zero normals (falling back to `vec3(0, 1, 0)`) so glTF meshes without a normal attribute still produce a non-white silhouette.

The viewer pane SHALL fill the left/main region of the Mesh editor window (per the two-pane layout requirement added by this change).

#### Scenario: Default camera frames the mesh

- **WHEN** the user opens the editor for a mesh with AABB `[-1,-1,-1]` to `[1,1,1]`
- **THEN** the viewer's default camera distance is approximately `2 / tan(fov/2) × 0.6` (so the unit cube fills ~60% of the viewer's shorter axis)
- **AND** the camera is aimed at the AABB center `(0, 0, 0)`

#### Scenario: Drag rotates the camera

- **WHEN** the user clicks and drags left-to-right in the viewer pane
- **THEN** the camera azimuth around the AABB center increases and the mesh appears to rotate

#### Scenario: Wheel zooms the camera

- **WHEN** the user scrolls the mouse wheel up inside the viewer pane
- **THEN** the camera moves closer to the AABB center
- **AND** the new distance is clamped to `[0.1 × default, 10 × default]`

#### Scenario: Right-drag pans the orbit target

- **WHEN** the user right-click-drags upward in the viewer pane
- **THEN** the orbit target point translates upward in screen-space
- **AND** the mesh appears to shift downward within the frame
- **AND** the camera azimuth and elevation are unchanged

#### Scenario: Middle-drag pans the orbit target

- **WHEN** the user middle-click-drags to the right in the viewer pane
- **THEN** the orbit target point translates rightward in screen-space
- **AND** the mesh appears to shift leftward within the frame
- **AND** the camera azimuth and elevation are unchanged

#### Scenario: ViewManipulate cube snaps to axis view

- **WHEN** the user clicks the `+X` face of the `ImGuizmo::ViewManipulate` cube
- **THEN** the orbit camera snaps to view the mesh from the +X axis
- **AND** the cube's highlighted face matches the active view

#### Scenario: ViewManipulate cube tracks orbit orientation

- **WHEN** the user orbit-drags the camera to an arbitrary angle
- **THEN** the `ImGuizmo::ViewManipulate` cube rotates to reflect the current view direction

#### Scenario: Viewer uses the simple Lambert shader

- **WHEN** the user opens the editor for a mesh with a textured PBR material bound to its first submesh
- **THEN** the viewer renders the mesh with the flat 0.7 grey albedo + fixed directional light
- **AND** the mesh's bound material is NOT sampled
- **AND** the viewer's background is opaque black (matching the 3D scene viewport)
- **AND** meshes with missing or zero normals still produce a non-white silhouette (the shader falls back to a default normal)

### Requirement: The Mesh editor SHALL render mesh metadata below or beside the preview

The Mesh editor SHALL render a metadata block with the mesh's serializable properties (rendered in the right-side metadata pane per the two-pane layout requirement added by this change):

- Name (read-only label)
- LOD dropdown (only when `Mesh::GetLodCount() > 1`) — preview-only selection held on per-window state; not written back to the `Mesh`
- Total vertex count, total index count, total triangle count
- Submesh count and per-submesh vertex/index/triangle counts (collapsible)
- AABB min / max / size / center (read-only labels)
- `cast_shadows` checkbox (bound to `Mesh::GetCastShadows()` / `SetCastShadows()`)
- Joint / skeleton summary if skinned (root joint name, joint count) — read-only

#### Scenario: Mesh metadata renders totals

- **WHEN** the user opens the editor for a mesh with 1024 vertices, 2048 indices, 1 submesh
- **THEN** the metadata block displays "Vertices: 1024", "Indices: 2048", "Triangles: 682", "Submeshes: 1"

#### Scenario: Cast Shadows checkbox edits the mesh

- **WHEN** the user unchecks the `cast_shadows` checkbox in the Mesh editor's metadata pane
- **THEN** `mesh->SetCastShadows(false)` is called
- **AND** the change persists when the editor window is closed

#### Scenario: Skinned mesh shows joint summary

- **WHEN** the user opens the editor for a skinned mesh with 33 joints rooted at "RootJoint"
- **THEN** the metadata block displays "Root Joint: RootJoint" and "Joints: 33"
