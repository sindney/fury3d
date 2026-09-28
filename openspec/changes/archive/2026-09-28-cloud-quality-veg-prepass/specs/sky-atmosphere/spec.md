# sky-atmosphere (delta)

## REMOVED Requirements

### Requirement: The sky SHALL render a 2D cloud layer when enabled

**Reason**: The single-sample 2D deck cannot produce volumetric form (flat, hard-edged results). It is replaced by the ray-marched volumetric cloud model; see the `volumetric-clouds` capability for density, lighting, marching, and composite behavior, and the ADDED requirement below for sky-pass integration.

**Migration**: Scenes load unchanged - legacy cloud fields (`cloud_enable`, `cloud_coverage`, `cloud_altitude_km`, `cloud_thickness_km`, `cloud_scale`, `cloud_wind_speed`, `cloud_density`, `cloud_fade_km`) map onto the volumetric model (enable/coverage/altitude/thickness/wind/density/fade carry over directly; scale maps to base noise scale). The old single-sample shader path is deleted.

## ADDED Requirements

### Requirement: The sky pass SHALL composite the volumetric cloud layer with scene-depth occlusion

When clouds are enabled on the active `SkyAtmosphere`, the pipeline SHALL render the volumetric cloud layer (see `volumetric-clouds`) into its half-res target before `pass_sky`, and `pass_sky` SHALL composite it over the atmosphere output as `sky * (1 - a) + rgb`. Clouds SHALL be occluded by opaque scene depth (a mountain nearer than the cloud along the ray hides it) and SHALL occlude the sun/moon discs. When clouds are disabled or no `SkyAtmosphere` exists, the sky pass SHALL behave exactly as the no-cloud case does today (samplers bound to dummies, no extra target).

#### Scenario: Clouds occlude the sun disc

- **WHEN** an opaque cloud edge crosses the sun
- **THEN** the disc is hidden/dimmed by the cloud's alpha, with a bright silver lining at the edge

#### Scenario: No-sky scenes unchanged

- **WHEN** a scene without `SkyAtmosphere` renders
- **THEN** output is pixel-identical to before this change

## MODIFIED Requirements

### Requirement: SkyAtmosphere SHALL be a serializable scene component holding atmosphere and time-of-day parameters

`SkyAtmosphere` SHALL be a `Component` registered in
`SceneNode::ComponentRegistry` (name `SkyAtmosphere`) with: planet radius,
atmosphere thickness, Rayleigh/Mie/ozone coefficients, Mie phase g, ground
albedo, sun angular diameter, volumetric cloud settings (enable, slab
base/top km, coverage, cloud-type bias, base/detail noise scales, density
scale, wind vector, detail erosion strength, quality preset, HG eccentricity
forward/back, powder strength, ambient scale, fade distance, cirrus enable/
coverage/altitude/softness/scale), time-of-day settings (time in hours
[0,24), day length in real minutes, auto-advance flag, sun-from-TOD flag),
and a sun-light node name. It SHALL implement `Load`/`Save`/`Clone` per the
component contract. Scenes without the component SHALL load unchanged.
Scenes saved with the legacy 2D cloud fields SHALL load with those fields
mapped to their volumetric equivalents and new fields defaulted.

#### Scenario: Round-trip a sky component

- **WHEN** a scene containing a configured `SkyAtmosphere` node is saved and reloaded
- **THEN** all atmosphere, volumetric cloud, and time-of-day parameters and the sun-light name are restored exactly

#### Scenario: Legacy scenes load untouched

- **WHEN** a scene saved before this change (e.g. `outdoor_physics.bin`) is loaded
- **THEN** loading succeeds with no sky component present and rendering is pixel-identical to before

#### Scenario: Legacy cloud fields map forward

- **WHEN** a scene saved with 2D-deck cloud settings (coverage 0.6, altitude 2 km, wind 10 m/s) is loaded
- **THEN** the volumetric cloud block picks up coverage 0.6, slab base 2 km, wind 10 m/s, and defaults for the new fields, with no load error

### Requirement: SkyAtmosphere SHALL have an editor inspector section

`EditorNodeProperties` SHALL render a `SkyAtmosphere` section (registered in
`ComponentRenderTable` and the Add-Component menu) exposing TOD time
slider/day-length/auto-advance, sun-light name, the atmosphere parameters,
and the volumetric cloud block (slab range, coverage, type bias, noise
scales, erosion, lighting params, quality preset, cirrus sub-section),
marking the scene dirty on edit.

#### Scenario: Edit TOD in the inspector

- **WHEN** the user drags the time slider with sun-from-TOD set
- **THEN** the viewport sky, sun position, and light direction update immediately and the scene is marked dirty

#### Scenario: Edit cloud coverage in the inspector

- **WHEN** the user drags the coverage slider
- **THEN** the viewport clouds inflate/contract smoothly on the next frame and the scene is marked dirty
