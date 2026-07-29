# project-render-settings (delta)

## MODIFIED Requirements

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
