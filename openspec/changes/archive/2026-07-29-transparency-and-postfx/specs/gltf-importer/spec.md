# gltf-importer (delta)

## MODIFIED Requirements

### Requirement: The importer SHALL translate glTF materials to engine Lambert materials with lossy PBR mapping

For each `tinygltf::Material`, the importer SHALL emit one engine `Material` registered in the scene's `EntityManager` with the engine's named-uniform shape (matching today's `Material::Save` output for the Lambert pipeline). The PBR `baseColorFactor` SHALL map to the `diffuse_color` uniform; the `baseColorTexture` SHALL map to the `diffuse_texture` slot. The texture's pixels SHALL be loaded according to the "embedded glTF images directly from memory" requirement (in-memory upload for embedded images, file-path passthrough for external-URI images). The `emissiveFactor` SHALL map to the `emissive_color` uniform. The glTF `alphaMode` SHALL map to the material's alpha mode: `OPAQUE` (or unspecified) → `OPAQUE`, `MASK` → `MASK`, `BLEND` → `BLEND`; `alphaCutoff` (default 0.5 when unspecified) SHALL map to the material's alpha cutoff. The legacy `opaque` flag SHALL be derived as `alpha_mode != BLEND` — so MASK materials are opaque-bucketed (alpha-tested) and only BLEND materials join the transparent queue. When the material declares `KHR_materials_transmission` with a positive `transmissionFactor` and `alphaMode` is not `"BLEND"`, the importer SHALL fall back to alpha mode `BLEND` with `transparency = transmissionFactor` (a refraction approximation, not a full transmission implementation).

Material translation SHALL be sensitive to the target pipeline's HDR mode at import time:

- **When the target pipeline is LDR** (Lambert), the importer SHALL keep the existing lossy mapping: `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, and `occlusionTexture` SHALL be read but not mapped to engine uniforms, and the importer SHALL log exactly one warning per source material listing the discarded fields.
- **When the target pipeline is HDR** (PBR), the importer SHALL additionally map `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, and `occlusionTexture` to the corresponding PBR material uniforms/texture slots, so no PBR field is silently discarded.

#### Scenario: baseColorFactor maps to diffuse_color

- **WHEN** a glTF material has `pbrMetallicRoughness.baseColorFactor = [0.8, 0.2, 0.2, 1.0]`
- **THEN** the emitted engine `Material` has a `Uniform3f` named `diffuse_color` with values `(0.8, 0.2, 0.2)`

#### Scenario: baseColorTexture maps to diffuse_texture slot (external URI)

- **WHEN** a glTF material has `pbrMetallicRoughness.baseColorTexture.index` set, AND that texture's image has a non-empty `uri` (e.g., `"foo.png"`)
- **THEN** the emitted engine `Material` has an entry in its texture map with key `diffuse_texture` whose `m_FilePath` is the URI verbatim
- **AND** the texture's GPU upload uses `Texture::CreateFromImage(uri, ...)` (existing path)

#### Scenario: baseColorTexture maps to diffuse_texture slot (embedded bytes)

- **WHEN** a glTF material has `pbrMetallicRoughness.baseColorTexture.index` set, AND that texture's image is bufferView-backed with empty `uri`
- **THEN** the emitted engine `Material` has an entry in its texture map with key `diffuse_texture` whose GPU upload was driven by `Texture::CreateFromMemory(bytes, len, ...)`
- **AND** the texture's `m_FilePath` is empty until the scene is saved
- **AND** the texture's recorded `m_OriginalFilename` matches the glTF `image.name` (or the synthesized `<stem>_image<i>.<ext>` fallback when `image.name` is empty)

#### Scenario: LDR import discards PBR-only fields with a warning

- **WHEN** a glTF material with metallic/roughness/normal/occlusion inputs is imported while the target pipeline is LDR
- **THEN** those fields are read but not mapped to engine uniforms
- **AND** exactly one warning per source material lists the discarded fields

#### Scenario: HDR import maps PBR fields

- **WHEN** a glTF material with metallic/roughness/normal/occlusion inputs is imported while the target pipeline is HDR
- **THEN** those inputs are mapped to the PBR material's metallic/roughness/normal/occlusion uniforms and texture slots
- **AND** no warning about discarded PBR fields is logged

#### Scenario: alphaMode BLEND maps to transparent

- **WHEN** a glTF material has `alphaMode = "BLEND"`
- **THEN** the emitted engine `Material` has alpha mode `BLEND` and its `opaque` flag is `false`

#### Scenario: alphaMode MASK maps to alpha-tested opaque

- **WHEN** a glTF material has `alphaMode = "MASK"` and `alphaCutoff = 0.25`
- **THEN** the emitted engine `Material` has alpha mode `MASK`, alpha cutoff 0.25, and its `opaque` flag is `true`

#### Scenario: alphaMode OPAQUE or unspecified

- **WHEN** a glTF material has `alphaMode = "OPAQUE"` or no `alphaMode` field
- **THEN** the emitted engine `Material` has alpha mode `OPAQUE`, default cutoff 0.5, and its `opaque` flag is `true`

#### Scenario: KHR_materials_transmission falls back to BLEND

- **WHEN** a glTF material declares `KHR_materials_transmission` with `transmissionFactor > 0` and its `alphaMode` is not `"BLEND"`
- **THEN** the emitted engine `Material` has alpha mode `BLEND` and its `transparency` uniform equals the transmissionFactor (so alpha = 1 − transmissionFactor)
- **AND** an explicit `alphaMode = "BLEND"` takes precedence over the extension
