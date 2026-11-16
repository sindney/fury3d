# scene-round-trip

## MODIFIED Requirements

### Requirement: A `Scene` saved by the engine SHALL round-trip through `Scene::Save` then `Scene::Load` with stable structural counts

For any `Scene` produced by the engine, saving through `FileUtil::SaveByExtension` and loading into a fresh `Scene` SHALL preserve structural counts and persistent state through the shared `Serialize` implementations. Structural counts SHALL match exactly for `nodes`, `meshes_total`, `meshes_static`, `meshes_skinned`, `submeshes`, `vertices`, `triangles`, `materials`, `textures`, `animations`, and `joints`. AABB is reported but not compared because float rounding makes tight equality noisy.

Persistent state SHALL be verified by comparing the saved scene documents. The engine SHALL provide a comparison that parses two saved documents into canonical trees and reports every differing field with its path. Floating-point values SHALL compare under a documented tolerance; identifiers, paths, enumerated values, and array order SHALL compare exactly. The comparison covers whatever the archive persists, so node names and hierarchy, local transforms, component types and their persistent properties, path-keyed resource references, mesh and material assignments, light parameters, particle configuration, skeletal animation and joint bindings, terrain and ocean configuration, and render settings are all covered by construction. Transient GPU objects, runtime playback cursors, and editor-only state are excluded because the archive does not persist them.

Under `asset-path-identity`, the texture count after round trip equals the unique path count, not the pre-dedupe entry count. Legacy same-name-different-UUID textures collapse to the canonical path entry, and material references SHALL resolve to that entry.

#### Scenario: Round-trip preserves structure and semantics

- **WHEN** a scene is saved to a temporary file and reloaded
- **THEN** every structural count matches exactly
- **AND** the saved document of the reloaded scene compares equal to the saved document it was loaded from, field for field

#### Scenario: Canonical showcases round-trip in both formats

- **WHEN** the feature-showcase and island scenes are each round-tripped through temporary `.json` and `.bin` files
- **THEN** all structural and semantic checks pass for both formats
- **AND** the round-trip runs through the headless CLI without a graphics context

#### Scenario: Round-trip of the bundled legacy scene succeeds

- **WHEN** the bundled `Resource/Scene/scene.bin` is loaded, saved to a temporary `.bin`, and reloaded
- **THEN** all applicable structural counts match between the two loads
- **AND** the two saved documents compare equal
- **AND** no assertion fires during either load in a debug build

#### Scenario: Legacy same-name duplicates collapse on the first round-trip save

- **WHEN** a legacy scene contains multiple Texture entries at one path with distinct UUIDs
- **AND** the scene is saved via `SaveByExtension` and reloaded into a fresh `Scene`
- **THEN** the reloaded `EntityManager` holds exactly one Texture at that path
- **AND** the saved file contains exactly one Texture entry for that path
- **AND** affected material texture slots point at the canonical entry

### Requirement: Scenes SHALL round-trip the render settings block

A `Scene` saved by the engine SHALL round-trip the `renderSettings` block, including referenced pipeline, HDR/LDR mode, CSM enablement, and the ordered postprocess chain, with stable values in addition to the structural and semantic invariants. Scenes saved before this block existed SHALL still load, defaulting to LDR, pre-existing CSM behavior, and an empty postprocess chain. Camera component serialization remains covered by the canonical document comparison.

#### Scenario: Round-trip preserves the postprocess chain

- **WHEN** a `Scene` with a postprocess chain in `renderSettings` is saved and reloaded
- **THEN** the reloaded scene has the same effects in the same order with the same enabled flags and persistent parameters

#### Scenario: Round-trip preserves HDR and CSM settings

- **WHEN** a `Scene` with HDR and CSM enabled is saved and reloaded
- **THEN** the reloaded scene reports HDR and CSM enabled

#### Scenario: Legacy scene without render settings still loads

- **WHEN** a scene saved before render-settings persistence is loaded
- **THEN** loading returns true
- **AND** render settings default to LDR with pre-existing CSM behavior and an empty postprocess chain

### Requirement: SceneNode SHALL support an editor-only flag that serialization skips

`SceneNode` SHALL expose `SetEditorOnly(bool)` / `IsEditorOnly()`, default false, with a Lua binding. The saving branch of `SceneNode::Serialize` SHALL omit editor-only subtrees from the `"nodes"` output so tooling nodes never persist to scene files, including temporary scenes written by play mode. The flag is runtime/editor state and SHALL NOT itself load from scene files; older files have all nodes unflagged.

#### Scenario: Editor camera never reaches a saved file

- **WHEN** the editor camera node is flagged editor-only and the scene is saved normally or through play mode
- **THEN** the saved document's node tree contains no editor-camera node
- **AND** reloading that file yields a tree without it

#### Scenario: Unflagged nodes are unaffected

- **WHEN** a scene with no editor-only nodes is saved and reloaded
- **THEN** node count, hierarchy, and node state round-trip exactly under the canonical document comparison

#### Scenario: Legacy leaked nodes are cleaned by the editor

- **WHEN** a scene file containing previously leaked `EditorCamera` nodes is loaded into `furye`
- **THEN** the editor removes the stale nodes on load and flags its fresh editor camera editor-only
- **AND** the next save produces a file with zero `EditorCamera` nodes

## ADDED Requirements

### Requirement: Scene serialization SHALL emit deterministic document content

A saved scene document SHALL NOT depend on hash iteration order. The saving branch of scene serialization SHALL emit the top-level `textures`, `materials`, `meshes`, `animations`, `particleSystems`, and `heightmaps` arrays in ascending order of the asset's path and then its name; a material's `textures` and `uniforms` arrays in ascending order of the slot key; and a render chain entry's `uniforms` array in ascending order of the uniform name. Arrays whose order is semantically significant - node children, node components, joints, animation channels, and the postprocess chain - SHALL keep their existing order.

#### Scenario: Saving the same state twice produces the same document

- **WHEN** a scene is loaded from a file, saved, and the saved output is loaded and saved again
- **THEN** the two saved documents are identical byte for byte

#### Scenario: Asset order does not depend on registration order

- **WHEN** two scenes hold the same assets but register them in a different order
- **THEN** their saved documents list the assets in the same order

### Requirement: Scene documents SHALL NOT persist runtime material identifiers

A material's `material_id` uniform SHALL NOT be written to a saved scene document. The value is a process-global runtime counter that the loader replaces with the freshly constructed material's own identifier, so the persisted value is stale on every load. Loading SHALL continue to set the uniform from the material's runtime identifier.

#### Scenario: Saved document carries no material identifier

- **WHEN** a scene containing materials is saved
- **THEN** no material in the document contains a `material_id` uniform

#### Scenario: Reloaded materials keep a runtime identifier

- **WHEN** a saved scene is loaded
- **THEN** each material's `material_id` uniform holds the identifier of that material instance

### Requirement: Saved scene documents SHALL be comparable across runs and formats

The engine SHALL provide a comparison of two saved scene documents that reports structured field paths. The comparison SHALL parse each document into a canonical tree in which object members are keyed by name and array elements keep their order, then report every differing field with its path and both values. Floating-point values SHALL compare under a documented tolerance; identifiers, paths, enumerated values, and array order SHALL compare exactly. The comparison SHALL accept documents in either saved format, so a plain JSON document and the JSON payload of a compressed document compare directly.

#### Scenario: Two equal documents compare equal

- **WHEN** two saved scene documents describe the same state
- **THEN** the comparison reports no differences

#### Scenario: A differing field is reported with its path

- **WHEN** two saved scene documents differ in one component field
- **THEN** the comparison reports one difference naming the field's path and both values

#### Scenario: Compressed and plain documents compare directly

- **WHEN** a `.bin` document and a `.json` document describe the same state
- **THEN** the comparison reports no differences
