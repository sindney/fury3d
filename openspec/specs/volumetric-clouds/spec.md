# volumetric-clouds Specification

## Purpose
TBD - created by archiving change cloud-quality-veg-prepass. Update Purpose after archive.
## Requirements
### Requirement: Cloud noise textures SHALL be generated at load via fragment passes and cached on disk

The renderer SHALL generate three tileable noise textures without compute shaders: a 128^3 RGBA texture (R = Perlin-Worley base noise built by dilating Perlin fBm with inverted-Worley billows; G/B/A = Worley fBm at increasing frequencies), a 32^3 RGB Worley detail texture, and a 2D RGB weather-map texture (R = coverage, G = cloud type, B = reserved). Generation SHALL use the existing `TEXTURE_3D` slice-rendering infra (one fullscreen pass per slice), run once per unique parameter set, and persist to a cache file under the content directory keyed by a parameter hash; subsequent runs SHALL load the cache. Generation failure SHALL log and fall back to clouds disabled, never crash.

#### Scenario: Cold start generates and caches noise

- **WHEN** the engine loads a scene with clouds enabled and no valid noise cache exists
- **THEN** the noise textures are generated via fragment passes, bound to the cloud shader, and written to the cache file

#### Scenario: Warm start loads cache

- **WHEN** a scene with clouds enabled loads with a valid cache for the current parameter hash
- **THEN** no generation passes run and the cached textures are used

#### Scenario: Headless run works

- **WHEN** `fury exec` runs headlessly with clouds enabled
- **THEN** noise generation (or cache load) succeeds and screenshots show clouds

### Requirement: Cloud density SHALL combine weather map, height gradients, and two-scale noise erosion

The density function SHALL be: weather-map sample (coverage, type) at the sample's world XZ; a height fraction `h` in [0,1] across the cloud slab; a per-type height gradient blended between stratus (thin band at base), cumulus (bulge peaking low, rounded top), and cumulonimbus (tall column) profiles by `type`; base shape = `remap(perlin_worley, 1 - detail_worley * erosion_strength, 1, 0, 1)` (remap, not multiply, so core density is preserved while detail carves edges) x height gradient; coverage applied as a remap erosion `remap(base, 1 - coverage, 1, 0, 1)` (never a threshold, so clouds inflate/contract smoothly); density reduced toward the slab bottom for wispy bases. The model SHALL produce visually rounded, plump cumulus at mid coverage and thin flat stratus at low coverage. Wind SHALL scroll only the noise sample offsets; coverage/type fields stay static so large-scale structure is art-stable.

#### Scenario: Cumulus reads plump

- **WHEN** coverage ~0.5, type biased to cumulus, and a screenshot is taken from ground view
- **THEN** clouds show rounded cauliflower tops, wispy bases, and edge detail (not flat hard-edged slabs)

#### Scenario: Coverage sweeps fill the sky

- **WHEN** coverage is animated from 0.0 to 1.0
- **THEN** clouds grow from scattered puffs to near-overcast without popping (density remap is continuous in coverage)

### Requirement: The cloud march SHALL be budgeted, adaptive, and ground-view only

Rays SHALL march a spherical shell between `cloud_base_km` and `cloud_top_km` (curved-earth so clouds sink into the horizon). The marcher SHALL use a two-LOD schedule: a cheap sampler (base noise + gradient only, larger steps) until density > 0, then step back and switch to the full sampler (detail erosion + lighting); consecutive empty cheap samples SHALL continue cheaply. Step count SHALL scale with in-shell ray length (more steps toward the horizon) up to a quality-preset cap, steps SHALL grow with distance from the camera, and marching SHALL early-exit at transmittance ~0 or above the shell. Density beyond `cloud_fade_km` SHALL dissolve into aerial-perspective haze. Camera-inside-cloud behavior is out of scope; rays starting above the slab top SHALL skip the march.

#### Scenario: Horizon clouds descend correctly

- **WHEN** the camera looks at the horizon with clouds enabled
- **THEN** distant clouds sit at the horizon line (spherical shell), not floating on a flat plane parallel to view

#### Scenario: Early exit keeps clear sky cheap

- **WHEN** half the screen is clear sky (coverage 0 in that region)
- **THEN** Tracy GPU timing for the cloud pass is materially lower than for a fully overcast frame

### Requirement: Cloud lighting SHALL use Beer-Powder, cone-sampled sun transmittance, and a phase function

Sun lighting SHALL integrate 6 cone-spread samples toward the sun through the density field, weighted to favor nearby density, combining Beer attenuation (`exp(-d)`) with a Powder in-scatter term (`1 - exp(-2d)`) so edges facing away from the sun darken while thin edges near the sun glow. A multi-lobe Henyey-Greenstein phase (base eccentricity g = 0.2 plus a stronger forward lobe for silver lining, tunable) SHALL modulate the sun contribution. Ambient SHALL be `pow(1 - coarse_density, 0.5)` - reusing the march's coarse sample as an outside-to-inside gradient, so no ambient march - modulated by height in slab and tinted by the sky's sun/sky colors so clouds match time-of-day automatically. Once accumulated alpha exceeds a threshold, lighting MAY switch to the cheap sampler (LOD).

#### Scenario: Silver lining toward the sun

- **WHEN** a screenshot is taken looking sunward at a cloud edge vs looking away
- **THEN** the sunward edge is brighter (forward scattering) and the anti-sun edge shows darker rims

#### Scenario: Sunset tints undersides

- **WHEN** time-of-day is set to sunset
- **THEN** cloud undersides shift warm/dark and tops catch warm light, matching the sky's palette

### Requirement: Clouds SHALL render at half resolution and composite over the sky with scene-depth clipping

The cloud pass SHALL render premultiplied-alpha clouds into a target sized relative to the render target (half res default, quarter at the Low preset; the current fixed 640x360 size SHALL become RT-relative). The march SHALL terminate at the scene's opaque depth (linear gbuffer depth convention) so clouds correctly sit in front of distant terrain, and `pass_sky` SHALL composite the cloud target over `hdr_composite` for ALL pixels (cloud over terrain where the cloud is nearer), while the sky LUT/sun/moon remain restricted to the sky mask as today. When clouds are disabled the pass SHALL be skipped entirely. A quality preset SHALL control render scale, step cap, and lighting sample count; an env override (`FURY_CLOUD_QUALITY`) SHALL force a preset for tests.

#### Scenario: Terrain occludes clouds

- **WHEN** a mountain silhouette stands in front of distant clouds
- **THEN** the mountain pixels show no cloud contribution

#### Scenario: Low clouds in front of far terrain

- **WHEN** a cloud along the ray is nearer than the terrain behind it
- **THEN** the cloud renders over the terrain, fading with its alpha (the march clipped at the terrain depth)

#### Scenario: Disabled clouds cost nothing

- **WHEN** clouds are disabled in the component
- **THEN** the cloud pass does not execute (no target allocation churn, no march)

### Requirement: A high-altitude 2D cirrus layer SHALL composite above the volumetric deck

A separate cheap 2D cirrus deck (scrolling tiling noise, no marching) SHALL be evaluated after the volumetric march, at a configurable altitude above the slab, attenuated by the volumetric layer's transmittance, tinted by sun/time-of-day, with independent coverage/softness settings. The two layers together SHALL read as a multi-layered sky from the ground.

#### Scenario: Two decks visible

- **WHEN** cirrus coverage > 0 and volumetric coverage > 0
- **THEN** thin high streaks are visible through gaps in the low plump deck

#### Scenario: Cirrus hidden under overcast

- **WHEN** the volumetric deck is fully overcast
- **THEN** cirrus contributes ~nothing (occluded by deck transmittance)

### Requirement: Cloud settings SHALL be serializable, editable, and animatable via existing component plumbing

`SkyAtmosphere` SHALL carry the new cloud block (slab base/top km, coverage, type bias, density scale, wind vector, detail erosion strength, step/quality preset, HG eccentricity, powder strength, ambient scale, cirrus settings) with Load/Save/Clone round-trip and inspector exposure. Legacy 2D-deck fields SHALL load without error; on load they map to nearest new equivalents where obvious (coverage, altitude, wind) and defaults otherwise.

#### Scenario: Round-trip new cloud settings

- **WHEN** a scene with configured volumetric clouds is saved and reloaded
- **THEN** every cloud parameter restores exactly and the rendered sky is unchanged

#### Scenario: Legacy scene loads

- **WHEN** a scene saved with the 2D-deck cloud fields loads
- **THEN** loading succeeds, clouds render with the new model using mapped/defaulted values, and no field parse error is logged

### Requirement: Cloud rendering SHALL meet a frame budget with verification hooks

On the reference dev machine the cloud pass SHALL target <= 2 ms GPU at 1920x1080 half-res with the default preset (Tracy GPU zone `Clouds`), and <= 1 ms at quarter res Low preset. Debug hooks SHALL exist: `FURY_CLOUD_QUALITY` env override, a freeze-wind flag for deterministic screenshots, and debug views (raw density, transmittance, step-count heatmap) selectable like existing postfx debug views.

#### Scenario: Budget measured

- **WHEN** Tracy captures the island scene with default clouds at 1080p
- **THEN** the `Clouds` GPU zone averages <= 2 ms over a camera sweep

#### Scenario: Deterministic screenshot

- **WHEN** wind is frozen via the debug flag and two screenshots are taken seconds apart
- **THEN** cloud pixels are identical between the two captures

