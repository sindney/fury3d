## ADDED Requirements

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

### Requirement: The Mesh editor SHALL render a centered 3D preview with rotate/zoom and an `ImGuizmo::ViewManipulate` axis-cube

The Mesh editor window SHALL render a 3D preview pane (sized to fill the available content region below the header) that renders the mesh centered in view. The default camera SHALL be positioned so the mesh's AABB fills ~60% of the preview's shorter axis: the camera distance SHALL be computed from `mesh->GetAABB()`'s radius and the preview's aspect ratio. The preview SHALL support only:

- **Rotate**: left-mouse-drag orbits the camera around the mesh's AABB center (azimuth + elevation).
- **Zoom**: mouse wheel moves the camera closer to / further from the AABB center, clamped to `[0.1 × default_distance, 10 × default_distance]`.
- **Pan** is explicitly NOT supported (the mesh stays centered at all times).

The preview SHALL render an `ImGuizmo::ViewManipulate` axis-cube in the top-right corner of the preview pane (using the overload `ViewManipulate(view, length, position, size, backgroundColor)`), and clicking a face SHALL snap the orbit camera to the corresponding axis-aligned view (front/back/left/right/top/bottom). The cube SHALL reflect the current orbit orientation in real time.

The preview SHALL render the mesh with its first bound material if the mesh has one, else a fallback flat-shaded material. The preview SHALL NOT participate in the scene's main pipeline — it is rendered into its own FBO/viewport using the same `Texture::GetTemporary` / blit pattern used by the Profiler Shadows tab.

#### Scenario: Default camera frames the mesh

- **WHEN** the user opens the editor for a mesh with AABB `[-1,-1,-1]` to `[1,1,1]`
- **THEN** the preview's default camera distance is approximately `2 / tan(fov/2) × 0.6` (so the unit cube fills ~60% of the preview's shorter axis)

#### Scenario: Drag rotates the camera

- **WHEN** the user clicks and drags left-to-right in the preview pane
- **THEN** the camera azimuth around the AABB center increases and the mesh appears to rotate

#### Scenario: Wheel zooms the camera

- **WHEN** the user scrolls the mouse wheel up inside the preview pane
- **THEN** the camera moves closer to the AABB center
- **AND** the new distance is clamped to `[0.1 × default, 10 × default]`

#### Scenario: Pan is not available

- **WHEN** the user right-click-drags or middle-click-drags in the preview pane
- **THEN** no panning occurs and the mesh stays centered

#### Scenario: ViewManipulate cube snaps to axis view

- **WHEN** the user clicks the `+X` face of the `ImGuizmo::ViewManipulate` cube
- **THEN** the orbit camera snaps to view the mesh from the +X axis
- **AND** the cube's highlighted face matches the active view

#### Scenario: ViewManipulate cube tracks orbit orientation

- **WHEN** the user orbit-drags the camera to an arbitrary angle
- **THEN** the `ImGuizmo::ViewManipulate` cube rotates to reflect the current view direction

### Requirement: The Mesh editor SHALL render mesh metadata below or beside the preview

The Mesh editor SHALL render a metadata block with the mesh's serializable properties:

- Name (read-only label)
- Total vertex count, total index count, total triangle count
- Submesh count and per-submesh vertex/index/triangle counts (collapsible)
- AABB min / max / size / center (read-only labels)
- `cast_shadows` checkbox (bound to `Mesh::GetCastShadows()` / `SetCastShadows()`)
- Joint / skeleton summary if skinned (root joint name, joint count) — read-only

#### Scenario: Mesh metadata renders totals

- **WHEN** the user opens the editor for a mesh with 1024 vertices, 2048 indices, 1 submesh
- **THEN** the metadata block displays "Vertices: 1024", "Indices: 2048", "Triangles: 682", "Submeshes: 1"

#### Scenario: Cast Shadows checkbox edits the mesh

- **WHEN** the user unchecks the `cast_shadows` checkbox in the Mesh editor
- **THEN** `mesh->SetCastShadows(false)` is called
- **AND** the change persists when the editor window is closed

#### Scenario: Skinned mesh shows joint summary

- **WHEN** the user opens the editor for a skinned mesh with 33 joints rooted at "RootJoint"
- **THEN** the metadata block displays "Root Joint: RootJoint" and "Joints: 33"
