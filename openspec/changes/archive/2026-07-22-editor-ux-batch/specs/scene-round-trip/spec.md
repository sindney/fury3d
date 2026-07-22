# scene-round-trip (delta)

## ADDED Requirements

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

`Scene::Save` SHALL write a `"version"` integer at the scene root equal to `Scene::kFormatVersion` (currently 2). The field SHALL be present in both the plain-JSON (`.json`) and LZ4-compressed (`.bin`) envelopes, since both serialize the same document.

`Scene::Load` SHALL read `"version"`, defaulting to 1 when the field is absent (pre-versioning files). A file whose version exceeds `Scene::kFormatVersion` SHALL be rejected with an explicit error message naming the file's version and the supported version — the load SHALL NOT proceed and produce a partially-loaded scene.

#### Scenario: Legacy file without a version field loads

- **WHEN** a scene file saved before versioning (no `"version"` field) is loaded
- **THEN** it is treated as version 1 and loads normally

#### Scenario: New save carries the version

- **WHEN** a scene is saved to `.json` or `.bin` by this engine
- **THEN** the file's root contains `"version": 2`

#### Scenario: Newer-than-supported file is rejected

- **WHEN** a scene file whose `"version"` is 99 is loaded
- **THEN** `Scene::Load` fails with an error stating version 99 is newer than supported (2)
- **AND** no partially-loaded scene is produced
