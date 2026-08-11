# hdr-pipeline (delta)

## ADDED Requirements

### Requirement: The PBR pipeline SHALL include a sky pass between combine and transparent

`DefferedLightingPBR.json` SHALL declare a `pass_sky` pass ordered after
`pass_combine` and before `pass_transparent`, writing into `hdr_composite`
with the sky-mask convention (gbuffer depth at far value). The pass SHALL be
driven by a dedicated `SKY` draw mode so atmosphere LUTs and uniforms are
bound from C++. With no enabled `SkyAtmosphere` in the scene the pass SHALL
no-op and leave `hdr_composite` bit-identical to before.

#### Scenario: Pass order keeps transparents over sky

- **WHEN** a sky-enabled HDR scene contains transparent surfaces
- **THEN** the frame order is gbuffer -> light -> combine (with aerial perspective) -> sky -> transparent -> postfx/final

#### Scenario: No sky, no cost, no change

- **WHEN** a scene without `SkyAtmosphere` renders in HDR
- **THEN** the sky pass performs no draw and the image matches the pre-change pipeline

### Requirement: The combine pass SHALL apply aerial perspective under a uniform gate

`PbrCombine.glsl` SHALL apply the camera aerial-perspective volume
(transmittance multiply + inscatter add) to opaque scene pixels when
`u_atmosphere_enabled != 0`, reconstructing view depth from the gbuffer depth
input. The atmosphere samplers (sky-view LUT, camera volume) SHALL be bound
on every combine draw, using dummy textures when no sky is active.

#### Scenario: Gate off preserves legacy output

- **WHEN** `u_atmosphere_enabled` is 0
- **THEN** combine produces the same output as the pre-change shader

### Requirement: The LDR Lambert pipeline SHALL be unaffected

`DefferedLightingLambert.json` and its shaders SHALL gain no sky pass and no
aerial-perspective behavior; sky atmosphere is an HDR-only feature.

#### Scenario: Lambert scene unchanged

- **WHEN** any scene (with or without a `SkyAtmosphere`) renders through the Lambert pipeline
- **THEN** output is identical to before this change
