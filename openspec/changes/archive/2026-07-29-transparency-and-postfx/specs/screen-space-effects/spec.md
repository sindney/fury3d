# screen-space-effects (delta — new capability)

## REMOVED Requirements

### Requirement: The engine SHALL ship a depth-of-field postprocess effect

**Reason**: Depth of field proved too hard to tune across scene scales (world-unit focus parameters are unintuitive and the gather quality at large CoC was unacceptable) — dropped for simplicity.
**Migration**: Scenes with DOF chain entries keep loading (unresolved effect names are retained but skipped); the `DOF` effect descriptor and shader are deleted. Revisit as a two-pass CoC-scatter implementation if the feature returns.

## ADDED Requirements

### Requirement: The engine SHALL ship an SSAO postprocess effect

A screen-space ambient occlusion effect SHALL be provided as a data-driven effect descriptor + GLSL shader (no C++ changes). It SHALL reconstruct view-space positions from the linear G-buffer depth and normals from `gbuffer_normal`, sample a randomized hemisphere kernel, and darken occluded fragments. Tunable uniforms SHALL include at least kernel radius, occlusion strength, power, and depth bias.

#### Scenario: Corner darkening

- **WHEN** the SSAO effect is enabled on a scene with geometry meeting at a concave corner
- **THEN** the corner region darkens relative to open surfaces, scaled by the strength uniform

#### Scenario: Radius tunes the look

- **WHEN** the SSAO radius uniform is increased
- **THEN** occlusion spreads over a larger screen region

### Requirement: The engine SHALL ship a screen-space reflections postprocess effect

A screen-space reflections effect SHALL be provided as a data-driven effect descriptor + GLSL shader. It SHALL ray-march in view space against the linear G-buffer depth along the reflected view direction (decoded from `gbuffer_normal`), composite the hit color from the scene input, and attenuate by material roughness (`gbuffer_normal.a`) and screen-edge fade. Tunable uniforms SHALL include at least ray step count, hit thickness, and max ray distance.

#### Scenario: Floor reflects object

- **WHEN** the SSR effect is enabled on a scene with a low-roughness floor beneath an object
- **THEN** a screen-space reflection of the object appears on the floor

#### Scenario: Rough surfaces suppress reflections

- **WHEN** a surface's roughness approaches 1
- **THEN** its SSR contribution fades to zero

### Requirement: Screen-space effects SHALL compose with the chain

SSAO and SSR SHALL be toggleable chain entries in the `pre_tonemap` stage: they run on the linear scene composite before the tonemap pivot, with intermediates never sRGB-encoded. They SHALL work in both HDR (pre-ACES) and LDR (no tonemap runs) pipelines, since both declare the `$gbuffer_*` textures they consume.

#### Scenario: SSAO before tonemap in HDR

- **WHEN** an HDR chain enables SSAO
- **THEN** SSAO reads the linear composite before ACES and its output is tonemapped by ACES

#### Scenario: SSAO in LDR

- **WHEN** an LDR chain enables SSAO
- **THEN** SSAO composites over the LDR linear composite and the chain's terminal effect sRGB-encodes

### Requirement: Screen-space effects SHALL have buffer debug views

SSAO and SSR shaders SHALL carry a `DEBUG_VIEW` branch that outputs the effect's raw term (SSAO: AO factor as grayscale; SSR: composited reflection contribution). When the viewport toolbar's debug-view combo selects one, the pipeline SHALL render that variant standalone (effect need not be chain-enabled) into a `debug_view` texture, which the viewport presents in place of the scene and the Profiler's GBuffer tab also lists. SSR SHALL detect ray hits as depth sign crossings (ray passes behind a surface), rejecting crossings whose depth gap exceeds `u_thickness`; distances are world units and the shipped defaults SHALL be meter-scale (max distance ~1000, thickness ~25).

#### Scenario: AO view shows contact darkening

- **WHEN** the debug view is set to SSAO on an outdoor scene
- **THEN** the view shows white open surfaces with localized darkening at contact points and creases, not a uniform gray

#### Scenario: SSR detects hits at meter scale

- **WHEN** SSR runs on a scene with a low-roughness ground plane 5-15 m from other geometry
- **THEN** reflections of that geometry appear on the ground

### Requirement: SSR SHALL remain functional with HDR off

When the LDR (Lambert) pipeline is active, the chain runner SHALL compile effects with an `LDR` define (variant cached under a distinct key). SSR SHALL then take roughness from its `u_ldr_roughness` uniform for every surface, since the LDR gbuffer packs no roughness channel.

#### Scenario: SSR in LDR

- **WHEN** an LDR chain enables SSR with default `u_ldr_roughness` 0.35
- **THEN** reflections render instead of the effect being a passthrough

