# hdr-pipeline

## Purpose

HDR rendering in the prelight pipeline: float (`rgba16f`) lighting/composite targets, PBR lighting shaders with sensible defaults for non-PBR meshes, and a mandatory tonemap before the final sRGB composite. Ships as a separate pipeline JSON (`DefferedLightingPBR.json`); the LDR Lambert pipeline is untouched.

## Requirements

### Requirement: Prelight pipeline supports HDR rendering

The prelight pipeline SHALL support an HDR mode that renders lighting into floating-point render targets (`rgba16f`) rather than 8-bit targets, preserving values above 1.0 through the lighting stage.

#### Scenario: Rendering lit scene in HDR

- **WHEN** the pipeline is in HDR mode
- **THEN** the lighting/G-buffer targets use a floating-point format and retain HDR luminance until tonemapping

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
