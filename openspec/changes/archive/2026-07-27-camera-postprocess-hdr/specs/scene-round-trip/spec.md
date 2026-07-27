## ADDED Requirements

### Requirement: Scenes SHALL round-trip the render settings block

A `Scene` saved by the engine SHALL round-trip the `renderSettings` block — referenced pipeline, HDR/LDR mode, CSM enablement, and the ordered postprocess chain — with stable values, in addition to the existing structural-count invariants. Scenes saved before this block existed SHALL still load, defaulting to LDR + pre-existing CSM behavior and an empty postprocess chain. Camera component serialization is unchanged by this requirement.

#### Scenario: Round-trip preserves the postprocess chain

- **WHEN** a `Scene` with a postprocess chain in `renderSettings` is saved to a temp file and reloaded
- **THEN** the reloaded scene has the same effects in the same order with the same enabled flags

#### Scenario: Round-trip preserves HDR and CSM settings

- **WHEN** a `Scene` with HDR and CSM enabled is saved and reloaded
- **THEN** the reloaded scene reports HDR and CSM enabled

#### Scenario: Legacy scene without render settings still loads

- **WHEN** a scene saved before this change is loaded
- **THEN** `Scene::Load` returns true
- **AND** render settings default to LDR with the pre-existing CSM behavior and an empty postprocess chain
