# particle-editor

## Purpose

Per-asset particle editor window in `furye`, opened from the Content Browser or
the Node Properties panel for any `ParticleSystem` / `ParticleRenderer`
component. Provides a module inspector (Emission, Shape, Velocity,
ColorOverLifetime, SizeOverLifetime, RotationOverLifetime, Renderer) plus a
3D preview pane that reuses the mesh editor's orbit camera, FBO, ground grid,
and AABB wireframe via the shared `Editor3DPreview` helper.

## ADDED Requirements

### Requirement: Double-clicking a ParticleSystem asset SHALL open the per-asset particle editor window

The editor MUST open a window titled `Particle: <system name>` of first-use
size `640 × 480` when the user double-clicks a `ParticleSystem` tile in the
Content Browser. Only one editor window per asset SHALL be open at a time;
double-clicking an already-open asset SHALL focus the existing window rather
than open a duplicate. Closing the window (Ok / Esc / click-outside) SHALL
NOT roll back widget edits — the editor is a live view of the asset, exactly
as the Material editor's `RenderMaterialEditorBody` is.

#### Scenario: Double-click opens the particle editor

- **WHEN** the user double-clicks a `ParticleSystem` tile named "FireEmber" in
  the Content Browser
- **THEN** a window titled `Particle: FireEmber` opens on the next frame

#### Scenario: Double-clicking an already-open system focuses the existing window

- **WHEN** the `Particle: FireEmber` window is already open
- **AND** the user double-clicks the `FireEmber` tile again
- **THEN** no second window opens
- **AND** the existing window is brought to the front

### Requirement: The particle editor SHALL render its module state via typed widgets

The particle editor body SHALL render, in order:

1. **Header**: the system's name (read-only label), the live particle count
   (`Update` is called by the editor preview loop, so this is non-zero after
   the first frame).
2. **Emission module**: a `DragFloat` for `rateOverTime` (per-second, range
   `[0, 1024]`, step 1), a "+" button that opens an inline `Burst` editor
   (time + count + probability), and a "-" button per existing burst.
3. **Shape module**: a `Combo` for `ShapeType` (`BOX` / `SPHERE` / `CONE`),
   plus `DragFloat3` for `scale` and `DragFloat` for `radius` (and `angle` for
   `CONE`).
4. **Velocity module**: `DragFloat3` for `linear`, `DragFloat` for `speed`,
   `Checkbox` for `inheritFromParent`.
5. **ColorOverLifetime module**: a gradient editor (start/stop colors + time
   sliders), same data model as the spec's `ColorOverLifetimeModule.color`
   gradient.
6. **SizeOverLifetime module**: a curve editor (multi-key, multi-component
   X/Y/Z), values sampled as multiplier on initial size.
7. **RotationOverLifetime module**: `DragFloat` for `angularVelocity`
   (degrees/second).
8. **Renderer module**: a `Combo` for `blendMode` (`ALPHA` / `ADDITIVE`) and
   a material-binding row identical in shape to `RenderMaterialTextureRow`
   (48×48 thumbnail + name + meta + Browse… button).

All edits SHALL apply to the underlying `ParticleSystem` on the same frame
the widget changes, mirroring the existing Material editor's live-edit
pattern.

#### Scenario: Editing emission rate updates the live count

- **WHEN** the user changes the `rateOverTime` slider from `30` to `300`
- **THEN** the live particle count grows by ~270 over the next second of
  preview time

#### Scenario: Browsing a texture rebinds the material

- **WHEN** the user clicks the Renderer's "Browse…" button and picks
  `Resource/Particle/smoke.png` from the texture picker
- **THEN** the renderer's `material` slot is reassigned to the picked
  texture's material
- **AND** the preview updates on the next frame

### Requirement: The particle editor SHALL preview the live system in a 3D viewport that shares the mesh editor's preview infrastructure

The particle editor's preview pane SHALL render the live `ParticleSystem` into
the same kind of FBO the mesh editor uses, via the shared
`Editor3DPreview` helper (see `asset-editor-windows` delta spec). The pane
SHALL expose the same orbit/zoom/pan camera controls (LMB orbit, wheel zoom,
RMB pan) and the same ground grid + AABB wireframe as the mesh editor. The
preview SHALL be driven by a manual preview-time slider (`[0, 10s]` range,
`[Play]` / `[Pause]` toggle) so the user can scrub the lifetime curves
without playing the scene. While the window is open the system's own
`Engine::OnUpdate` tick SHALL be suspended (`SetExternallyDriven`) so the
preview owns the clock — no double-advance while playing, no fighting the
paused/scrub Reset+seek path — and SHALL resume when the window closes.

#### Scenario: Open editor owns the simulation clock

- **WHEN** a particle editor window is open with `[Play]` active
- **THEN** the simulation advances exactly once per frame (preview-driven),
  not once from the preview plus once from the wall-clock tick
- **AND** closing the window resumes the wall-clock tick from the state the
  preview left behind

#### Scenario: Camera controls are shared with the mesh editor

- **WHEN** the user opens a Particle editor and a Mesh editor side by side
- **AND** orbits the particle preview by dragging LMB
- **THEN** the mesh preview's camera is unaffected
- **AND** the particle preview's camera rotates around the preview target

#### Scenario: Scrubbing the preview-time slider updates the simulation

- **WHEN** the user drags the preview-time slider from `1.0s` to `5.0s`
- **THEN** the live particles in the preview advance to the new age
- **AND** the `ColorOverLifetime` / `SizeOverLifetime` modules sample at the
  new `t01` value

#### Scenario: Pause halts the preview

- **WHEN** the user clicks `[Pause]`
- **THEN** the preview-time slider no longer advances
- **AND** the live particle state is frozen until `[Play]` is clicked

### Requirement: The particle editor SHALL be reachable from the Node Properties panel

The Node Properties panel MUST render an `Open Particle Editor…` button
when the user selects a `SceneNode` with an attached `ParticleSystem`
component. Clicking the button SHALL open the per-asset particle editor for
that system. The button MUST follow the same pattern as the existing
`Open Mesh Editor…` / `Open Material Editor…` buttons in the panel.

#### Scenario: Node Properties exposes the editor button

- **WHEN** the user selects a scene node whose components include a
  `ParticleSystem` named "FireEmber"
- **THEN** the Node Properties panel renders a button labeled
  `Open Particle Editor…`
- **AND** clicking it opens the `Particle: FireEmber` window