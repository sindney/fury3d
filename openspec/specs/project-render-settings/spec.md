# project-render-settings

## Purpose

The per-scene `renderSettings` block: the single home for project-level render configuration (referenced pipeline, HDR/LDR mode, CSM, ordered postprocess chain). Persisted with the scene, seeded into the active pipeline on load, and edited in the Engine settings panel.
## Requirements
### Requirement: Settings window sections open collapsed by default

The Settings window `Render` and `Postprocess` sections (which host the render-settings widgets) SHALL start in their collapsed state on first appearance. Headers remain interactive; clicking a header toggles open/closed. See `editor-settings-collapsed-default` for the global behavior.

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

The `renderSettings` block SHALL maintain a list of postprocess entries, each referencing an effect by name with an independent enabled flag and an optional map of per-instance uniform overrides. Entry order in the file is retained for round-tripping but SHALL NOT define execution order — the pipeline applies effects in the engine-owned canonical order (stage, order, name; see postprocess-effects). An entry whose effect name cannot be resolved SHALL be retained but treated as disabled rather than aborting the load. Entries saved before this change (without a `uniforms` field) SHALL load with empty overrides, i.e. effect defaults.

#### Scenario: Saved order does not drive execution

- **WHEN** a scene's chain entries are stored as `[CRT, SSAO]`
- **THEN** the pipeline executes SSAO first (pre-tonemap stage) and CRT last regardless of file order

#### Scenario: Toggling a single effect

- **WHEN** an effect entry's enabled flag is set to false
- **THEN** that effect is skipped while the remaining enabled effects still apply in canonical order

#### Scenario: Unresolved effect name

- **WHEN** the chain references an effect name that is not registered
- **THEN** the scene still loads and that entry is skipped during rendering

#### Scenario: Uniform overrides round-trip

- **WHEN** a chain entry overrides `u_strength` to 2.5 and the scene is saved and reloaded
- **THEN** the reloaded entry reports the same override and the effect renders with strength 2.5

#### Scenario: Legacy entry without uniforms

- **WHEN** a scene saved before this change contains a chain entry with only `effect` and `enabled`
- **THEN** it loads with empty overrides and the effect renders with its descriptor defaults

### Requirement: Render settings are editable in the Engine settings panel

The Engine settings panel SHALL expose the render settings — HDR/LDR toggle, CSM toggle, and the postprocess chain. The chain editor SHALL list every registered effect once, in canonical order with stage group labels (pre-tonemap / tonemap / post-tonemap), offering only an **Enabled** checkbox and an **Edit** button per effect — no add/remove/reorder UI. The tonemap-stage row SHALL display the HDR state and SHALL NOT be user-toggleable (tooltip explains it is automatic). Each **Edit** button SHALL open a per-effect settings dialog listing the effect's declared uniforms with type-appropriate widgets (scalar drag for float1, drag or color edit for float3/float4); edits write into the entry's uniform overrides (an entry is created on demand when the effect has none). Changes SHALL apply to the active pipeline and mark the scene dirty. The dialog SHALL offer a reset-to-defaults action that clears the entry's overrides. Saved entries that no longer resolve SHALL be shown flagged with a remove affordance.

#### Scenario: Toggling HDR in the settings panel

- **WHEN** the user toggles HDR/LDR in the Engine settings panel
- **THEN** the active pipeline switches mode and the scene is marked dirty

#### Scenario: Toggling CSM in the settings panel

- **WHEN** the user toggles CSM in the Engine settings panel
- **THEN** the pipeline's CSM switch updates and the setting is persisted with the scene

#### Scenario: Toggling an effect in the settings panel

- **WHEN** the user checks or unchecks an effect's Enabled checkbox
- **THEN** the chain entry in `renderSettings` is created or updated and the scene is marked dirty

#### Scenario: Tonemap row is automatic

- **WHEN** HDR is enabled
- **THEN** the tonemap row shows as enabled and its checkbox is disabled

#### Scenario: Tuning an effect uniform via the Edit dialog

- **WHEN** the user clicks a chain entry's Edit button and changes a uniform value in the dialog
- **THEN** the entry's overrides update, the change is visible in the viewport on the next frame, and the scene is marked dirty

#### Scenario: Reset to defaults

- **WHEN** the user clicks reset-to-defaults in the Edit dialog
- **THEN** the entry's overrides are cleared and the effect renders with its descriptor defaults

### Requirement: Render settings SHALL hold CSM tuning parameters and own cascade split computation

Alongside the CSM toggle, the `renderSettings` block SHALL serialize:
`csm_map_size` (per-cascade depth map resolution; only 512, 1024, 2048, and
4096 are accepted — other values are rejected — default 2048), `shadow_far`
(cascade range in cm; 0 = cover the camera's full far plane; default 20000),
and `csm_split_blend` (0 = linear, 1 = logarithmic, in-between blends,
clamped to [0, 1]; default 0.7).
`RenderSettings::ComputeCsmSplits(nearPlane, cameraFar, outSplits4)` SHALL be
THE single source of the 4 cascade far distances: the shadow-map render and
the light shader's cascade picker SHALL both derive their splits from it, and
the cascade range SHALL never stretch past the camera's far plane. The Engine
settings panel SHALL expose the three fields (map-size combo, shadow-far
drag, split-blend slider) and mark the scene dirty on edit.

#### Scenario: Splits blend linear to logarithmic

- **WHEN** `csm_split_blend` moves from 0 to 1
- **THEN** `ComputeCsmSplits` output interpolates between the linear and logarithmic split schemes

#### Scenario: Shadow far caps the cascade range

- **WHEN** `shadow_far` is below the camera's far plane
- **THEN** the outermost cascade ends at `shadow_far` rather than the camera far

#### Scenario: Legacy scenes get the shipped defaults

- **WHEN** a scene saved without the CSM tuning fields is loaded
- **THEN** `csm_map_size` / `shadow_far` / `csm_split_blend` default to 2048 / 20000 / 0.7 and rendering is unchanged

#### Scenario: Edit CSM settings in the panel

- **WHEN** the user changes the CSM map size or split blend in the Engine settings panel
- **THEN** the new values persist with the scene, the shadow maps rebuild at the new resolution, and the scene is marked dirty

