# project-render-settings

## ADDED Requirements

### Requirement: Render settings SHALL persist and edit clustered-lighting enablement

The scene `renderSettings` block SHALL serialize a `clustered_lighting` boolean, defaulting to `false` when absent. The Engine settings panel SHALL expose a clustered-lighting checkbox that updates the active pipeline and marks the scene dirty. The saved value represents the user's request; the active pipeline SHALL independently resolve backend capability and fall back without rewriting the saved preference. The panel SHALL indicate when the requested mode is unavailable on the active backend.

#### Scenario: Clustered setting round-trips

- **WHEN** a scene with `clustered_lighting` enabled is saved and reloaded
- **THEN** the setting remains enabled and the active pipeline uses clustering if supported

#### Scenario: Missing field remains backward compatible

- **WHEN** a scene saved without `clustered_lighting` is loaded
- **THEN** the setting defaults to disabled and the existing deferred path remains active

#### Scenario: Unsupported request does not alter the saved preference

- **WHEN** a scene requesting clustered lighting is opened on an unsupported backend
- **THEN** rendering falls back, the panel shows the unavailable state, and saving the scene preserves the requested boolean unless the user changes it
