# scene-inspector-component-panel

## Purpose

TBD

## Requirements

### Requirement: Components section enumerates all attached components
The Node Properties panel SHALL list every component attached to the selected node as a collapsible section with a header showing the component's type name and a control affordance for removal.

#### Scenario: Selected node has Light and MeshRender
- **WHEN** the selected node has a Light component and a MeshRender component
- **THEN** the panel renders a "Light" section and a "MeshRender" section, in registration order

#### Scenario: Selected node has no components
- **WHEN** the selected node has no components (other than the fundamental Transform)
- **THEN** the panel renders only the Transform section

### Requirement: Each component section has a delete button except Transform
Every component section SHALL display a delete control. The Transform component SHALL NOT display a delete control because it is fundamental to the node.

#### Scenario: Delete a Light component
- **WHEN** the user clicks the delete control on the Light section
- **THEN** the Light component is detached from the node and the section disappears

#### Scenario: Transform has no delete control
- **WHEN** the panel renders the Transform section
- **THEN** no delete control is rendered for it

### Requirement: Add Component button offers deduplicated registry entries
The Node Properties panel SHALL display an "Add Component" button at the bottom of the Components section. The button SHALL open a menu listing every entry in `SceneNode::ComponentRegistry` that is not currently attached to the selected node.

#### Scenario: Add Component menu omits already-attached types
- **WHEN** the selected node already has a Light component
- **THEN** the Add Component menu does NOT list "Light"

#### Scenario: Add Component offers unattached types
- **WHEN** the selected node has no Light and no Camera
- **THEN** the Add Component menu lists "Light" and "Camera"

#### Scenario: Adding a component attaches it
- **WHEN** the user picks "Light" from the Add Component menu
- **THEN** a new Light component is created via the registry factory and attached to the node, and the panel re-renders to show the new Light section

#### Scenario: Transform is not in the Add Component menu
- **WHEN** the Add Component menu opens
- **THEN** "Transform" is not offered (Transform is fundamental and always present)

### Requirement: MeshRender section renders a three-level layout
For nodes with a `MeshRender` component, the panel SHALL render three nested collapsible blocks: the MeshRender block (mesh reference, renderable status indicator, material slot list), the Mesh block (submesh counts, AABB, skinned/joint metadata, per-submesh Inspect dropdown), and one Material block per slot (textures with thumbnails and meta, categorized uniforms, read-only shader pass list).

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

### Requirement: Material block renders textures, uniforms, and shader passes
For each material slot, the panel SHALL render a Material block containing a Textures table (one row per standard slot key plus any custom keys, each row with a 64×64 thumbnail via `ImGui::Image` using the texture's GL handle), a Uniforms table (color uniforms use `ColorEdit4`, factor / shininess / transparency uniforms use `DragFloat`, other keys render by runtime uniform type), and a read-only list of shader passes by name.

#### Scenario: Diffuse slot shows thumbnail and meta
- **WHEN** the material has a diffuse_texture bound to a 512×512 RGBA8 sRGB texture
- **THEN** the row renders a 64×64 thumbnail, the texture name, and the meta string "512 × 512 RGBA8 sRGB"

#### Scenario: Empty slot shows placeholder
- **WHEN** the material has no specular_texture bound
- **THEN** the specular_texture row renders an empty thumbnail area and the label "(none)"

#### Scenario: Custom texture keys are rendered
- **WHEN** the material's texture map contains a non-standard key like "detail_map"
- **THEN** the Textures table includes a row for "detail_map" alongside the standard slots

#### Scenario: Color uniforms use ColorEdit4
- **WHEN** the material has a DIFFUSE_COLOR uniform
- **THEN** the Uniforms table renders it with an ImGui::ColorEdit4 widget

#### Scenario: Factor uniforms use DragFloat
- **WHEN** the material has a SHININESS uniform
- **THEN** the Uniforms table renders it with an ImGui::DragFloat widget

#### Scenario: Shader passes are listed by name
- **WHEN** the material has shader passes assigned at indices 0 and 2
- **THEN** the Material block displays "Pass 0: <name>" and "Pass 2: <name>" and omits empty passes

#### Scenario: Picking a texture from the picker updates the slot
- **WHEN** the user picks "albedo.png" from the texture picker on the diffuse_texture row
- **THEN** the row's thumbnail updates to albedo.png and the underlying material slot is reassigned