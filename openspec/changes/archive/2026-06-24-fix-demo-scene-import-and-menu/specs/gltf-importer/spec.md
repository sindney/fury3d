## ADDED Requirements

### Requirement: The importer SHALL translate `KHR_lights_punctual` entries into engine `Light` components

For each entry in `tinygltf::Model::lights`, the importer SHALL build an engine `Light` prototype according to the mapping below. For each `tinygltf::Node` whose `light` index is non-negative, the importer SHALL clone the corresponding prototype and attach it as a `Light` component on the emitted `SceneNode`.

Mapping:

- `tinygltf::Light::type == "point"` → `LightType::POINT`. The `range` field (float, glTF units) SHALL map to the engine `Light`'s radius. When `range` is 0 or unset, the engine's existing default radius applies.
- `tinygltf::Light::type == "spot"` → `LightType::SPOT`. `range` maps to radius as above. `spot.innerConeAngle` and `spot.outerConeAngle` (radians, per the glTF spec) SHALL map directly (no conversion) onto the engine's inner-cone and outer-cone fields.
- `tinygltf::Light::type == "directional"` → `LightType::DIRECTIONAL`. `range` is ignored.
- `tinygltf::Light::color` (linear-space float[3]) SHALL be copied to the engine `Light`'s color.
- `tinygltf::Light::intensity` SHALL be copied 1:1 to the engine `Light`'s intensity. (PBR-correct luminous-flux conversion is deferred; v1 is unit-less pass-through.)

The importer SHALL log exactly one info-level line per imported light naming the source node, light type, and intensity. If a node references a light index that is out of range, the importer SHALL log a warning and skip that node's light without aborting the rest of the import.

When `tinygltf::Model::lights` is empty AND no lights are emitted, the importer SHALL log a single warning advising "imported scene has no lights — viewport will render black under deferred Lambert pipeline." This is informational; the import still succeeds.

#### Scenario: glTF point light is translated to a POINT light on the corresponding SceneNode

- **WHEN** the importer processes a glTF model whose `model.lights` contains a `{type:"point", color:[1,0.8,0.5], intensity:5, range:8}` entry, and a node with `node.light` pointing at that entry
- **THEN** the emitted `SceneNode` has a `Light` component
- **AND** that `Light` has `GetType() == LightType::POINT`
- **AND** its color is approximately `(1, 0.8, 0.5)`, its intensity is approximately `5`, and its radius is approximately `8`

#### Scenario: glTF spot light cone angles are preserved without conversion

- **WHEN** the importer processes a glTF spot light with `innerConeAngle = 0.4`, `outerConeAngle = 0.9` (both radians)
- **THEN** the emitted engine `Light` has inner and outer cone fields equal to `0.4` and `0.9`

#### Scenario: glTF directional light translates to DIRECTIONAL

- **WHEN** the importer processes a glTF light with `type:"directional"`
- **THEN** the emitted engine `Light` has `GetType() == LightType::DIRECTIONAL`
- **AND** the light's `range` field (if any in the source) is ignored

#### Scenario: Scenes with no lights produce a deferred-Lambert warning

- **WHEN** the importer processes a glTF model whose `model.lights` is empty (e.g., a mesh-only export)
- **THEN** the import succeeds (returns a non-null `Scene`)
- **AND** the log contains exactly one warning naming the asset and stating "viewport will render black under deferred Lambert"

#### Scenario: Outdoor demo asset's fire light survives an FBX → glTF → engine round-trip

- **WHEN** a user clicks `File → Open → outdoor.fbx` in Demo.lua (which triggers `Importer.LoadFbx`, which invokes FBX2glTF and then `GltfImporter::Import` on the resulting `.glb`)
- **AND** the source FBX carries a point light at the campfire location
- **THEN** the imported scene contains a `SceneNode` with a `Light` component of `LightType::POINT` at the campfire location
- **AND** the viewport renders the surrounding geometry illuminated by that point light (not black)
