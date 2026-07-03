## MODIFIED Requirements

### Requirement: MeshRender section renders a three-level layout
For nodes with a `MeshRender` component, the panel SHALL render three nested collapsible blocks: the MeshRender block (mesh reference + Browse… picker + Jump-to-Asset button, renderable status indicator, material slot list where each slot has a Browse… picker and Jump-to-Asset button), the Mesh block (submesh counts, AABB, skinned/joint metadata, per-submesh Inspect dropdown), and one Material block per slot (textures with thumbnails and meta, categorized uniforms, read-only shader pass list).

The MeshRender block SHALL display:

- A **Cast Shadows** checkbox bound to `MeshRender::GetCastShadows()` / `SetCastShadows()` (per-instance flag, overrides the mesh's asset-level default).
- A **Mesh row** showing the bound mesh's name (or `(no mesh)` in disabled text), the vertex/triangle count summary, a **Browse…** button that opens a mesh picker modal listing `Scene::Manager()->ForEach<Mesh>()`, and a **→** (Jump-to-Asset) button that calls `Editor::SelectAssetInBrowser(typeid(Mesh), mesh->GetName())` to focus the mesh in the Content Browser.
- A **Material slot list** with one row per slot in `MeshRender::GetMaterialCount()`. Each row SHALL show: the slot index, the bound material's name (or `(none)`), a **Browse…** button opening a material picker modal listing `Scene::Manager()->ForEach<Material>()`, a **→** (Jump-to-Asset) button that calls `Editor::SelectAssetInBrowser(typeid(Material), mat->GetName())`, and a remove-slot button (`×`) that erases the slot (sets it to null weak_ptr). When the mesh has more submeshes than material slots, an **Add Material Slot** button SHALL append a new null slot.

The **Browse… mesh picker** SHALL be a `BeginPopupModal` window listing every `Mesh` in the active `EntityManager` as a `Selectable` row (`<name>  <verts>v <tris>t`), with `ImGuiSelectableFlags_AllowDoubleClick`. Single-click selects (highlights), double-click or `OK` confirms: the picker calls `MeshRender::SetMesh(picked_mesh)`, which updates the node's model AABB via the existing `OnAttaching` path, then closes. `Cancel` closes without changing the mesh.

The **Browse… material picker** SHALL work identically but list every `Material` in the active `EntityManager` and call `MeshRender::SetMaterial(picked_material, slot_index)` on confirm.

The **Jump-to-Asset** button (`→`) SHALL be disabled (greyed out) when the mesh or material slot is null. When enabled, clicking it SHALL call `Editor::SelectAssetInBrowser(...)` and switch keyboard focus to the Content Browser window.

Below the MeshRender block, the **Mesh block** (collapsible) and per-slot **Material blocks** (collapsible) SHALL render the read-only metadata and texture/uniform/shader-pass editors exactly as before (per the existing `Material block renders textures, uniforms, and shader passes` requirement and the `RenderMeshRenderBody` / `RenderMaterialSectionBody` / `RenderMaterialTextureRow` implementations in `EditorNodeProperties.cpp`). The Material block's texture row SHALL additionally expose a **Browse…** button that opens a texture picker modal listing `Scene::Manager()->ForEach<Texture>()` and rebinds the slot via `Material::SetTexture(key, picked_texture)` — reusing the pattern specified in the `asset-editor-windows` capability's Material editor.

#### Scenario: MeshRender header shows mesh name and status
- **WHEN** the selected node has a MeshRender component bound to a mesh named "Cube" and one Material
- **THEN** the MeshRender block header displays "Mesh: Cube" and a green renderable status indicator

#### Scenario: MeshRender shows renderable=false when material count is short
- **WHEN** the selected node has a MeshRender with two submeshes but only one material slot filled
- **THEN** the status indicator is rendered in red (not renderable)

#### Scenario: Mesh block lists totals and AABB
- **WHEN** the MeshRender is bound to a mesh
- **THEN** the Mesh block displays total vertex count, total index count, total triangle count, submesh count, AABB min/max/size/center, and a Cast Shadows checkbox

#### Scenario: Inspect dropdown lists All and every submesh
- **WHEN** the bound mesh has three submeshes
- **THEN** the Inspect dropdown offers "All", "Submesh 0", "Submesh 1", "Submesh 2"

#### Scenario: Inspecting a submesh shows its counts
- **WHEN** the user selects "Submesh 1" from the Inspect dropdown
- **THEN** the Mesh block displays Submesh 1's vertex count, index count, triangle count, and a bitmask of non-empty vertex attributes

#### Scenario: Mesh row shows Browse and Jump-to-Asset buttons
- **WHEN** the selected node has a MeshRender bound to a mesh named "Cube"
- **THEN** the Mesh row renders a "Browse…" button and a "→" button next to the mesh name "Cube"
- **AND** both buttons are enabled (not greyed out)

#### Scenario: No-mesh row disables Jump-to-Asset
- **WHEN** the selected node has a MeshRender with no mesh bound
- **THEN** the Mesh row displays "(no mesh)" in disabled text
- **AND** the "→" Jump-to-Asset button is disabled

#### Scenario: Browse… opens the mesh picker modal
- **WHEN** the user clicks "Browse…" on the Mesh row
- **THEN** a modal window opens listing every `Mesh` in `Scene::Manager()` as Selectable rows
- **AND** each row shows `<name>  <verts>v <tris>t`

#### Scenario: Double-click in mesh picker rebinds the mesh
- **WHEN** the user double-clicks the "Cylinder" row in the mesh picker
- **THEN** `MeshRender::SetMesh(cylinder)` is called
- **AND** the node's model AABB is updated to the cylinder's AABB
- **AND** the picker modal closes
- **AND** the MeshRender block re-renders with "Mesh: Cylinder"

#### Scenario: Confirm in material picker rebinds the slot
- **WHEN** the user opens the material picker on slot 1 and clicks "OK" with "Material_Ground" selected
- **THEN** `MeshRender::SetMaterial(material_ground, 1)` is called
- **AND** the picker modal closes
- **AND** the slot 1 row re-renders with "Material_Ground"

#### Scenario: Jump-to-Asset focuses the asset in the Content Browser
- **WHEN** the user clicks the "→" button on the Mesh row while bound to "Cube"
- **THEN** `Editor::SelectAssetInBrowser(typeid(Mesh), "Cube")` is called
- **AND** the Content Browser window is brought to the front and the "Cube" tile is scrolled into view on the next frame

#### Scenario: Add Material Slot appends a null slot
- **WHEN** the bound mesh has 3 submeshes and the MeshRender has 2 material slots
- **THEN** an "Add Material Slot" button is rendered
- **AND** clicking it appends a new null slot (slot index 2) to `MeshRender::m_Materials`

#### Scenario: Remove slot erases the slot
- **WHEN** the user clicks the "×" button on material slot 1
- **THEN** the slot at index 1 is set to a null weak_ptr (erased from the slot list)
- **AND** the row re-renders showing "(none)"
