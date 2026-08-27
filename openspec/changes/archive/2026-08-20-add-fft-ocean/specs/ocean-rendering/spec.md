## ADDED Requirements

### Requirement: OceanComponent SHALL be a serializable scene component

`OceanComponent` SHALL be registered in `SceneNode::ComponentRegistry` (name
`OceanComponent`) with: geometry mode (finite grid / infinite), wave source
(Auto / Baked), wave data asset path, spectrum parameters used by GPU
generation (wind speed and direction, fetch, choppiness, damping, per-band
tile size, resolution and frame count, loop seconds, seed), water level,
finite grid size and resolution, infinite ring radii and densities, shading
parameters (absorption color, scatter color, specular/roughness, normal
detail strength, foam amount, shore-foam depth range), and an SSR
participation flag. It SHALL round-trip through scene save/load exactly
like other registered components.

#### Scenario: Ocean parameters survive scene round-trip

- **WHEN** a scene with a configured infinite ocean is saved and reloaded
- **THEN** every ocean parameter and the wave asset reference are restored

### Requirement: Finite mode SHALL render a displaced grid for bounded water areas

Finite mode SHALL build one static grid mesh of the configured size and
resolution, centered on the node's transform, displaced per-vertex by the
baked wave bands. It is intended for inland water areas inside terrain.

#### Scenario: Lake in a terrain basin

- **WHEN** a finite ocean node is placed over a terrain depression below its water level
- **THEN** the water renders only within the grid extent and animates with the baked waves

### Requirement: Infinite mode SHALL keep dense geometry under the camera without tessellation

Infinite mode SHALL use prebuilt concentric grid rings of decreasing radial
density (static ring LOD, since GL 3.3 has no tessellation) that follow the
active camera in world space, snapped to whole grid cells so vertices never
swim, plus a far skirt disc extending to the horizon. Total vertex count of
all rings at default settings SHALL stay within a stated budget.

#### Scenario: Camera travels and ocean follows

- **WHEN** the camera moves 1 km in any direction
- **THEN** the densest ring remains centered under the camera, vertex positions are stable (no swimming), and ring seams are not visible at shading resolution

#### Scenario: Vertex budget respected

- **WHEN** infinite mode uses default ring settings
- **THEN** the summed ring vertex count is at or below the stated budget (on the order of 250k vertices)

### Requirement: The ocean shader SHALL replay baked waves and shade with scene lighting

The vertex shader SHALL displace vertices by sampling 2-3 baked displacement
bands (each with its own world-space tiling scale) as a pure function of world
position and time. The fragment shader SHALL combine band normals with detail
ripple normals, and shade with the same sun/sky lighting model as the rest of
the scene (GGX specular, sky reflection fallback), including water
absorption/scatter color by view depth, responding to sky time-of-day.

#### Scenario: Ocean responds to time-of-day

- **WHEN** the sky time-of-day changes from noon to sunset
- **THEN** the ocean's sun specular and sky reflection tint change consistently with the terrain's lighting

#### Scenario: Two oceans sharing one asset stay in sync

- **WHEN** two ocean nodes reference the same wave asset at different locations
- **THEN** overlapping regions show identical wave phase

### Requirement: The ocean SHALL render crest and shore foam

Crest foam SHALL come from the baked Jacobian foam maps, thresholded and
modulated by wind speed. Shore/contact foam SHALL be computed per fragment
from the difference between water depth and scene linear depth (the existing
gbuffer convention), fading in within the configured shore-foam depth range,
so foam rings appear around intersecting geometry and terrain waterlines.

#### Scenario: Floating object generates contact foam

- **WHEN** a static object intersects the water surface
- **THEN** a foam band appears at the intersection line with the configured width

#### Scenario: Island waterline foams

- **WHEN** terrain slopes through the water level
- **THEN** foam appears along the shoreline and follows it as waves animate

### Requirement: The ocean SHALL integrate with the existing transparency and postfx chain

The ocean SHALL render in a dedicated pipeline pass placed between the sky
pass and the transparent pass, alpha-blending over the lit opaque scene in
the HDR composite target, writing depth (so SSR and later transparents test
against the displaced surface) and writing view normal + roughness to the
gbuffer normal target (so the existing SSR effect treats water as a
low-roughness reflector). Shore foam SHALL read a pre-pass copy of the scene
depth (never the depth attachment being written). The ocean SHALL remain
correct in all postfx debug views.

#### Scenario: SSR reflects scene objects on water

- **WHEN** SSR is enabled and an object stands next to the water
- **THEN** its reflection appears on the water surface and raymarches against the displaced water depth

#### Scenario: Postfx debug views handle water correctly

- **WHEN** the AO, SSR, and depth debug views are toggled
- **THEN** none show artifacts (double blending, missing depth, wrong brightness) attributable to the ocean

### Requirement: The editor SHALL expose ocean parameters and debug views

The inspector SHALL edit every OceanComponent field (with the wave asset
chosen via the asset picker), display the resolved wave source (GPU compute
/ baked asset, including why: compute unavailable, disabled by user, or
forced baked), and provide debug view toggles: ring-LOD wireframe, foam
mask, and displacement heatmap.

#### Scenario: Foam mask debug view

- **WHEN** the foam-mask debug toggle is enabled in the inspector
- **THEN** the viewport shows the foam term as a grayscale overlay and disabling the toggle restores normal shading
