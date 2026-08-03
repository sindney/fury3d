# transparent-rendering (delta)

## MODIFIED Requirements

### Requirement: The transparent pass SHALL composite before the postprocess chain

The transparent pass SHALL write into the same composite target the
postprocess chain reads (`hdr_composite` in HDR, `gbuffer_light` in LDR), so
chain effects apply to the blended result. The transparent pass's bucket of
drawable units SHALL include `ParticleRenderer` instances in addition to
`MeshRender` instances whose material's alpha mode is `BLEND` or `MASK`.

#### Scenario: DOF blurs particle smoke

- **WHEN** a scene contains a `ParticleRenderer` (smoke emitter) and a DOF
  chain effect tuned to blur its depth range
- **THEN** the smoke plume is blurred like neighboring transparent geometry

#### Scenario: ParticleRenderer participates in back-to-front sort

- **WHEN** a scene contains two `ParticleRenderer` instances with centers at
  different camera distances
- **THEN** the farther emitter is drawn first
- **AND** the nearer emitter blends over the farther one on screen

#### Scenario: MASK particle (cutout smoke) renders as alpha-tested

- **WHEN** a `ParticleRenderer` has `blendMode = ALPHA` and the diffuse
  texture's alpha is below `0.5` over a region of the quad
- **THEN** that region shows the scene behind (fully discarded by the
  alpha-test branch)
- **AND** the rest of the quad blends normally

## ADDED Requirements

### Requirement: Forward shaders depth-testing against gbuffer_depth SHALL write matching linear gl_FragDepth

`gbuffer_depth` stores LINEAR view depth (`gl_FragDepth = -viewZ /
camera_far`, see `GBuffer.glsl`). Any forward shader that depth-tests against
it (the transparent pass) SHALL write the same linear value on every fragment
path — the default `gl_FragCoord.z` is post-perspective depth (≈ 1 − n/d) and
compares incorrectly via `GL_LESS` against linear depth (d/far), which makes
fragments fail against any opaque surface behind them while passing against
sky. This applies to `Forward.glsl` (mesh transparents) and `ParticleShader`
(particles) alike; `camera_far` is bound by `Shader::BindCamera`.

#### Scenario: particle composites over terrain, not just sky

- **WHEN** a `ParticleRenderer` is between the camera and opaque terrain
- **THEN** its fragments pass the depth test and blend over the terrain
- **AND** fragments behind opaque geometry are correctly occluded

### Requirement: The transparent pass SHALL apply each shadow-casting light's shadow to its direct contribution

In the per-light additive loop (mesh transparents) and the particle draw
block, a light with `cast_shadows` and a cached shadow map SHALL have its
direct contribution multiplied by the shadow factor sampled from that map
(point: cube radial-distance convention per deferred `PointLight.glsl`;
directional: single-map `z > 1 lit else z < tex` per deferred
`SunLight.glsl`; CSM and spot lights are out of scope and bind
`u_shadow_type = 0`). The base ambient/emissive pass (u_light_type 0)
SHALL NOT be shadowed — ambient lives outside any light. Shadow-map
temporaries SHALL be released only at end of Execute (the temporary pool
recycles by spec mid-frame, which would let a second same-spec caster's
draw overwrite the first light's map before the transparent pass reads it).

#### Scenario: Every declared sampler is bound every draw

- **WHEN** any shader declares a sampler (2D, cube, or array)
- **THEN** the draw path SHALL bind a matching-target texture to it even
  when the feature using it is off (dummies allowed) — core GL rejects
  `glDrawElements` on sampler/target mismatch, and an unbound sampler
  defaults to unit 0's texture, which silently kills the whole draw

#### Scenario: Shadowed light contributes nothing past its occluder

- **WHEN** a transparent surface is occluded from a shadow-casting point
  light but lit by a second non-casting light
- **THEN** the first light's additive contribution is zero in the occluded
  region and the second light's contribution is unaffected

### Requirement: Known transparent kinds SHALL include CPU-driven billboard particles

The engine SHALL recognize `ParticleRenderer` as a transparent kind alongside
`MeshRender` BLEND and `MeshRender` MASK. Documentation in
`docs/transparent-rendering.md` (or the relevant existing doc) SHALL list
`ParticleRenderer` under "known transparent kinds" with a one-line summary of
the CPU-driven billboard-quad implementation, the `ALPHA` / `ADDITIVE` blend
modes, and the lack of per-fragment lighting (the particle shader reads the
diffuse texture directly without scene lighting — particles are emissive in
v1).

#### Scenario: docs list particle rendering

- **WHEN** the docs are regenerated from the spec sources
- **THEN** the "known transparent kinds" section includes the particle entry

#### Scenario: particle shader does not sample scene lights

- **WHEN** a `ParticleRenderer` renders in a scene with two point lights
- **THEN** the output color is the diffuse texture color modulated by the
  color-over-lifetime sample, NOT multiplied by light contribution