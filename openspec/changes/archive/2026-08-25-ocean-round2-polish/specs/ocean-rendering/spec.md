## MODIFIED Requirements

### Requirement: Infinite mode SHALL keep dense geometry under the camera without tessellation

Infinite mode SHALL use prebuilt concentric grid rings of decreasing radial
density (static ring LOD, since GL 3.3 has no tessellation) that follow the
active camera in world space, plus a far skirt disc extending to the horizon.
Every piece's origin SHALL snap to whole cells of the FINEST (center) cell
grid so neighboring pieces' shared edges coincide exactly - per-piece snapping
leaves world-space gaps that read as slits at grazing angles. The skirt's
inner radius SHALL overlap the last ring's square footprint so the join never
opens. Total vertex count of all rings at default settings SHALL stay within
a stated budget.

#### Scenario: Camera travels and ocean follows

- **WHEN** the camera moves 1 km in any direction
- **THEN** the densest ring remains centered under the camera, vertex positions are stable (no swimming), and ring seams are not visible at shading resolution

#### Scenario: No geometric gaps between pieces

- **WHEN** the infinite ocean is viewed at a grazing angle (camera 3-6 m over the water)
- **THEN** no background shows through between the center grid, ring frames, and skirt

#### Scenario: Vertex budget respected

- **WHEN** infinite mode uses default ring settings
- **THEN** the summed ring vertex count is at or below the stated budget (on the order of 250k vertices)

### Requirement: The ocean shader SHALL replay baked waves and shade with scene lighting

The vertex shader SHALL displace vertices by sampling the baked displacement
bands (each with its own world-space tiling scale) as a pure function of world
position and time. The fragment shader SHALL combine band normals - including
a third fine "chop" cascade (a few-meter tile, normals + foam only, never
displacing vertices, so buoyancy and physics stay on the two big bands) -
and shade with the same sun/sky lighting model as the rest of the scene
(GGX specular, sky reflection fallback), including water
absorption/scatter color by view depth, responding to sky time-of-day.

The fragment shader SHALL additionally: flatten band normals with view
distance (detail never exceeds the pixel footprint), widen the sun specular
lobe with view distance (the glitter path toward the sun), modulate the body
color by displaced wave height (dark troughs, sun-through-crest scatter
glow), and converge to the sky horizon color at range (distance fog). Band
fades SHALL be smooth functions of camera distance applied to BOTH the
vertex displacement and the fragment band normals together (never a
per-geometry-piece step - piecewise-constant fades read as V-shaped shading
seams at the square ring boundaries). The ripple band fade SHALL complete
before the first ring boundary so the fine band never straddles a
T-junction, and the swell band SHALL fade to zero across the last ring so
the coarsely-sampled skirt never aliases it.

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
Foam SHALL fade out with view distance (anti-aliasing). To hide the band
tiling period from elevated views, crest foam SHALL be modulated by a second
rotated, rescaled fetch of the band's foam channel, and the swell band
normals SHALL likewise blend a second rotated/rescaled fetch - multi-scale
sampling instead of a larger bake.

#### Scenario: Floating object generates contact foam

- **WHEN** a static object intersects the water surface
- **THEN** a foam band appears at the intersection line with the configured width

#### Scenario: Island waterline foams

- **WHEN** terrain slopes through the water level
- **THEN** foam appears along the shoreline and follows it as waves animate

#### Scenario: No visible tiling from above

- **WHEN** the open ocean is viewed from an elevated camera (hundreds of meters)
- **THEN** neither the crest foam nor the wave shading shows an obvious repeating pattern at the band tile size

### Requirement: The editor SHALL expose ocean parameters and debug views

The inspector SHALL edit every OceanComponent field, with the wave asset
chosen through the standard asset-reference row (Change / jump-to-browser /
clear chip + asset stats line, the Terrain heightmap precedent), display the
resolved wave source (GPU compute / baked asset, including why: compute
unavailable, disabled by user, or forced baked), and provide debug view
toggles: ring-LOD wireframe, foam mask, and displacement heatmap. The
spectrum (GPU generation) section SHALL be disabled with the resolved reason
visible whenever `HasEffectiveCompute()` is false. The same debug views SHALL
additionally live in the viewport's debug droplist (Ocean submenu) alongside
the other debug views. The droplist SHALL size to fit its rows when there is
room (no scrollbar) and SHALL NOT close when a row or submenu item is clicked
- only an outside click dismisses it, so multiple views can be toggled in one
session. The Content Browser SHALL list OceanWaves assets (filter entry,
badge, and a preview thumbnail when the bake's preview PNG exists), and the
editor's open-scene path SHALL transfer OceanWaves into the active scene's
EntityManager like every other asset type.

#### Scenario: Foam mask debug view

- **WHEN** the foam-mask debug toggle is enabled in the inspector or the viewport debug droplist
- **THEN** the viewport shows the foam term as a grayscale overlay and disabling the toggle restores normal shading

#### Scenario: Spectrum section disabled where compute is unavailable

- **WHEN** the inspector shows an ocean on a machine without effective compute
- **THEN** the spectrum fields render disabled with the reason visible, so the user knows they are inert

#### Scenario: OceanWaves visible after opening a scene

- **WHEN** a scene with a baked ocean is opened in the editor
- **THEN** the Content Browser lists the OceanWaves asset and the inspector's wave-asset picker is populated

#### Scenario: Droplist stays open for multi-toggle

- **WHEN** the user clicks rows inside the viewport debug droplist
- **THEN** the droplist stays open until the user clicks outside it

## ADDED Requirements

### Requirement: The wave bake SHALL ship human-viewable previews

The bake tool SHALL write PNG previews next to the binary payloads: one
normal-map frame per band and a max-projection of the foam channel. The
binary .f16/.u8 payloads remain the authoritative data (precision); previews
are for inspection and editor thumbnails.

#### Scenario: Previews render the wave field

- **WHEN** the bake completes
- **THEN** each band directory contains PNG previews that open in any image viewer and match the baked field

### Requirement: The default wave bake SHALL be an engine asset, with a storm sample as a project asset

The calm default bake SHALL live in the engine resource root
(`Resource/Ocean/`, referenced as `Engine/Ocean/ocean.json`) so any scene
can use it directly regardless of working directory, and the OceanComponent
default path SHALL point there. A strong-sea sample bake (storm: ~22 m/s
wind, ~2.5 m swell, visible whitecaps) SHALL live as a project sample asset
with its own demo scene.

#### Scenario: New ocean defaults to the engine bake

- **WHEN** a fresh OceanComponent is created in any scene
- **THEN** it resolves the engine baseline bake with no path setup

#### Scenario: Storm sample shows crest foam

- **WHEN** the storm sample scene is opened
- **THEN** whitecaps appear on crests while the calm baseline scenes show none

### Requirement: The bake SHALL produce full-spectrum bands per tile

Each baked band's Phillips spectrum SHALL use a tile-relative high-frequency
cutoff (on the order of two texels), so every band carries its full
wavelength range - a fetch-relative cutoff zeroes the small tiles' modes
(the 8 m band degenerates to a single-mode plane wave; shorter tiles to
zero amplitude).

#### Scenario: Ripple band is a spectrum, not a plane wave

- **WHEN** the default bake is inspected or rendered from an elevated view
- **THEN** the ripple band shows varied wavelengths instead of uniform 8 m dashes
