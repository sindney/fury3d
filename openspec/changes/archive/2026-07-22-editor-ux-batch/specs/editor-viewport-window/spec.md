# editor-viewport-window (delta)

## ADDED Requirements

### Requirement: The Viewport window SHALL render a top toolbar hosting gizmo controls and the Debug Overlays combo

The Viewport window SHALL render a one-line toolbar row at the top of its content region, above the scene `ImGui::Image`; the scene image SHALL be sized to the content region remaining below the bar. The toolbar SHALL render whenever the Viewport window is visible, independent of scene or selection state. All toolbar controls SHALL share the single line — the Debug Overlays combo SHALL NOT wrap to a second line (right-alignment must `ImGui::SameLine()` before measuring the remaining width).

The toolbar's **left group** SHALL host, in order: the gizmo controls (Translate / Rotate / Scale radios, a single `Snap` checkbox) per the `editor-shell` gizmo requirement, and a `Grid` checkbox driving `PipelineSwitch::EDITOR_GRID` (equivalent to Settings → Editor → Show Grid).

The toolbar's **right group** — separated from the left group by a spring spacer (right-aligned via `ImGui::SetCursorPosX` or equivalent) — SHALL host the **Debug Overlays** multi-select combo (`Draw Light Bounds`, `Draw Mesh Bounds`, `Draw Custom Bounds`, `Draw OcTree Bounds`, `LOD Debug Colors`) relocated from the Profiler Perf tab, preserving its behavior: `ImGuiSelectableFlags_DontClosePopups` multi-select, preview text `(none)` / single name / `N overlays selected`, and per-frame `Pipeline::Active->SetSwitch(...)` for each entry. The Profiler window SHALL NOT render the combo anymore.

#### Scenario: Toolbar layout on one line

- **WHEN** the Viewport window is visible
- **THEN** the toolbar's left edge shows the gizmo Translate / Rotate / Scale radios, a `Snap` checkbox, and a `Grid` checkbox on one line
- **AND** the toolbar's right edge shows the Debug Overlays combo on the SAME line
- **AND** the 3D scene renders in the content region below the toolbar

#### Scenario: Grid checkbox drives the pipeline switch

- **WHEN** the user toggles the toolbar `Grid` checkbox
- **THEN** `Pipeline::Active->IsSwitchOn(PipelineSwitch::EDITOR_GRID)` returns the new value on the next frame
- **AND** the reference grid appears/disappears in the viewport

#### Scenario: Overlays toggle from the toolbar

- **WHEN** the user enables `Draw Mesh Bounds` in the toolbar's Debug Overlays combo
- **THEN** `Pipeline::Active->IsSwitchOn(PipelineSwitch::MESH_BOUNDS)` returns `true` on the next frame
- **AND** the viewport draws mesh bounds

#### Scenario: Combo is gone from the Profiler

- **WHEN** the Profiler window's `Perf` tab is visible
- **THEN** no Debug Overlays combo renders in it
