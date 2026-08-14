# engine-sky-defaults

## Purpose

Ship the atmosphere shaders and sky textures that `SkyAtmosphere` needs as engine-default files so any project can enable a sky without copying assets out of the outdoor project.

## Requirements

### Requirement: Atmosphere shaders live in `examples/Resource/Shader/Atmosphere/`

The seven shaders referenced by `SkyAtmosphere` (`AtmosphereCommon.glsl`, `TransmittanceLut.glsl`, `MultiScatterLut.glsl`, `SkyViewLut.glsl`, `CameraVolume.glsl`, `SkyRayMarch.glsl`, `CloudLayer.glsl`) SHALL live at `examples/Resource/Shader/Atmosphere/`.

The `Shader::Compile` include resolver keeps relative `#include` paths working: `#include "AtmosphereCommon.glsl"` inside an `examples/Resource/Shader/Atmosphere/*.glsl` shader resolves to the sibling file in the same directory.

#### Scenario: project loads atmosphere passes

- **WHEN** a scene creates a `SkyAtmosphere` node and the pipeline loads the atmosphere passes
- **THEN** the includes resolve from `examples/Resource/Shader/Atmosphere/`
- **AND** the project does not need to copy any atmosphere shader

### Requirement: Cloud and moon textures live in `examples/Resource/Texture/Sky/`

The two textures the `SkyAtmosphere` component loads for the cloud field and moon disc (`cloud_noise.png` and `moon.png`) SHALL live at `examples/Resource/Texture/Sky/`. They SHALL be byte-identical copies of the outdoor project's `Terrain/cloud_noise.png` and `moon.png` files.

#### Scenario: project uses engine-default textures via path prefix

- **WHEN** a project's setup script sets `sky:SetCloudNoisePath("Engine/Texture/Sky/cloud_noise.png")` and `sky:SetMoonTexturePath("Engine/Texture/Sky/moon.png")`
- **THEN** `Scene::ResolveAsset` routes them to `<cwd>/Resource/Texture/Sky/{cloud_noise.png,moon.png}`
- **AND** the runtime matches the pre-promotion pixel result

#### Scenario: project overrides via the existing setters

- **WHEN** a project sets an absolute path or a different relative path via `SkyAtmosphere:SetCloudNoisePath` / `SetMoonTexturePath`
- **THEN** the engine default is bypassed and the supplied path is used
