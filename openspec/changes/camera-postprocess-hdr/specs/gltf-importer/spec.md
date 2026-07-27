## MODIFIED Requirements

### Requirement: The importer SHALL translate glTF materials to engine Lambert materials with lossy PBR mapping

For each `tinygltf::Material`, the importer SHALL emit one engine `Material` registered in the scene's `EntityManager` with the engine's named-uniform shape (matching today's `Material::Save` output for the Lambert pipeline). The PBR `baseColorFactor` SHALL map to the `diffuse_color` uniform; the `baseColorTexture` SHALL map to the `diffuse_texture` slot. The texture's pixels SHALL be loaded according to the "embedded glTF images directly from memory" requirement (in-memory upload for embedded images, file-path passthrough for external-URI images). The `emissiveFactor` SHALL map to the `emissive_color` uniform. The emitted material's `opaque` flag SHALL be `true` when the glTF material's `alphaMode` is `OPAQUE` and `false` otherwise.

Material translation SHALL be sensitive to the target pipeline's HDR mode at import time:

- **When the target pipeline is LDR** (Lambert), the importer SHALL keep the existing lossy mapping: `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, and `occlusionTexture` SHALL be read but not mapped to engine uniforms, and the importer SHALL log exactly one warning per source material listing the discarded fields.
- **When the target pipeline is HDR** (PBR), the importer SHALL additionally map `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, and `occlusionTexture` to the corresponding PBR material uniforms/texture slots, so no PBR field is silently discarded.

#### Scenario: baseColorFactor maps to diffuse_color

- **WHEN** a glTF material has `pbrMetallicRoughness.baseColorFactor = [0.8, 0.2, 0.2, 1.0]`
- **THEN** the emitted engine `Material` has a `Uniform3f` named `diffuse_color` with values `(0.8, 0.2, 0.2)`

#### Scenario: LDR import discards PBR-only fields with a warning

- **WHEN** a glTF material with metallic/roughness/normal/occlusion inputs is imported while the target pipeline is LDR
- **THEN** those fields are read but not mapped to engine uniforms
- **AND** exactly one warning per source material lists the discarded fields

#### Scenario: HDR import maps PBR fields

- **WHEN** a glTF material with metallic/roughness/normal/occlusion inputs is imported while the target pipeline is HDR
- **THEN** those inputs are mapped to the PBR material's metallic/roughness/normal/occlusion uniforms and texture slots
- **AND** no warning about discarded PBR fields is logged
