# gltf-importer (delta)

## ADDED Requirements

### Requirement: Imported light radii SHALL be converted from glTF metres to engine centimetres

For `KHR_lights_punctual` point and spot lights, glTF `range` is specified in metres while the engine unit is 1 cm. The importer SHALL set the engine light radius to `range × 100 / world_scale_of_light_node`, where the division compensates the deferred pipeline's volume sizing (`world_matrix × radius`) — the importer strips light-node local scale to 1 (the FBX cm-to-m parent-scale fix), so the divisor is the parent chain's world scale. A `range` of 0 / unset (infinite per spec) SHALL keep the engine-units default radius (10 units), matching hand-authored scenes.

#### Scenario: FBX fire light with no authored range matches the engine default

- **WHEN** an FBX-derived glTF's point light has `range == 0`
- **THEN** the imported light's radius is 10 engine units (identical to a hand-authored scene's value)

#### Scenario: Metre-authored range converts to centimetres

- **WHEN** a glTF point light has `range = 1.5` and its node sits under a 1× parent
- **THEN** the imported light's radius is 150 engine units

#### Scenario: FBX 100× parent scale does not double-apply

- **WHEN** a glTF point light has `range = 2` and its node's parent chain carries a 100× scale (light-node local scale stripped to 1)
- **THEN** the imported light's radius is 2 engine units
- **AND** the rendered light volume spans 200 engine units in world space

#### Scenario: Scaled scene keeps light contribution

- **WHEN** a scene containing a point light with radius 10 is scaled ×100 at the root (e.g. the editor's import auto-scale)
- **THEN** the light's effective attenuation radius is 1000 units (`radius × node world scale`)
- **AND** shadow-caster collection and shadow projection use the same effective radius (no empty caster set, no contribution loss)

### Requirement: Imported scenes SHALL self-register their node hierarchy with the scene manager

Before returning, `GltfImporter::Import` SHALL register the imported root and all descendants into the returned scene's `SceneManager` (`AddSceneNodeRecursively`), so the scene is renderable as-is via `Scene.SetActive(imported)` + `Pipeline.Execute` without a `MergeInto` round-trip. (`SceneNode::AddChild` does not register nodes; previously only `MergeInto` and `Scene::Load` performed registration.)

The `Importer.MergeInto` Lua entry point SHALL remove each moved top-level child from the SOURCE scene manager (recursively) before attaching it to the target root, so the discarded source scene's octree destruction cannot wipe the target-tree back-pointer (`OcTreeNode::Clear` calls `SetOcTreeNode(nullptr)` on every node it still holds).

#### Scenario: Direct SetActive renders an imported scene

- **WHEN** a script imports a glTF via `Importer.LoadGltf` and calls `Scene.SetActive` on the returned scene without `MergeInto`
- **THEN** every mesh node in the imported hierarchy is visible to the render query (a static skinned mesh renders even with no Animator)

#### Scenario: MergeInto remains clean after self-registration

- **WHEN** an imported (self-registered) scene is merged into the active scene via `Importer.MergeInto`
- **THEN** the moved nodes are registered in the target scene manager exactly once
- **AND** destroying the imported scene does not clear the target-tree back-pointers of the moved nodes

### Requirement: The importer SHALL generate normals for NORMAL-less primitives in a user-selectable smooth or flat mode

For a glTF primitive without a `NORMAL` attribute (the glTF spec says the loader should generate them), the importer SHALL generate per-vertex normals in one of two modes controlled by `GltfImporter::Options::NormalGen`:

- **Smooth (default)** — face normals (area-weighted cross products) are accumulated per POSITION, welding vertices whose positions are bit-identical (exporter-split meshes with per-face vertices still shade smoothly), then normalized and broadcast to every vertex in the weld group.
- **Flat** — each triangle's normalized face normal is assigned to its three corners. On shared-vertex topology later triangles sharing a vertex win (true flat shading requires the exporter's split vertices).

The mode SHALL be exposed on the Lua import entry points as an optional second argument: `Importer.LoadGltf(path, normal_mode)`, `Importer.LoadFbx(path, normal_mode)`, `Importer.LoadScene(path, normal_mode)` where `normal_mode` is `"smooth"` (default when omitted) or `"flat"`; an unrecognized value SHALL log a warning and fall back to `"smooth"`. The mode SHALL apply to all import paths equally (direct glTF, FBX via FBX2glTF, and the editor's File → Open / File → Import flows).

The editor SHALL surface the mode in Settings → Import as a `Normal Gen` combo (`Smooth (default)` / `Flat`) backed by a `normals_smooth` import flag (default `true`), following the `auto_default_sun` pattern; `examples/Editor.lua` SHALL read the flag and pass the matching string to every `Importer.LoadScene` call.

#### Scenario: Default is smooth with position welding

- **WHEN** a glTF mesh whose primitives omit `NORMAL` and whose vertices are duplicated per-face is imported with no `normal_mode` argument
- **THEN** every vertex gets a normalized normal accumulated across all triangles sharing its exact position
- **AND** adjacent faces sharing positions shade without a hard seam

#### Scenario: Flat mode produces per-face normals

- **WHEN** the same mesh is imported with `normal_mode = "flat"`
- **THEN** each triangle's three corners receive that triangle's normalized face normal
- **AND** the mesh shades with visible facets

#### Scenario: Settings combo drives editor imports

- **WHEN** the user picks `Flat` in Settings → Import → Normal Gen and imports a NORMAL-less glTF via File → Import
- **THEN** the imported mesh carries flat (per-face) normals

#### Scenario: Meshes with authored normals are unaffected

- **WHEN** a glTF primitive provides a `NORMAL` attribute
- **THEN** the importer copies the authored normals verbatim regardless of the mode setting
