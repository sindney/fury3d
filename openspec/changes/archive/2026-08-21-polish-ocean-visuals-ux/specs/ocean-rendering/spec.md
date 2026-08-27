## MODIFIED Requirements

### Requirement: The ocean shader SHALL replay baked waves and shade with scene lighting

The vertex shader SHALL displace vertices by sampling 2-3 baked displacement
bands (each with its own world-space tiling scale) as a pure function of world
position and time. The fragment shader SHALL combine band normals with detail
ripple normals, and shade with the same sun/sky lighting model as the rest of
the scene (GGX specular, sky reflection fallback), including water
absorption/scatter color by view depth, responding to sky time-of-day.

The fragment shader SHALL additionally: flatten band normals with view
distance (detail never exceeds the pixel footprint), widen the sun specular
lobe with view distance (the glitter path toward the sun), modulate the body
color by displaced wave height (dark troughs, sun-through-crest scatter
glow), and converge to the sky horizon color at range (distance fog). Band
fades SHALL be smooth functions of camera distance applied to BOTH the
vertex displacement and the fragment band normals together (never a
per-geometry-piece step - piecewise-constant fades read as V-shaped shading
seams at the square ring boundaries).

#### Scenario: Ocean responds to time-of-day

- **WHEN** the sky time-of-day changes from noon to sunset
- **THEN** the ocean's sun specular and sky reflection tint change consistently with the terrain's lighting

#### Scenario: Two oceans sharing one asset stay in sync

- **WHEN** two ocean nodes reference the same wave asset at different locations
- **THEN** overlapping regions show identical wave phase

#### Scenario: No ring seams at shading resolution

- **WHEN** the camera roams the infinite ocean at any pose
- **THEN** no V-shaped or linear shading discontinuities appear at ring boundaries

#### Scenario: Far water converges to the horizon

- **WHEN** looking at the open ocean toward the horizon
- **THEN** the water lerps into the sky's horizon haze with no hard skirt edge

### Requirement: The ocean SHALL render crest and shore foam

Crest foam SHALL come from the baked Jacobian foam maps, thresholded and
modulated by wind speed. Shore/contact foam SHALL be computed per fragment
from the difference between water depth and scene linear depth (the existing
gbuffer convention), fading in within the configured shore-foam depth range,
so foam rings appear around intersecting geometry and terrain waterlines.
Foam SHALL fade out with view distance (anti-aliasing).

#### Scenario: Floating object generates contact foam

- **WHEN** a static object intersects the water surface
- **THEN** a foam band appears at the intersection line with the configured width

#### Scenario: Island waterline foams

- **WHEN** terrain slopes through the water level
- **THEN** foam appears along the shoreline and follows it as waves animate

### Requirement: The editor SHALL expose ocean parameters and debug views

The inspector SHALL edit every OceanComponent field (with the wave asset
chosen via the asset picker), display the resolved wave source (GPU compute
/ baked asset, including why: compute unavailable, disabled by user, or
forced baked), and provide debug view toggles: ring-LOD wireframe, foam
mask, and displacement heatmap. The spectrum (GPU generation) section SHALL
be disabled with the resolved reason visible whenever `HasEffectiveCompute()`
is false. The same debug views SHALL additionally live in the viewport's
debug droplist (Ocean submenu) alongside the other debug views.

#### Scenario: Foam mask debug view

- **WHEN** the foam-mask debug toggle is enabled in the inspector or the viewport debug droplist
- **THEN** the viewport shows the foam term as a grayscale overlay and disabling the toggle restores normal shading

#### Scenario: Spectrum section disabled where compute is unavailable

- **WHEN** the inspector shows an ocean on a machine without effective compute
- **THEN** the spectrum fields render disabled with the reason visible, so the user knows they are inert
