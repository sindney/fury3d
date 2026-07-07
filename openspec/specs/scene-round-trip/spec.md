# scene-round-trip

## Purpose

Covers the contract that a `Scene` saved by the engine can be reloaded into a fresh `Scene` with stable structural counts — including the `Scene::Load` reload path (which must not assert or silently skip the top-level `textures` array), the `FileUtil::SaveByExtension` dispatch helper that selects the save format by extension, and the round-trip count-stability invariant.

## Requirements

### Requirement: `Scene::Load` SHALL read the top-level `"textures"` array without asserting or silently skipping

`Scene::Load` (engine/Fury/Scene.cpp) SHALL walk the top-level `"textures"` array — when present — using the array-walking `LoadArray` overload on the already-resolved member value. It SHALL NOT call any `FindMember`-based overload on the array value itself, because `rapidjson::Value::FindMember` asserts `IsObject()` on non-object values (document.h:1154) and returns `MemberEnd()` silently under `NDEBUG`. A missing top-level `"textures"` array SHALL be treated as the legacy pre-textures-array format and SHALL NOT cause `Scene::Load` to fail (the materials' inline textures and the registration pass still populate the `EntityManager`).

This requirement exists because `Scene::Save` always emits a top-level `"textures"` array (deduped by UUID from the `EntityManager`), so every saved scene must be reloadable without an assertion failure (debug builds) and without dropping the textures that live only in that array (release builds).

#### Scenario: Reload a saved scene in a debug build does not assert

- **WHEN** a `Scene` is saved via `FileUtil::SaveFile` or `SaveCompressedFile` (which emits a top-level `"textures"` array)
- **AND** the saved file is reloaded into a fresh `Scene` via `Scene::Load` in a build where `RAPIDJSON_ASSERT` is active (no `NDEBUG`)
- **THEN** `Scene::Load` returns true
- **AND** the process does not abort with `Assertion failed: (IsObject()), function FindMember`

#### Scenario: Reload a saved scene in a release build preserves top-level textures

- **WHEN** a `Scene` containing a texture that is referenced only by the top-level `"textures"` array (not re-emitted inline by any material) is saved and reloaded in a release build (`-DNDEBUG`)
- **THEN** the reloaded `Scene`'s `EntityManager` contains that texture (`ForEach<Texture>` finds it)
- **AND** the texture count after reload equals the texture count before save

#### Scenario: Legacy scene without a top-level textures array still loads

- **WHEN** a scene file saved before the top-level `"textures"` array existed (e.g. the bundled `Resource/Scene/scene.bin`) is loaded
- **THEN** `Scene::Load` returns true
- **AND** no error is logged about a missing `"textures"` member
- **AND** textures referenced inline by materials are registered in the `EntityManager` via the existing registration pass

### Requirement: `FileUtil::SaveByExtension` SHALL dispatch the save format by the output path's extension

`FileUtil` SHALL provide `SaveByExtension(source, filePath, maxDecimalPlaces = 5)` that selects the underlying serializer by lowercasing the extension of `filePath`: `.json` → `SaveFile`, `.bin` → `SaveCompressedFile`. For any other extension, it SHALL return false and log a `FURYE`-level message naming the unsupported extension. Memory-backed texture extraction (`ExtractMemoryBackedTextures`) SHALL run as part of the dispatched `SaveFile`/`SaveCompressedFile` call exactly as it does today — `SaveByExtension` SHALL NOT duplicate or skip that step.

This is the single canonical save-dispatch entry point used by the CLI (`convert scene`), the editor (`Editor.lua`'s `write_scene_to_path`), and any future caller that wants format-by-extension behavior.

#### Scenario: Save by .json extension writes a pretty-printed JSON file

- **WHEN** `FileUtil::SaveByExtension(scene, "/tmp/out.json")` is called
- **THEN** the file at `/tmp/out.json` is a UTF-8 JSON document (not LZ4-compressed)
- **AND** the call returns true

#### Scenario: Save by .bin extension writes an LZ4-compressed file

- **WHEN** `FileUtil::SaveByExtension(scene, "/tmp/out.bin")` is called
- **THEN** the file at `/tmp/out.bin` begins with the 8-byte LZ4 envelope (network-order original size + compressed size) produced by `SaveCompressedFile`
- **AND** the call returns true

#### Scenario: Save by an unsupported extension returns false

- **WHEN** `FileUtil::SaveByExtension(scene, "/tmp/out.xml")` is called
- **THEN** the call returns false
- **AND** a `FURYE`-level log message names `.xml` as unsupported and lists `.json` and `.bin`
- **AND** no file is written to `/tmp/out.xml`

#### Scenario: Memory-backed textures are extracted before serialization regardless of extension

- **WHEN** `FileUtil::SaveByExtension(scene, out_path)` is called on a `Scene` containing a memory-backed texture
- **AND** `out_path` ends in either `.json` or `.bin`
- **THEN** the memory-backed texture's encoded bytes are written to a sibling file in `dirname(out_path)` before the scene is serialized
- **AND** the texture's `m_FilePath` is set to the bare sibling filename in the serialized output

### Requirement: A `Scene` saved by the engine SHALL round-trip through `Scene::Save` then `Scene::Load` with stable structural counts

For any `Scene` produced by the engine (loaded from `.json`/`.bin`, imported from glTF/FBX, or assembled at runtime), the following round-trip SHALL preserve structural counts: save the scene to a temp file via `SaveByExtension`, load the temp file into a fresh `Scene`, and compare counts. The counts SHALL match exactly for: `nodes`, `meshes_total`, `meshes_static`, `meshes_skinned`, `submeshes`, `vertices`, `triangles`, `materials`, `textures`, `animations`, `joints`. AABB is reported but not compared (float rounding through serialization makes tight equality noisy).

#### Scenario: Round-trip preserves all structural counts

- **WHEN** a `Scene` is saved to a temp file and reloaded
- **THEN** every counted field (`nodes`, `meshes_total`, `meshes_static`, `meshes_skinned`, `submeshes`, `vertices`, `triangles`, `materials`, `textures`, `animations`, `joints`) is equal between the original load and the round-tripped load

#### Scenario: Round-trip of the bundled scene.bin succeeds

- **WHEN** the bundled `Resource/Scene/scene.bin` is loaded, saved to a temp `.bin`, and reloaded
- **THEN** all structural counts match between the two loads
- **AND** no assertion fires during either load (debug build)