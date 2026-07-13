## MODIFIED Requirements

### Requirement: `FileUtil::SaveFile` and `FileUtil::SaveCompressedFile` SHALL relocate texture pixel sources to sibling files before serializing the scene

When `FileUtil::SaveFile(scene, output_path)` (or `SaveCompressedFile`) is called on a `Scene`, the function SHALL, before invoking the underlying `Serializable::Save` traversal, ensure that every texture referenced by the scene is written as a sibling file of the output and referenced by a **bare filename** (no directory segments). This covers two texture states:

**Memory-backed textures** (`m_FilePath.empty() && !m_EncodedBytes.empty()`):

1. Compute the output's directory: `output_dir = dirname(output_path)`.
2. For each `Material` reachable from the scene (via the scene's `EntityManager`), iterate its texture map.
3. For each memory-backed `Texture`:
   1. Compute a target filename. Prefer `texture->GetOriginalFilename()` when non-empty (e.g. `body.jpg`); else fall back to `<output_stem>_<texture_name>.<ext>` where `<ext>` is derived from sniffing `m_EncodedBytes` (JPEG SOI `FF D8`, PNG magic `89 50 4E 47`, BMP `42 4D`).
   2. If a file named `<output_dir>/<target_filename>` already exists AND is byte-equal to `m_EncodedBytes`, SHALL skip the write and reuse the existing file.
   3. Otherwise, write `m_EncodedBytes` verbatim to `<output_dir>/<target_filename>`. On write failure, log via `FURYE` and abort the save (return false).
   4. Set `texture->m_FilePath = <target_filename>` (relative — resolved against `Scene::Path` on load).

**File-backed textures** (`!m_FilePath.empty()`) — this is the relocation that makes saved scenes portable:

1. Resolve the texture's current `m_FilePath` against the current `Scene::Path` (the source working directory) to find the real source image file on disk.
2. Compute `target_filename = basename(m_FilePath)` (strip any directory segments — e.g. `Resource/Scene/wheels.jpg` → `wheels.jpg`).
3. If `<output_dir>/<target_filename>` already exists AND is byte-equal to the resolved source file, SHALL skip the copy and reuse it.
4. Otherwise, if the resolved source file exists and differs from (or is absent at) the target, copy the source bytes to `<output_dir>/<target_filename>`. If the resolved source file cannot be found, the texture SHALL be left unchanged (its stored path is written as-is) and a `FURYW`-level warning SHALL be logged naming the missing source — the save SHALL still succeed.
5. On a successful copy/reuse, set `texture->m_FilePath = <target_filename>` (bare filename) so the serialized scene references a sibling of the output.

When two textures resolve to the same target filename with different bytes, the second SHALL get a numeric suffix (`<stem>_2.<ext>`) and its `m_FilePath` reflects the actual filename used. After all relocations complete, the normal `Serializable::Save` traversal runs; the serialized output now references bare sibling filenames only. Pixel bytes are never cleared from memory-backed textures, so a follow-on save to a different directory works without re-importing.

Because file-backed paths are rewritten to bare filenames resolved against the output directory, reopening a saved scene via `Importer.LoadScene` (which sets the working directory to the scene file's folder) resolves `<scene_dir>/<filename>` correctly and SHALL NOT produce the double-prepended path (`Resource/Scene/Resource/Scene/wheels.jpg`) that previously left the texture at 0×0.

#### Scenario: Save extracts embedded bytes to sibling files

- **WHEN** a user runs `./fury convert fbx tank.fbx /tmp/out.json`
- **AND** the FBX import produced 3 memory-backed textures with original filenames `wheels.jpg`, `body.jpg`, `grass.jpg`
- **THEN** `/tmp/out.json` exists
- **AND** `/tmp/wheels.jpg`, `/tmp/body.jpg`, `/tmp/grass.jpg` exist on disk with the embedded JPEG bytes
- **AND** `/tmp/out.json`'s texture entries have `path` values `wheels.jpg`, `body.jpg`, `grass.jpg` (relative, sibling to the output)

#### Scenario: File-backed textures are relocated to sibling files on save

- **WHEN** a scene loaded from `examples/Resource/Scene/scene.bin` (whose diffuse texture path is `Resource/Scene/wheels.jpg`) is saved to `examples/Resource/Scene/scene_lod.bin`
- **THEN** `examples/Resource/Scene/wheels.jpg` exists (copied/reused as a sibling of the output)
- **AND** the saved scene's texture entry has `path` value `wheels.jpg` (bare filename, no directory segments)

#### Scenario: Reopening a relocated scene loads the texture at full resolution

- **WHEN** the saved `scene_lod.bin` from the previous scenario is reopened via `Importer.LoadScene` (working directory set to the `.bin`'s folder)
- **THEN** `Texture::CreateFromImage` resolves `<scene_dir>/wheels.jpg` successfully
- **AND** the loaded texture's width and height are non-zero (not 0×0)
- **AND** no double-prepended path such as `Resource/Scene/Resource/Scene/wheels.jpg` is attempted

#### Scenario: Identical bytes already on disk are not rewritten

- **WHEN** the user re-saves the same scene to the same output path where the sibling image already exists byte-equal
- **THEN** the relocation step detects the byte-equal pre-existing file and skips the write
- **AND** the file's mtime is unchanged
- **AND** the saved JSON's path entries still resolve correctly

#### Scenario: Missing source file does not fail the save

- **WHEN** a file-backed texture's resolved source image cannot be found on disk
- **THEN** a `FURYW`-level warning names the missing source path
- **AND** the texture's stored path is written unchanged
- **AND** the save still returns true

#### Scenario: Filename collisions get numeric suffixes

- **WHEN** two textures resolve to the same target filename but have different bytes
- **THEN** the first written file keeps the base name
- **AND** the second is written with a `_2` suffix before the extension
- **AND** both textures' `m_FilePath` reflect their respective actual filenames
