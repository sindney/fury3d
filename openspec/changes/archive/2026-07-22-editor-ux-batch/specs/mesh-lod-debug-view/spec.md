# mesh-lod-debug-view (delta)

## RENAMED Requirements

- FROM: `### Requirement: The editor SHALL provide a "LOD Debug Colors" toggle in the Profiler FPS tab`
- TO: `### Requirement: The editor SHALL provide a "LOD Debug Colors" toggle in the Viewport top toolbar`

## MODIFIED Requirements

### Requirement: The editor SHALL provide a "LOD Debug Colors" toggle in the Viewport top toolbar

Inside the Viewport window's top toolbar (right group — see the `editor-viewport-window` capability), the editor SHALL render a **Debug Overlays** multi-select combo (`ImGui::BeginCombo` with `ImGuiSelectableFlags_DontClosePopups`) that includes `LOD Debug Colors` as one of the entries alongside `Draw Light Bounds`, `Draw Mesh Bounds`, `Draw Custom Bounds`, and `Draw OcTree Bounds`. The combo's preview text SHALL show `(none)` when nothing is selected, the single entry name when exactly one is selected, or `N overlays selected` otherwise. The toggle's static state SHALL default to `false`. Toggling the entry calls `Pipeline::Active->SetSwitch(PipelineSwitch::LOD_DEBUG_COLORS, lod_debug_on)`.

When `WITH_EDITOR` is OFF, neither the combo nor the toggle exist.

#### Scenario: Toggling the entry flips the engine switch

- **WHEN** the user clicks `LOD Debug Colors` in the Debug Overlays combo
- **THEN** `Pipeline::Active->IsSwitchOn(PipelineSwitch::LOD_DEBUG_COLORS)` returns the new value on the next frame

#### Scenario: Combo preview reflects selection state

- **WHEN** zero overlays are selected
- **THEN** the combo's preview text reads `(none)`

- **WHEN** exactly one overlay is selected
- **THEN** the preview text reads that overlay's name (e.g. `Draw Light Bounds`)

- **WHEN** multiple overlays are selected
- **THEN** the preview text reads `N overlays selected` where N is the selection count

#### Scenario: Default state is off

- **WHEN** the editor starts up with no prior session
- **THEN** `lod_debug_on` defaults to `false`
- **AND** `LOD_DEBUG_COLORS` is off in the pipeline (no green tint in the viewport)
