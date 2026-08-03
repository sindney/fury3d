# asset-editor-windows (delta)

## MODIFIED Requirements

### Requirement: Double-clicking a Mesh, Material, or ParticleSystem asset in the Content Browser SHALL open a per-asset editor window

The Content Browser SHALL open a per-asset editor window when the user
double-clicks a `Mesh`, `Material`, or `ParticleSystem` tile. The window
SHALL be scoped to that single asset with title `<AssetType>: <asset name>`.
Only one editor window per asset SHALL be open at a time; double-clicking an
already-open asset SHALL focus the existing window rather than opening a
duplicate. Closing the window (Ok / Esc / click-outside) SHALL NOT mutate
the asset beyond edits already applied through the editor's widgets — the
editor is a live view of the asset.

#### Scenario: Double-click opens the particle editor

- **WHEN** the user double-clicks a `ParticleSystem` tile named "FireEmber"
- **THEN** a window titled `Particle: FireEmber` opens

#### Scenario: Double-clicking an already-open particle system focuses the existing window

- **WHEN** the `Particle: FireEmber` window is already open
- **AND** the user double-clicks the `FireEmber` tile again
- **THEN** no second window opens
- **AND** the existing window is brought to the front

### Requirement: Editor 3D preview infrastructure SHALL be shared across Mesh, Material, and Particle editors via the Editor3DPreview helper

The engine MUST expose a single `Editor3DPreview` helper at
`engine/Fury/Editor/Editor3DPreview.{h,cpp}` that owns the orbit camera state
(`OrbitState`), per-window FBO (`PreviewRT`), ground grid, AABB wireframe,
and camera-projection helpers. The mesh editor's `RenderMeshPreview`, the
new particle editor's `RenderParticlePreview`, and any future editor with a
3D viewport MUST call into this helper rather than re-implementing the
orbit camera or the ground grid.

The helper SHALL expose at minimum:

- `Editor3DPreview::EnsureRT(popup_id, w, h) -> GLuint` — returns the FBO for
  the given popup, creating it on first call and resizing on width/height
  change.
- `Editor3DPreview::OrbitFor(popup_id) -> OrbitState&` — keyed access to the
  orbit state map.
- `Editor3DPreview::ComputeViewProj(orbit, aabb_center, aabb_radius, aspect) -> { view, proj }`
  — derives the view/projection matrices from the orbit state and the framed
  target's AABB.
- `Editor3DPreview::DrawGroundGrid(view, proj, aabb_center, aabb_min_y, aabb_radius)`
  — draws the grid lines + the AABB wireframe (the same two GL calls
  `EditorAssetWindows.cpp`'s `RenderMeshPreview` makes today).
- `Editor3DPreview::ApplyCameraInput(orbit, eye, io, radius)` — applies the
  existing LMB-orbit / wheel-zoom / RMB-pan logic.

The mesh editor's `RenderMeshPreview` SHALL be rewritten to delegate to these
helpers. The particle editor's `RenderParticlePreview` SHALL likewise
delegate. After the refactor, opening a Mesh editor and a Particle editor
side-by-side SHALL keep their orbit cameras independent (per-popup_id state).

#### Scenario: Mesh editor preview is unchanged after the refactor

- **WHEN** a Mesh editor is opened before the refactor
- **AND** the same Mesh editor is opened after the refactor
- **THEN** the camera framing, the ground grid, and the AABB wireframe are
  pixel-equivalent
- **AND** the per-popup_id orbit state is preserved across the refactor

#### Scenario: Particle editor preview uses the same orbit camera as the mesh editor

- **WHEN** the user opens a Mesh editor for "Cube" and a Particle editor for
  "FireEmber" side by side
- **AND** LMB-orbits the Mesh editor's preview
- **THEN** the Particle editor's preview camera does NOT move
- **AND** the Mesh editor's preview camera rotates around the cube

#### Scenario: Particle editor preview ground grid matches the mesh editor's

- **WHEN** the user opens a Particle editor with a single emitter whose
  AABB radius is `r`
- **THEN** the ground grid extent is `2r` (the same formula the mesh editor
  uses)
- **AND** the AABB wireframe draws the emitter's 8-corner / 12-edge box