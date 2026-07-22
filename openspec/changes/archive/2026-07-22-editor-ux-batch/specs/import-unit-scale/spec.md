# import-unit-scale

## ADDED Requirements

### Requirement: The editor SHALL detect undersized glTF/FBX imports by scene bounds

After a successful `Importer.LoadScene` of a `.gltf`, `.glb`, or `.fbx` file through the editor's File → Open or File → Import flows, the editor SHALL compute the imported scene's world-space AABB (union of all node world AABBs) and compare its largest dimension against 100 engine units (1 m, per the engine's 1 unit = 1 cm convention). A scene whose largest dimension is strictly under 100 units SHALL be treated as a suspected unit-mismatch (metre- or smaller-authored) import.

The check SHALL be skipped when: the detection setting is disabled (see the Settings requirement below), the imported scene has no finite AABB (empty scene, lights-only, or infinite bounds), or the import happened outside the editor flows (headless CLI, test scripts, `render-mesh`) — those paths SHALL never prompt.

The detection SHALL run for both Open (scene replacement) and Import (merge into active scene).

#### Scenario: Metre-authored glTF is flagged on Open

- **WHEN** the user opens a glTF whose scene world AABB measures 1.55 units across
- **THEN** the editor classifies the import as undersized (1.55 < 100)

#### Scenario: Centimetre-scale native import is not flagged

- **WHEN** the user imports a glTF whose scene world AABB measures 350 units across
- **THEN** no undersized classification occurs and no dialog appears

#### Scenario: Empty or lights-only import is skipped

- **WHEN** the user imports a glTF containing no meshes (no finite scene AABB)
- **THEN** the detection is skipped silently

### Requirement: The editor SHALL offer auto-scale via a Yes/No confirm dialog with a bounds-derived factor

When an import is classified as undersized, the editor SHALL queue a Yes/No confirm dialog via the existing `Editor::RequestConfirmDialog` helper (exposed to Lua). The dialog message SHALL state the measured largest dimension (in engine units and its cm equivalent) and the proposed scale factor. The proposed factor SHALL be the smallest power of 100 (100, 10000, 1000000, …) that brings the scaled largest dimension to at least 100 engine units.

Answering **Yes** SHALL multiply the local scale of the imported scene's root node(s) by the factor — for Open, before the imported scene replaces the active scene; for Import, on the imported root(s) being merged. The scale SHALL be applied to SceneNode transforms only; vertex, joint, and inverse-bind-matrix data SHALL NOT be baked or modified.

Answering **No** (or Esc / click-outside) SHALL leave the import unscaled. Either answer SHALL NOT block or re-prompt for the same completed import; a multi-file Import SHALL evaluate and prompt per file.

#### Scenario: Fox-sized import prompts with 100×

- **WHEN** the user imports a glTF measuring 1.55 units across and the dialog appears
- **THEN** the message proposes a 100× scale
- **AND** clicking Yes scales the imported root node(s) by 100 on each axis

#### Scenario: Sub-centimetre-scale import escalates past 100×

- **WHEN** the user imports an FBX-derived glTF measuring 0.05 units across
- **THEN** the proposed factor is 10000 (0.05 × 100 = 5 < 100, so the factor escalates one power of 100; 0.05 × 10000 = 500 ≥ 100)

#### Scenario: User declines

- **WHEN** the undersized dialog appears and the user clicks No
- **THEN** the imported scene's transforms are left unchanged

### Requirement: Settings → Import SHALL expose an enable/disable toggle for unit-scale detection

The Settings window's Import section SHALL contain a checkbox (label containing `Auto-Scale`) backed by an `auto_scale_detect` entry in the editor's import-flag map (`Editor::SetImportFlag` / `GetImportFlag`), following the existing `auto_default_sun` pattern. The flag SHALL default to `true`. When `false`, the undersized detection SHALL NOT run and no dialog SHALL appear for any import.

#### Scenario: Disabled flag suppresses the dialog

- **WHEN** the user unchecks the Auto-Scale detection checkbox in Settings → Import
- **AND** imports a glTF measuring 1.55 units across
- **THEN** no dialog appears and the import completes unscaled

#### Scenario: Default is enabled

- **WHEN** the editor runs with no prior persisted setting
- **THEN** `GetImportFlag("auto_scale_detect", true)` returns `true`
