# editor-reference-grid

## ADDED Requirements

### Requirement: The editor viewport SHALL render a toggleable infinite reference grid

The editor SHALL render a reference grid on the world XZ plane (Y = 0) in the Viewport window, implemented as a screen-space pass over the final scene color: world position reconstructed from the scene's depth, grid lines emitted with derivative-based (`fwidth`) anti-aliasing, alpha-blended over the scene. The pass SHALL NOT write depth and SHALL NOT disturb subsequent passes (gizmo, selection outlines, joint overlays, editor UI). The grid SHALL extend to the horizon (distance-faded) rather than being a fixed-size quad, and SHALL remain visible regardless of scene content or camera position.

#### Scenario: Grid renders over an empty scene

- **GIVEN** the grid toggle is on and the active scene is empty
- **WHEN** the Viewport renders
- **THEN** grid lines on the XZ plane are visible, fading with distance

#### Scenario: Grid respects scene depth

- **GIVEN** the grid toggle is on and a cube sits on the XZ plane
- **WHEN** the camera looks at the cube from above
- **THEN** grid lines are occluded by the cube's faces (no grid bleeding through geometry)

### Requirement: Grid visibility SHALL be user-toggleable and persisted

A `Grid` checkbox SHALL live in the Viewport toolbar's left group and a `Show Grid` checkbox in Settings → Editor; both SHALL bind the same editor-owned state (equivalent controls). The state SHALL **default to ON** and SHALL persist across runs via the FuryEditor imgui.ini settings handler (`ShowGrid=0|1` under `[FuryEditor][Editor]`, same mechanism as the `Gizmo` line). The persisted state SHALL be applied to `PipelineSwitch::EDITOR_GRID` every frame the Viewport is visible, so pipeline recreation (scene reload) cannot silently reset the user's choice. Toggles SHALL take effect on the next rendered frame without a scene reload or editor restart.

#### Scenario: Toggle persists across runs

- **WHEN** the user turns the grid off in the toolbar and restarts the editor
- **THEN** the next launch starts with the grid hidden and the checkbox unchecked

#### Scenario: Default is visible

- **WHEN** the editor starts with no prior persisted setting
- **THEN** the grid renders and both checkboxes are checked
