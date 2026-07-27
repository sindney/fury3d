# project-render-settings

## Purpose

The per-scene `renderSettings` block: the single home for project-level render configuration (referenced pipeline, HDR/LDR mode, CSM, ordered postprocess chain). Persisted with the scene, seeded into the active pipeline on load, and edited in the Engine settings panel.

## Requirements

### Requirement: Scene owns a per-project render settings block

A `Scene` SHALL persist a `renderSettings` block containing at minimum: the referenced pipeline, HDR/LDR mode, Cascaded Shadow Map (CSM) enablement, and an ordered postprocess chain. This block is the single home for project-level render configuration and SHALL NOT be stored on the Camera component or on individual nodes.

#### Scenario: Persisting render settings

- **WHEN** a scene with HDR enabled, CSM enabled, and a postprocess chain is saved and reloaded
- **THEN** the reloaded scene reports HDR and CSM enabled and the same postprocess chain

#### Scenario: Loading a scene without render settings

- **WHEN** a scene from before this change is loaded
- **THEN** render settings default to LDR with the pre-existing CSM behavior and an empty postprocess chain, and the scene loads successfully

### Requirement: Render settings seed the active pipeline on load

On scene load, the `renderSettings` block SHALL configure the active pipeline: HDR/LDR mode, the CSM `PipelineSwitch`, and the resolved postprocess chain.

#### Scenario: Seeding CSM from settings

- **WHEN** a scene with CSM enabled in `renderSettings` is loaded
- **THEN** the active pipeline's CSM switch is turned on

#### Scenario: Seeding HDR mode from settings

- **WHEN** a scene with HDR enabled in `renderSettings` is loaded
- **THEN** the active pipeline renders in HDR mode

### Requirement: Render settings hold an ordered postprocess chain

The `renderSettings` block SHALL maintain an ordered list of postprocess entries, each referencing an effect by name with an independent enabled flag. The order SHALL define the order in which effects are applied. An entry whose effect name cannot be resolved SHALL be retained but treated as disabled rather than aborting the load.

#### Scenario: Reordering effects

- **WHEN** the user reorders effects in the chain
- **THEN** the pipeline applies them in the new order on the next frame

#### Scenario: Toggling a single effect

- **WHEN** an effect entry's enabled flag is set to false
- **THEN** that effect is skipped while the remaining enabled effects still apply in order

#### Scenario: Unresolved effect name

- **WHEN** the chain references an effect name that is not registered
- **THEN** the scene still loads and that entry is skipped during rendering

### Requirement: Render settings are editable in the Engine settings panel

The Engine settings panel SHALL expose the render settings — HDR/LDR toggle, CSM toggle, and postprocess chain editing (pick, reorder, toggle, remove effects, reusing the asset-picker modal). Changes SHALL apply to the active pipeline and mark the scene dirty.

#### Scenario: Toggling HDR in the settings panel

- **WHEN** the user toggles HDR/LDR in the Engine settings panel
- **THEN** the active pipeline switches mode and the scene is marked dirty

#### Scenario: Toggling CSM in the settings panel

- **WHEN** the user toggles CSM in the Engine settings panel
- **THEN** the pipeline's CSM switch updates and the setting is persisted with the scene

#### Scenario: Editing the postprocess chain in the settings panel

- **WHEN** the user adds, reorders, toggles, or removes an effect via the settings panel
- **THEN** the chain in `renderSettings` updates and the scene is marked dirty
