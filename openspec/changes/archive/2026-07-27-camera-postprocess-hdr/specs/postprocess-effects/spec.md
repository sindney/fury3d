## ADDED Requirements

### Requirement: Postprocess effects are data-driven

A postprocess effect SHALL be defined by data — a JSON descriptor plus an external GLSL shader file — loaded through the same serialization machinery used for pipeline shaders/textures/passes. No effect SHALL require C++ changes to add, modify, or remove.

#### Scenario: Defining a new effect from JSON and GLSL

- **WHEN** a user creates an effect JSON referencing a `.glsl` file with uniforms and input/output declarations
- **THEN** the effect loads and becomes selectable without recompiling the engine

#### Scenario: Hand-editing an existing effect

- **WHEN** a user edits an effect's GLSL or JSON parameters directly
- **THEN** the changes take effect on reload with no code changes required

### Requirement: Effects run as fullscreen-quad passes

Each effect SHALL execute as a fullscreen-quad pass that samples its declared input texture(s) and writes to a declared output, allowing effects to be chained by ping-ponging intermediate buffers.

#### Scenario: Chaining two effects

- **WHEN** a camera chain contains two enabled effects
- **THEN** the first effect's output is fed as the second effect's input, and the final output is composited to the screen

### Requirement: Ships ACES tonemapping, FXAA, and retro CRT samples

The change SHALL provide three ready-to-use sample effects: an ACES filmic tonemapping operator for HDR-to-LDR mapping, an FXAA antialiasing effect implementing the NVIDIA FXAA 3.11 algorithm, and a retro CRT effect.

#### Scenario: Selecting ACES tonemapping

- **WHEN** the ACES effect is applied to an HDR input
- **THEN** the output is a tonemapped LDR image

#### Scenario: Selecting FXAA

- **WHEN** the FXAA effect is applied
- **THEN** aliased edges in the input image are smoothed per the FXAA algorithm

#### Scenario: Selecting retro CRT

- **WHEN** the CRT effect is applied
- **THEN** the output shows the CRT look (e.g. scanlines/curvature/vignette) driven by its configurable uniforms
