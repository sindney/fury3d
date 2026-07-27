## ADDED Requirements

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

### Requirement: Switching to HDR requires a camera

When HDR mode is enabled and the active scene has no Camera component, the system SHALL prompt the user (reusing the confirm dialog) to add a Camera before HDR rendering proceeds.

#### Scenario: Enabling HDR on a scene without a camera

- **WHEN** the user switches the project to HDR and no Camera component exists in the scene
- **THEN** a dialog prompts to add a Camera, and accepting adds one via the component registry
