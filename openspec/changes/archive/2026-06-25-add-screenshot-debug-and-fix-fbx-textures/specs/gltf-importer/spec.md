## ADDED Requirements

### Requirement: The importer SHALL load embedded glTF images directly from memory into engine `Texture` objects, without writing to disk

When a `tinygltf::Image` has an empty `image.uri` and a non-negative `bufferView`, the importer SHALL:

1. Resolve the bufferView to its underlying `tinygltf::Buffer` slice (offset + length).
2. Determine the encoded format from `image.mimeType` (`image/jpeg`, `image/png`, `image/bmp`).
3. Construct the engine `Texture` and call a new `Texture::CreateFromMemory(bytes, len, srgb, mipmap)` API to upload the texture to the GPU.
4. Record the original filename for later use during scene serialization. Preference order: `image.name` if non-empty, else `<scene_stem>_image<i>.<ext>` derived from the input glTF's basename.

The importer SHALL NOT call `Texture::SetFilePathAndSRGB`, SHALL NOT synthesize a temp-file URI, and SHALL NOT write any bytes to disk during this load. The encoded bytes SHALL remain attached to the `Texture` object so they can be extracted later if the scene is saved.

When `image.uri` is non-empty (external-URI case), the importer SHALL keep the existing behavior: pass the URI through to `Texture::SetFilePathAndSRGB` and rely on the scene's working_dir to resolve relative paths. No bytes-in-memory path runs for external-URI images.

#### Scenario: tank.fbx embedded JPEG loads textured without disk hops

- **WHEN** Demo.lua calls `Importer.LoadFbx("Resource/Scene/tank.fbx")` and FBX2glTF emits a `.glb` with `image[0].name = "wheels.jpg"`, `image[1].name = "body.jpg"`, `image[2].name = "grass.jpg"`, all bufferView-backed
- **THEN** each emitted engine `Texture` has its GPU `m_ID != 0` after import (eager upload from memory succeeded)
- **AND** no files are written to any temp directory by the importer
- **AND** the rendered viewport shows the textured tank body, wheels, and grass plane

#### Scenario: External-URI .gltf still works through the file path

- **WHEN** the importer loads a `.gltf` whose `image.uri` is `textures/foo.png` (relative external file)
- **THEN** the existing `SetFilePathAndSRGB` + `CreateFromImage` path runs unchanged
- **AND** no in-memory upload path is invoked for that image

#### Scenario: Importer no longer needs a temp directory for embedded images

- **WHEN** any code path invokes `GltfImporter::Import` (via `LoadGltf`, `LoadFbx`, or the CLI `convert` chain) on a `.glb` with embedded images
- **THEN** no `temp_directory_path()` call is made by the importer
- **AND** no `_image<i>.<ext>` files appear on disk after the import returns

### Requirement: `Texture` SHALL provide `CreateFromMemory` for in-memory image upload and SHALL retain the encoded bytes for later extraction

The `Texture` class SHALL gain:

- `void CreateFromMemory(const unsigned char *bytes, size_t len, bool srgb, bool mipmap)`: Decode the encoded image with `stbi_load_from_memory` (already linked via `STB_IMAGE_IMPLEMENTATION` in `FileUtil.cpp`), upload to the GPU using the same `glTexStorage2D` / `glTexSubImage2D` path as `CreateFromImage`. On success, the texture's `m_ID` is non-zero, `m_Width` / `m_Height` / `m_Format` are set, and the encoded bytes are stored in a private `m_EncodedBytes` member alongside `m_OriginalFilename`.
- A way to expose the encoded bytes to the save path. Either: (a) the save-time extraction code in `FileUtil` is `friend` of `Texture` and reads `m_EncodedBytes` directly, or (b) the API surfaces `GetEncodedBytes() const -> const std::vector<unsigned char>&` and `GetOriginalFilename() const -> std::string`. The choice is non-normative; the spec only mandates that the bytes survive from import to save.

The `Texture::Save` serializer SHALL not embed bytes inline in JSON. The disk-extraction step in `FileUtil::SaveFile` / `SaveCompressedFile` (see the `embedded-textures` spec) is responsible for writing the bytes to a sibling file and setting `m_FilePath` before serialization runs.

#### Scenario: CreateFromMemory uploads and retains bytes

- **WHEN** the importer calls `texture->CreateFromMemory(jpeg_bytes, jpeg_len, /*srgb=*/true, /*mipmap=*/true)`
- **THEN** the texture's `m_ID` is non-zero (GPU upload succeeded)
- **AND** the texture's encoded bytes are retrievable later (via friend access or getter) — `len` bytes, identical to the input
- **AND** the texture's `m_FilePath` is empty (no disk path yet)

#### Scenario: Texture round-trip after save extracts to file

- **WHEN** a scene with an in-memory texture is saved via `FileUtil::SaveFile` (which triggers the save-time extraction step)
- **THEN** the texture's `m_FilePath` is set to the relative or absolute path of the extracted sibling file
- **AND** the saved JSON's texture entry has `path` pointing at that file

## REMOVED Requirements

### Requirement: The importer SHALL extract embedded glTF image bytes during import to a temp-file location

**Reason**: Replaced by in-memory loading via `Texture::CreateFromMemory` (see the new `embedded-textures` capability and the new requirement above). The temp-file extraction path created portability problems (saved scenes referenced absolute temp paths), required the caller to set `Options::output_basename_no_ext` correctly to avoid silent-blank textures, and produced different results between the CLI write path (extraction ran) and the runtime read path (extraction did not run). The new architecture eliminates all three.

**Migration**: Callers that previously set `GltfImporter::Options::output_basename_no_ext` (currently `LuaBindings.cpp:LoadFbx` and `Cli.cpp:convert fbx`) SHALL stop setting that field; the field is removed from `Options`. Code that depended on temp `_image<i>.<ext>` files existing on disk after `Import` returns SHALL switch to using `Texture`'s in-memory bytes (or, if disk presence is needed, save the scene first and use the resulting sibling files).

## MODIFIED Requirements

### Requirement: The importer SHALL translate glTF materials to engine Lambert materials with lossy PBR mapping

For each `tinygltf::Material`, the importer SHALL emit one engine `Material` registered in the scene's `EntityManager` with the engine's named-uniform shape (matching today's `Material::Save` output for the Lambert pipeline). The PBR `baseColorFactor` SHALL map to the `diffuse_color` uniform; the `baseColorTexture` SHALL map to the `diffuse_texture` slot. The texture's pixels SHALL be loaded according to the new "embedded glTF images directly from memory" requirement above (in-memory upload for embedded images, file-path passthrough for external-URI images). The `emissiveFactor` SHALL map to the `emissive_color` uniform. Other PBR fields (`metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`) SHALL be read but not mapped to engine uniforms in v1; the importer SHALL log exactly one warning per source material that lists the discarded fields. The emitted material's `opaque` flag SHALL be `true` when the glTF material's `alphaMode` is `OPAQUE` and `false` otherwise. (Note: the engine ships a Lambert deferred pipeline only as of v1; an HDR/PBR pipeline is a deliberately deferred follow-up.)

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

#### Scenario: One warning per material with discarded PBR fields
- **WHEN** a glTF material has a non-null `normalTexture`, `metallicFactor`, and `roughnessFactor`
- **THEN** stderr contains a single warning line for that material naming the discarded fields
- **AND** no extra warnings are emitted for other materials that share the same discarded set

#### Scenario: alphaMode controls opaque flag
- **WHEN** a glTF material has `alphaMode = "BLEND"`
- **THEN** the emitted engine `Material`'s `opaque` flag is `false`
- **AND WHEN** `alphaMode` is `"OPAQUE"` (or unspecified), the flag is `true`
