# scene-round-trip

## Purpose

Covers the contract that a `Scene` saved by the engine can be reloaded into a fresh `Scene` with stable structural counts — including the `Scene::Load` reload path (which must not assert or silently skip the top-level `textures` array), the `FileUtil::SaveByExtension` dispatch helper that selects the save format by extension, and the round-trip count-stability invariant under the `asset-path-identity` model (path-keyed `EntityManager`, no same-name duplicates possible).
## Requirements
### Requirement: `Scene::Load` SHALL read the top-level `"textures"` array without asserting or silently skipping

The loading branch of scene serialization SHALL walk the top-level `"textures"` array, when present, using the array-aware archive interface on the already-resolved member value. It SHALL NOT invoke object-member lookup on the array value itself. A missing top-level `"textures"` array SHALL be treated as the legacy pre-textures-array format and SHALL NOT cause loading to fail; materials' inline textures and the registration pass still populate the `EntityManager`.

The saving branch SHALL continue to emit the top-level `"textures"` array deduplicated by path from the path-keyed `EntityManager`, so every saved scene is reloadable without dropping textures that live only in that array.

#### Scenario: Reload a saved scene in a debug build does not assert

- **WHEN** a `Scene` is saved via `FileUtil::SaveFile` or `SaveCompressedFile` and reloaded into a fresh `Scene` in a build where `RAPIDJSON_ASSERT` is active
- **THEN** loading returns true
- **AND** the process does not abort with an `IsObject()` assertion from `FindMember`

#### Scenario: Reload a saved scene in a release build preserves top-level textures

- **WHEN** a `Scene` containing a texture referenced only by the top-level `"textures"` array is saved and reloaded in a release build
- **THEN** the reloaded `Scene`'s `EntityManager` contains that texture
- **AND** the texture count after reload equals the texture count before save

#### Scenario: Legacy scene without a top-level textures array still loads

- **WHEN** a scene file saved before the top-level `"textures"` array existed is loaded
- **THEN** loading returns true
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
- **AND** a a `FURYE`-level log message names `.xml` as unsupported and lists `.json` and `.bin`
- **AND** no file is written to `/tmp/out.xml`

#### Scenario: Memory-backed textures are extracted before serialization regardless of extension

- **WHEN** `FileUtil::SaveByExtension(scene, out_path)` is called on a `Scene` containing a memory-backed texture
- **AND** `out_path` ends in either `.json` or `.bin`
- **THEN** the memory-backed texture's encoded bytes are written to a sibling file in `dirname(out_path)` before the scene is serialized
- **AND** the texture's `m_FilePath` is set to the bare sibling filename in the serialized output

### Requirement: A `Scene` saved by the engine SHALL round-trip through `Scene::Save` then `Scene::Load` with stable structural counts

For any `Scene` produced by the engine, saving through `FileUtil::SaveByExtension` and loading into a fresh `Scene` SHALL preserve structural counts and semantic persistent state through the shared `Serialize` implementations. Structural counts SHALL match exactly for `nodes`, `meshes_total`, `meshes_static`, `meshes_skinned`, `submeshes`, `vertices`, `triangles`, `materials`, `textures`, `animations`, and `joints`. AABB is reported but not compared because float rounding makes tight equality noisy.

The semantic fingerprint SHALL cover node names and hierarchy, local transforms, component types and persistent properties, path-keyed resource references, mesh and material assignments, light parameters, particle configuration, skeletal animation and joint bindings, terrain and ocean configuration, and render settings. Floating-point values SHALL use a documented tolerance; identifiers, paths, ordering where semantically significant, and enumerated values SHALL match exactly. Transient GPU objects, runtime playback cursors, and editor-only state SHALL be excluded.

Under `asset-path-identity`, the texture count after round trip equals the unique path count, not the pre-dedupe entry count. Legacy same-name-different-UUID textures collapse to the canonical path entry, and material references SHALL resolve to that entry.

#### Scenario: Round-trip preserves structure and semantics

- **WHEN** a scene is saved to a temporary file and reloaded
- **THEN** every structural count matches exactly
- **AND** every field in the semantic fingerprint matches under its exactness or tolerance rule

#### Scenario: Canonical showcases round-trip in both formats

- **WHEN** the feature-showcase and island scenes are each round-tripped through temporary `.json` and `.bin` files
- **THEN** all structural and semantic checks pass for both formats
- **AND** the round-trip runs through the headless CLI without a graphics context

#### Scenario: Round-trip of the bundled legacy scene succeeds

- **WHEN** the bundled `Resource/Scene/scene.bin` is loaded, saved to a temporary `.bin`, and reloaded
- **THEN** all applicable structural counts and semantic fields match between the two loads
- **AND** no assertion fires during either load in a debug build

#### Scenario: Legacy same-name duplicates collapse on the first round-trip save

- **WHEN** a legacy scene contains multiple Texture entries at one path with distinct UUIDs
- **AND** the scene is saved via `SaveByExtension` and reloaded into a fresh `Scene`
- **THEN** the reloaded `EntityManager` holds exactly one Texture at that path
- **AND** the saved file contains exactly one Texture entry for that path
- **AND** affected material texture slots point at the canonical entry

### Requirement: Scene loads SHALL resolve relative texture paths against the loading scene's working dir

Textures resolve relative paths via `Scene::Path`, which prepends the ACTIVE scene's working dir. During a scene load (`FileUtil::LoadFile` / `LoadCompressedFile`), the active scene is typically the OLD one (startup placeholder or merge target), so saved scenes' bare texture filenames resolve against the wrong directory and never load. While loading a `Scene`, the engine SHALL bind that scene as `Scene::Active` for the duration of the load and restore the previous active scene afterward. Callers that want the loaded scene active continue to set it explicitly.

#### Scenario: Saved scene reloads with textures at startup

- **GIVEN** a scene saved with bare texture filenames (e.g. `"path": "wheels.jpg"`) whose image files sit next to the scene file
- **WHEN** the engine starts fresh and loads that scene (startup arg or File → Open)
- **THEN** the textures resolve against the scene file's own directory and upload (no `Binding dirty texture!` warnings, thumbnails render)

#### Scenario: Active scene is preserved across a load

- **WHEN** a script loads a scene B while scene A is active (e.g. for `MergeInto`)
- **THEN** after the load returns, `Scene::Active` is still scene A
- **AND** scene B's textures were still resolved against scene B's directory during its load

### Requirement: Scene files SHALL carry a format version field that gates loading

Scene serialization in the saving direction SHALL write a `"version"` integer at the scene root equal to `Scene::kFormatVersion`. The field SHALL be present in both plain-JSON and LZ4-compressed envelopes because both contain the same scene document.

Scene serialization in the loading direction SHALL read `"version"`, defaulting to 1 when the field is absent. A file whose version exceeds `Scene::kFormatVersion` SHALL be rejected with an explicit error naming the file's version and supported version. Loading SHALL NOT proceed and produce a partially loaded scene.

#### Scenario: Legacy file without a version field loads

- **WHEN** a scene file saved before versioning is loaded
- **THEN** it is treated as version 1 and loads normally

#### Scenario: New save carries the version

- **WHEN** a scene is saved to `.json` or `.bin`
- **THEN** the file's root contains the current `Scene::kFormatVersion` value

#### Scenario: Newer-than-supported file is rejected

- **WHEN** a scene file's version exceeds the supported version
- **THEN** loading fails with an error stating both versions
- **AND** no partially loaded scene is produced

### Requirement: Scenes SHALL round-trip the render settings block

A `Scene` saved by the engine SHALL round-trip the `renderSettings` block, including referenced pipeline, HDR/LDR mode, CSM enablement, and the ordered postprocess chain, with stable values in addition to the structural and semantic invariants. Scenes saved before this block existed SHALL still load, defaulting to LDR, pre-existing CSM behavior, and an empty postprocess chain. Camera component serialization remains covered by the semantic component fingerprint.

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
- **THEN** node count, hierarchy, and semantic node state round-trip exactly under the fingerprint rules

#### Scenario: Legacy leaked nodes are cleaned by the editor

- **WHEN** a scene file containing previously leaked `EditorCamera` nodes is loaded into `furye`
- **THEN** the editor removes the stale nodes on load and flags its fresh editor camera editor-only
- **AND** the next save produces a file with zero `EditorCamera` nodes

