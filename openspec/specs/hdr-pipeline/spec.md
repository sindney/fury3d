# hdr-pipeline

## Purpose

HDR rendering in the deferred pipeline: float (`rgba16f`) lighting/composite targets, PBR lighting shaders with sensible defaults for non-PBR meshes, and a mandatory tonemap before the final sRGB composite. Ships as a separate pipeline JSON (`DeferredLightingPBR.json`); the LDR Lambert pipeline is untouched.

## Requirements

### Requirement: Deferred pipeline supports HDR rendering

The deferred pipeline SHALL support an HDR mode that renders lighting and composite output into floating-point render targets (`rgba16f`) rather than 8-bit targets, preserving values above 1.0 through the lighting stage. HDR mode SHALL use the compact G-buffer's channel-appropriate formats; only targets that carry HDR luminance are required to be floating-point. Optional clustered local-light evaluation SHALL write the same HDR lighting output and obey the same PBR and tonemapping contracts as the fallback deferred path.

#### Scenario: Rendering lit scene in HDR

- **WHEN** the pipeline is in HDR mode with clustered lighting either enabled or disabled
- **THEN** the lighting/composite targets use floating-point formats, retain HDR luminance until tonemapping, and receive equivalent local-light contributions within image-comparison tolerance

### Requirement: HDR requires PBR materials and shaders

When the pipeline is in HDR mode, the lighting stage SHALL use PBR materials and shaders. Meshes lacking PBR inputs SHALL still render using sensible PBR defaults rather than failing.

#### Scenario: Lighting a PBR mesh in HDR

- **WHEN** an HDR scene lights a mesh with metallic/roughness inputs
- **THEN** the PBR lighting shader is used for that mesh

### Requirement: Tonemapping is mandatory in HDR

In HDR mode a tonemapping step SHALL always run before the final sRGB composite, so the presented image is valid LDR regardless of the render settings' user-selected effect chain.

#### Scenario: HDR composite always tonemaps

- **WHEN** an HDR frame is presented
- **THEN** an ACES tonemap is applied before the final sRGB encode even if the postprocess chain is otherwise empty

### Requirement: HDR toggle does not gate on camera presence

HDR mode MAY be toggled without any camera-component ceremony; the editor always owns a camera. (An earlier design prompted to force-add a Camera when switching to HDR; the prompt was dropped because camera-presence checks were unreliable — see the archived change's tasks.md.)

### Requirement: The PBR pipeline SHALL include a sky pass between combine and transparent

`DeferredLightingPBR.json` SHALL declare a `pass_sky` pass ordered after
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

`DeferredLightingLambert.json` and its shaders SHALL gain no sky pass and no
aerial-perspective behavior; sky atmosphere is an HDR-only feature.

#### Scenario: Lambert scene unchanged

- **WHEN** any scene (with or without a `SkyAtmosphere`) renders through the Lambert pipeline
- **THEN** output is identical to before this change
