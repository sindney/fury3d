## ADDED Requirements

### Requirement: The atmosphere shaders SHALL be shippable as engine defaults

The atmosphere shader files used by `SkyAtmosphere` (transmittance LUT, multi-scatter LUT, sky-view LUT, camera-volume, sky ray march, cloud layer, and the shared `AtmosphereCommon.glsl`) SHALL live at `examples/Resource/Shader/Atmosphere/`. The path is part of the engine's redistribution: any project that ships a `SkyAtmosphere` component reads its shader includes from this location.

The `Shader::Compile` include resolver (the existing `#include "Name.glsl"` rule) SHALL continue to resolve includes relative to the including file's directory, so the seven atmosphere shaders keep working without any path-prefix changes.

#### Scenario: project references the engine-default location

- **WHEN** a project creates a `SkyAtmosphere` node and the pipeline loads the atmosphere passes
- **THEN** the includes resolve from `examples/Resource/Shader/Atmosphere/`
- **AND** the project does not need to copy any atmosphere shader

#### Scenario: shared `AtmosphereCommon.glsl` stays single-sourced

- **WHEN** the bilinear re-render runs at noon vs sunset
- **THEN** both atmosphere shaders compile from the same `AtmosphereCommon.glsl` and inherit the same constants

### Requirement: The cloud and moon textures SHALL be shippable as engine defaults

The two textures the `SkyAtmosphere` component currently loads from the project's `Terrain/` folder (`cloud_noise.png` and `moon.png`) SHALL be promoted to `examples/Resource/Texture/Sky/`. The component's path lookup SHALL be the new engine-default location; the project MAY override the lookup by setting the path explicitly on the Lua side.

The promoted assets SHALL be byte-identical copies of the existing per-project assets (no resampling, no recompression) so projects that switch to the new path see no visual diff.

#### Scenario: project uses the engine-default cloud texture

- **WHEN** a project creates a `SkyAtmosphere` node with `SetCloudNoisePath` left at the default
- **THEN** the pipeline samples `examples/Resource/Texture/Sky/cloud_noise.png`
- **AND** the rendered clouds match the pre-move pixel result

#### Scenario: project uses the engine-default moon texture

- **WHEN** a project creates a `SkyAtmosphere` node with `SetMoonTexturePath` left at the default
- **THEN** the pipeline samples `examples/Resource/Texture/Sky/moon.png`
- **AND** the rendered moon disc matches the pre-move pixel result

### Requirement: A `examples/Resource/SKY-README.md` SHALL document the canonical setup

The `SKY-README.md` SHALL:

- List the engine-default files: `Shader/Atmosphere/AtmosphereCommon.glsl`, `TransmittanceLut.glsl`, `MultiScatterLut.glsl`, `SkyViewLut.glsl`, `CameraVolume.glsl`, `SkyRayMarch.glsl`, `CloudLayer.glsl`, `Texture/Sky/cloud_noise.png`, `Texture/Sky/moon.png`.
- Provide a minimal Lua snippet that creates a `SkyAtmosphere` node bound to a directional light, mirroring the two-line setup the project's `setup_terrain_sky_scene.lua` already uses.
- Note that the project can override any path through the existing `SetCloudNoisePath` / `SetMoonTexturePath` setters.
- Be ASCII-only and free of em-dashes / smart quotes.

The README SHALL be loadable as a reference from the `Projects/outdoor/README.md` cross-link.

#### Scenario: README explains the minimal setup

- **WHEN** a new project author reads `examples/Resource/SKY-README.md`
- **THEN** they can wire a sky into their scene in two lines of Lua
- **AND** they understand where the engine looks for the supporting assets

#### Scenario: README preserves the override path

- **WHEN** a project wants to use a custom cloud noise texture
- **THEN** the README documents the `SkyAtmosphere:SetCloudNoisePath(path)` setter
- **AND** the engine-default path remains the implicit fallback
