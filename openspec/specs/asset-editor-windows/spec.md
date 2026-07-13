# asset-editor-windows

## Purpose

Per-asset editor windows opened from the Content Browser. Provides modal editor
windows for `Mesh` and `Material` assets with typed widget editors (Material)
and a 3D preview pane with orbit/zoom and an `ImGuizmo::ViewManipulate`
axis-cube (Mesh).

## Requirements

### Requirement: Double-clicking a Mesh or Material asset in the Content Browser SHALL open a per-asset editor window

When the user double-clicks a `Mesh` or `Material` tile in the Content Browser, the editor SHALL open a modal `BeginPopupModal` window scoped to that single asset. The window's title SHALL be `<AssetType>: <asset name>` and the window SHALL be `ImGuiWindowFlags_AlwaysAutoResize` for the Material editor and a fixed reasonable size (e.g. 640×480 first-use) for the Mesh editor (to accommodate the 3D preview pane). Only one editor window per asset SHALL be open at a time; double-clicking an already-open asset's tile SHALL focus the existing window rather than opening a duplicate.

Closing the window (Ok / Cancel / Esc / click-outside) SHALL NOT mutate the asset beyond edits already applied through the editor's widgets — the editor is a live view of the asset, and widget edits (texture rebind, uniform tweak, cast shadows toggle) SHALL apply to the asset in real time, exactly as the existing Material texture row and Cast Shadows checkbox do in the Node Properties panel.

#### Scenario: Double-click opens the asset editor

- **WHEN** the user double-clicks a Mesh tile named "Cube" in the Content Browser
- **THEN** a modal window titled `Mesh: Cube` opens on the next frame

#### Scenario: Double-clicking an already-open asset focuses the existing window

- **WHEN** the `Mesh: Cube` editor window is already open
- **AND** the user double-clicks the "Cube" tile again
- **THEN** no second window is opened
- **AND** the existing `Mesh: Cube` window is brought to the front

#### Scenario: Esc closes the editor without rolling back edits

- **WHEN** the user changes a uniform in the Material editor and then presses Esc
- **THEN** the editor window closes
- **AND** the uniform change persists on the underlying `Material` (no rollback)

### Requirement: The Material editor SHALL walk serializable properties and render typed editors

The Material editor window SHALL render, in order:

1. **Header**: the material's name (read-only label), the `opaque` flag as a checkbox, and the `texture_flags` value as a read-only hex label.
2. **Textures table**: one row per entry in `Material::GetTextures()` (the `TextureMap`), each row rendered by the existing `RenderMaterialTextureRow` pattern (48×48 thumbnail via `ImGui::Image((ImTextureID)(intptr_t)tex->GetID(), ImVec2(48,48), ImVec2(0,1), ImVec2(1,0))`, slot key label, texture name, and meta string `"<w> × <h> <format> <srgb|linear>"`). Each row SHALL have a "Browse…" button opening a texture picker modal listing `Scene::Manager()->ForEach<Texture>()`.
3. **Uniforms table**: one row per entry in `Material::GetUniforms()` (the `UniformMap`). The widget type SHALL be selected by runtime uniform type: `Uniform1f`/`Uniform2f`/`Uniform3f`/`Uniform4f` use `DragFloat` (multi-component for 2/3/4); color-typed uniforms (key in the standard color set: `AMBIENT_COLOR`/`DIFFUSE_COLOR`/`SPECULAR_COLOR`/`EMISSIVE_COLOR`) use `ColorEdit4`; integer-typed uniforms use `DragInt`; matrix uniforms use a read-only label. Standard factor uniforms (`SHININESS`/`TRANSPARENCY`/`AMBIENT_FACTOR`/`DIFFUSE_FACTOR`/`SPECULAR_FACTOR`/`EMISSIVE_FACTOR`) use `DragFloat` with a 0.01 step.
4. **Shader passes**: a read-only list of shader pass indices and names, omitting empty slots.

#### Scenario: Textures table renders thumbnails

- **WHEN** the user opens the editor for a material with a diffuse_texture bound to a 512×512 RGBA8 sRGB texture
- **THEN** the diffuse_texture row renders a 48×48 thumbnail, the texture name, and the meta string `"512 × 512 RGBA8 sRGB"`

#### Scenario: Empty texture slot shows placeholder

- **WHEN** the material has no specular_texture bound
- **THEN** the specular_texture row (if present in the TextureMap with a null texture) renders an empty thumbnail area and the label `"(none)"`

#### Scenario: Color uniform uses ColorEdit4

- **WHEN** the material has a `DIFFUSE_COLOR` uniform
- **THEN** the Uniforms table renders it with an `ImGui::ColorEdit4` widget bound to the uniform's storage

#### Scenario: Shininess uniform uses DragFloat

- **WHEN** the material has a `SHININESS` uniform
- **THEN** the Uniforms table renders it with an `ImGui::DragFloat` widget with a 0.01 step

#### Scenario: Picking a texture rebinds the slot live

- **WHEN** the user clicks "Browse…" on the diffuse_texture row and picks "albedo.png" from the texture picker
- **THEN** the row's thumbnail updates to albedo.png immediately
- **AND** the underlying `Material::GetTextures()["diffuse_texture"]` is reassigned to the picked texture on the same frame

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

### Requirement: The mesh editor SHALL expose an LOD dropdown that lets the user preview each level of the bound mesh

The mesh editor window's metadata block SHALL include a dropdown listing every `LOD <index>` of the mesh's LOD chain (e.g. `LOD 0`, `LOD 1`, `LOD 2`), plus an `Auto` entry as the default selection. When the user selects `LOD i`, the 3D preview pane SHALL render the corresponding `Mesh::Ptr` from the chain (`mesh->GetLodMesh(i)`) instead of the runtime-driven LOD. When the user selects `Auto`, the preview pane SHALL restore runtime-driven LOD selection (i.e. the LOD the runtime would auto-pick for the current camera distance). The dropdown is preview-only; the selection is held on the per-window state and is NOT written back to the `Mesh`. The selection is restored when the window is reopened. When the mesh has no LOD chain (its `Mesh::GetLodCount() <= 1`), the dropdown is hidden.

#### Scenario: Dropdown lists every LOD plus Auto

- **WHEN** the user opens the mesh editor for a mesh whose LOD chain has 3 entries
- **THEN** the dropdown lists `Auto`, `LOD 0`, `LOD 1`, `LOD 2`
- **AND** the default selection is `Auto`

#### Scenario: Selecting an LOD forces the preview to render that LOD

- **WHEN** the user picks `LOD 1` in the dropdown
- **THEN** the 3D preview pane renders `mesh->GetLodMesh(1)`
- **AND** the metadata block's per-LOD stats (vertex / index counts) update to reflect the picked LOD
- **AND** moving the orbit camera closer or further does NOT switch the preview to a different LOD

#### Scenario: Selecting Auto restores runtime-driven LOD

- **WHEN** the user picks `Auto` in the dropdown
- **THEN** the preview pane selects the active LOD from the camera distance (the existing runtime-driven behavior)
- **AND** moving the orbit camera closer / further swaps the rendered LOD

#### Scenario: No dropdown for single-LOD assets

- **WHEN** the user opens the mesh editor for a mesh with no LOD chain
- **THEN** no LOD dropdown is rendered
- **AND** the preview pane renders the single `Mesh` as today

#### Scenario: Preview selection persists across reopen

- **WHEN** the user picks `LOD 2` in the mesh window, then closes and re-opens the window
- **THEN** the dropdown is restored to `LOD 2`
- **AND** the preview pane renders `mesh->GetLodMesh(2)` immediately on reopen
- **AND** the `Mesh`'s LOD chain is unchanged

### Requirement: The mesh editor SHALL expose a "Generate LODs…" action that simplifies the bound mesh and stores the result as an inline LOD chain

The mesh editor window SHALL include a `Generate LODs…` button. Clicking it SHALL open a modal dialog that lets the user configure the LOD count, the per-level reduction ratio, and the simplification target error. On confirm, the editor SHALL call `SimplifyMesh` on the bound mesh and assign the returned LOD meshes to the source mesh's LOD chain via `Mesh::SetLodMeshes`. The source mesh's `MeshRender` automatically sees the new chain without any further wiring. Each generated LOD mesh SHALL be owned by the source mesh and saved inline as a `lod_meshes` sub-object on the source's JSON; the LODs SHALL NOT be registered as separate entities in the scene's `EntityManager`.

#### Scenario: Generate LODs creates a chain on the source

- **WHEN** the user opens the mesh editor for a mesh with no LOD chain
- **AND** clicks `Generate LODs…` with default settings (3 levels, ratio 0.5, target error 0.5)
- **AND** confirms
- **THEN** the mesh's LOD chain has 3 additional entries
- **AND** any `MeshRender` referencing this mesh now picks the active LOD each frame from its camera's screen-coverage

### Requirement: The mesh editor SHALL render LOD thresholds as an inline (non-collapsible) table

The mesh editor window SHALL render LOD thresholds as a flat (non-collapsible) inline table whenever the bound mesh has more than one LOD (`mesh->GetLodCount() > 1`). The table SHALL have one row per interior LOD (LOD 1 .. LOD N−1). Each row SHALL display the index label (`"LOD i"`) and an `InputFloat` widget clamped to `[0.0, 1.0]` (the widget shows its own value). The deepest LOD row (LOD N) SHALL NOT expose a slider (its threshold is locked at `0.0`); it SHALL display a read-only `"0.000"` value instead. No `TreeNode`, `CollapsingHeader`, `TreeNodeEx`, or any other collapsible widget SHALL wrap the table. Editing a slider SHALL write the updated value into the mesh's LOD chain via `Mesh::SetLodMeshes`. The mesh's save path SHALL persist the new threshold.

#### Scenario: Threshold slider updates the chain

- **WHEN** the user opens the mesh editor for a mesh with a 4-entry chain
- **AND** types `0.250` into the LOD 2 input
- **THEN** the mesh's `GetLodThreshold(2)` returns `0.250`
- **AND** the change persists to disk when the scene is saved

#### Scenario: Single-LOD mesh skips the threshold table

- **WHEN** the mesh editor is open for a mesh with `GetLodCount() == 1`
- **THEN** the LOD thresholds table is not rendered

#### Scenario: Deepest LOD row is locked

- **WHEN** the mesh editor renders the LOD thresholds table for a 4-LOD mesh
- **THEN** the LOD 3 row has no slider
- **AND** the LOD 3 row's threshold value reads `0.000`

### Requirement: The Mesh editor SHALL list submeshes as an inline table

The Mesh editor's metadata panel SHALL render submeshes as a flat (non-collapsible) inline table. The table SHALL have a header row with columns `Index`, `Name`, `Indices`, `Triangles`, and one data row per submesh. The `Name` column SHALL show the submesh's placeholder label `Submesh <i>` (the engine has no submesh name field). The `Indices` column SHALL show `sm->Indices.Data.size()`. The `Triangles` column SHALL show `sm->Indices.Data.size() / 3`. No `TreeNode`, `CollapsingHeader`, `TreeNodeEx`, or any other collapsible widget SHALL wrap the table or any row. The total counts row above the table (`Vertices`, `Indices`, `Triangles`, `Submeshes`) SHALL remain unchanged.

#### Scenario: Mesh with three submeshes renders three data rows

- **WHEN** the Mesh editor is open for a mesh whose `GetSubMeshCount() == 3` with names `["Body", "Wheel_L", ""]` and indices `[900, 600, 300]`
- **THEN** the table renders 3 data rows
- **AND** row 1 shows `0`, `Submesh 0`, `900`, `300`
- **AND** row 2 shows `1`, `Submesh 1`, `600`, `200`
- **AND** row 3 shows `2`, `Submesh 2`, `300`, `100`
- **AND** no row is collapsible

#### Scenario: Mesh with no submeshes omits the table

- **WHEN** the Mesh editor is open for a mesh with `GetSubMeshCount() == 0`
- **THEN** the submeshes table is not rendered
- **AND** the total-counts row above shows `Submeshes: 0`
