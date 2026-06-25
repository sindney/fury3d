## ADDED Requirements

### Requirement: `Texture` SHALL support textures whose pixel source is in-memory encoded bytes, not a file path

A `Texture` instance SHALL be in one of three states with respect to its pixel source:

1. **File-backed**: `m_FilePath` is non-empty; `m_EncodedBytes` is empty. GPU upload comes from `Texture::CreateFromImage(path, ...)`. Save serializes `path` only.
2. **Memory-backed**: `m_FilePath` is empty; `m_EncodedBytes` carries the encoded image bytes; `m_OriginalFilename` carries a hint about what the image was originally named (e.g. `body.jpg`). GPU upload comes from `Texture::CreateFromMemory(bytes, len, ...)`. Cannot be serialized in this state — see the "extract before serialize" requirement below.
3. **Procedural**: Both empty; pixels supplied via `CreateEmpty` + `SetPixels`. Save serializes `format`/`width`/`height`/etc., not `path`. Existing behavior, unchanged.

The `Texture` class SHALL provide getters / friend access sufficient for `FileUtil::SaveFile` and `FileUtil::SaveCompressedFile` to read `m_EncodedBytes` and `m_OriginalFilename`, and to set `m_FilePath` after extraction. The exact API choice (getters vs friend) is implementation-detail; the spec only mandates the data is accessible.

#### Scenario: Memory-backed texture renders correctly without a file

- **WHEN** an importer calls `texture->CreateFromMemory(jpeg_bytes, len, /*srgb=*/true, /*mipmap=*/true)`
- **AND** the resulting texture is used by a `Material` in a `Scene` that is rendered
- **THEN** the texture's GPU upload succeeds (`m_ID != 0`, sampling produces the decoded pixels)
- **AND** no file I/O is performed during the upload

#### Scenario: Memory-backed texture's encoded bytes survive until save time

- **WHEN** a memory-backed texture is created during import
- **AND** the engine then runs for many frames before any save is requested
- **THEN** `m_EncodedBytes` remains non-empty for the lifetime of the texture
- **AND** the recorded `m_OriginalFilename` is unchanged

### Requirement: `FileUtil::SaveFile` and `FileUtil::SaveCompressedFile` SHALL extract memory-backed texture bytes to disk before serializing the scene

When `FileUtil::SaveFile(scene, output_path)` (or `SaveCompressedFile`) is called on a `Scene` containing memory-backed textures, the function SHALL, before invoking the underlying `Serializable::Save` traversal:

1. Compute the output's directory: `output_dir = dirname(output_path)`.
2. For each `Material` reachable from the scene (via the scene's `EntityManager`), iterate its texture map.
3. For each `Texture` that is memory-backed (`m_FilePath.empty() && !m_EncodedBytes.empty()`):
   1. Compute a target filename. Prefer `texture->GetOriginalFilename()` when non-empty (e.g. `body.jpg`); else fall back to `<output_stem>_<texture_name>.<ext>` where `<ext>` is derived from sniffing `m_EncodedBytes` (JPEG SOI `FF D8`, PNG magic `89 50 4E 47`, BMP `42 4D`).
   2. If a file named `<output_dir>/<target_filename>` already exists AND its byte length matches `m_EncodedBytes.size()` AND a SHA-1 (or simpler — byte-for-byte compare) over its contents matches, SHALL skip the write and reuse the existing file. (Avoids redundant rewrites when the same scene is saved repeatedly.)
   3. Otherwise, write `m_EncodedBytes` verbatim to `<output_dir>/<target_filename>`. On write failure, log via `FURYE` and abort the save (return false from `SaveFile`).
   4. Set `texture->m_FilePath = <target_filename>` (relative — Texture::Save serializes this as-is, and Texture::CreateFromImage on the load path resolves it against `Scene::Path`).
4. After all extractions complete, run the normal `Serializable::Save` traversal. The serialized output now references real on-disk files.

When the user re-saves the same scene to a different output path, the extraction repeats relative to the new `output_dir`. Memory bytes remain on the texture (they are not cleared after extraction); a follow-on save to a different location SHALL therefore work without re-importing.

When two memory-backed textures hash to the same target filename (e.g. both `image.name = ""` and they get `<stem>_image0.jpg`), the second extraction SHALL append a numeric suffix (`<stem>_image0_2.jpg`) to keep filenames unique. The texture's `m_FilePath` reflects the actual filename used.

#### Scenario: Save extracts embedded bytes to sibling files

- **WHEN** a user runs `./fury convert fbx tank.fbx /tmp/out.json`
- **AND** the FBX import produced 3 memory-backed textures with original filenames `wheels.jpg`, `body.jpg`, `grass.jpg`
- **THEN** `/tmp/out.json` exists
- **AND** `/tmp/wheels.jpg`, `/tmp/body.jpg`, `/tmp/grass.jpg` exist on disk with the embedded JPEG bytes
- **AND** `/tmp/out.json`'s texture entries have `path` values `wheels.jpg`, `body.jpg`, `grass.jpg` (relative, sibling to the output)
- **AND** loading `/tmp/out.json` via `FileUtil::LoadFile` resolves the textures correctly

#### Scenario: Identical bytes already on disk are not rewritten

- **WHEN** the user has previously saved the same scene to `/tmp/out.json` (so `/tmp/body.jpg` already exists with the same bytes)
- **AND** the user re-saves the (otherwise identical) scene to `/tmp/out.json`
- **THEN** the extraction step detects the byte-equal pre-existing file and skips the write
- **AND** the file's mtime is unchanged
- **AND** the saved JSON's path entries still resolve correctly

#### Scenario: Save with no memory-backed textures is a no-op for extraction

- **WHEN** a user saves a scene whose textures are all file-backed (e.g. the scene was loaded from `scene.json` and never had embedded textures)
- **THEN** the extraction step iterates and finds nothing to extract
- **AND** no files are written other than the scene file itself
- **AND** the existing save behavior is byte-identical to the prior behavior

#### Scenario: Filename collisions get numeric suffixes

- **WHEN** two memory-backed textures both have `m_OriginalFilename` empty AND fall back to the synthesized `<stem>_image0.jpg` naming
- **THEN** the first extracted file is `<stem>_image0.jpg`
- **AND** the second is `<stem>_image0_2.jpg`
- **AND** both textures' `m_FilePath` reflect their respective actual filenames

### Requirement: Memory-backed textures SHALL load correctly from saved scenes after a restart

After a scene with memory-backed textures has been saved (which extracted bytes to sibling files), reloading that scene with `FileUtil::LoadFile` (or `LoadCompressedFile`) SHALL produce textures that render identically to the original import. The reloaded textures will be file-backed (`m_FilePath` set, `m_EncodedBytes` empty), since they're loaded via the standard `Texture::CreateFromImage` path at scene-load time.

#### Scenario: Round-trip from FBX through saved JSON renders identically

- **WHEN** a user runs `./fury convert fbx tank.fbx /tmp/out.json` (extracts the textures to `/tmp/`)
- **AND** then runs `./fury Demo.lua /tmp/out.json` (loads the saved scene fresh)
- **THEN** the rendered scene shows the same textured geometry as `./fury Demo.lua tank.fbx` (the direct FBX load)
