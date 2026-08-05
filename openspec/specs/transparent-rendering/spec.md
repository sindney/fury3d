# transparent-rendering Specification

## Purpose
TBD - created by archiving change transparency-and-postfx. Update Purpose after archive.
## Requirements
### Requirement: Materials SHALL carry an alpha mode and alpha cutoff

A `Material` SHALL persist an alpha mode (`OPAQUE`, `MASK`, `BLEND`) and an alpha cutoff value (default 0.5) in addition to the legacy `opaque` flag. The `opaque` flag SHALL be derived as `alpha_mode != BLEND` so existing bucketing (`RenderQuery`) keeps working. Materials saved before this change (with only the `opaque` flag) SHALL load with mode derived from that flag: `opaque=true` → `OPAQUE`, `opaque=false` → `BLEND`.

#### Scenario: Round-trip alpha mode

- **WHEN** a material with alpha mode `MASK` and cutoff 0.33 is saved and reloaded
- **THEN** the reloaded material reports alpha mode `MASK` and cutoff 0.33

#### Scenario: Legacy material upgrade

- **WHEN** a scene saved before this change contains a material with `opaque=false`
- **THEN** it loads with alpha mode `BLEND` and default cutoff 0.5

### Requirement: MASK materials SHALL render as alpha-tested opaque

MASK materials SHALL render in the regular opaque G-buffer pass with a dedicated `ALPHA_TEST` shader variant: draw-time shader selection OR's an `ALPHA_TEST` bit into the material's texture flags, and the pipeline declares matching variants (`alpha_test` in the shader's texture list + `ALPHA_TEST` define), so the discard branch compiles only where needed. The G-buffer fragment shader SHALL `discard` fragments whose base-color alpha (diffuse texture alpha × diffuse color alpha) is below the material's alpha cutoff. Discarded fragments SHALL NOT write color or depth. Opaque (non-MASK) materials SHALL use shader variants without the discard branch.

#### Scenario: Foliage cutout

- **WHEN** a MASK material with cutoff 0.5 renders a textured quad whose texture alpha is 0.2 in one region and 0.9 in another
- **THEN** the 0.2 region shows the scene behind (fully discarded) and the 0.9 region renders fully opaque with correct deferred lighting

### Requirement: BLEND materials SHALL render in a forward transparent pass after lighting

The pipeline SHALL declare a transparent pass executing after the deferred combine pass and before the postprocess chain, drawing `RenderQuery::transparentUnits`. The pass SHALL enable alpha blending (`SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA`), test depth against the G-buffer depth, and NOT write depth. Transparent units SHALL be drawn back-to-front using the existing camera-distance sort. Each transparent unit SHALL be lit: one ambient/emissive base draw plus one additive (`ONE`/`ONE`) draw per scene light reusing the single-light uniform binding; the forward shader SHALL premultiply the diffuse term by alpha while leaving the specular term unmodulated, so fully transmissive glass (alpha 0) still shows highlights.

#### Scenario: Transmission-only glass shows highlights

- **WHEN** a material with alpha 0 (e.g. `KHR_materials_transmission` fallback) renders under a light
- **THEN** its surface is invisible except for specular highlights

#### Scenario: Glass over opaque geometry

- **WHEN** a BLEND material with base alpha 0.3 renders in front of an opaque wall
- **THEN** the wall shows through at ~70% contribution tinted by the glass color
- **AND** the glass surface receives scene lighting

#### Scenario: Occluded transparent fragment

- **WHEN** a transparent surface is fully behind an opaque surface
- **THEN** the transparent surface is not visible (depth test culls it)

#### Scenario: Two overlapping transparents

- **WHEN** two BLEND surfaces overlap on screen
- **THEN** the nearer surface blends over the farther one (painter's order)

### Requirement: The transparent pass SHALL sample shadow maps for every supported light kind

In the per-light additive loop (mesh transparents) and the particle draw block, a light with `cast_shadows` and a cached shadow map SHALL have its direct contribution multiplied by the shadow factor sampled from that map. The pipeline supports four shadow kinds (see `u_shadow_type` contract):

- **point** (`u_shadow_type = 1`): cube radial-distance compare per deferred `PointLight.glsl`.
- **dir single-map** (`u_shadow_type = 2`): `z > 1 lit, else z < tex` per deferred `SunLight.glsl` (bias lives in the caster polygon offset).
- **CSM** (`u_shadow_type = 3`): 2DArray `sampler2DArray shadow_buffer_csm` + 4 cascade matrices + `shadow_far` quarter-far splits; cascade select on LINEAR view depth then `z > 1 lit, else z < tex` per deferred `SunLight.glsl` CSM block.
- **spot** (`u_shadow_type = 4`): 2D map + spot view→shadow UV matrix; cone falloff lives in the per-light additive loop, the shadow sample multiplies the same radiance.

Mesh transparents evaluate each light per-unit in the additive loop. The particle draw block uses ONE shadow input per emitter (`ParticleRenderer`): the single dominant casting light, ranked by `intensity / (distance² + ε)` to the emitter's world position; if the dir light scores highest (or no spot/point competes) the dir channel is picked, with CSM when the `CASCADED_SHADOW_MAP` switch is on else single-map. The base ambient/emissive pass (`u_light_type = 0`) SHALL NOT be shadowed. Shadow-map temporaries SHALL be released only at end of Execute (the temporary pool recycles by spec mid-frame, which would let a second same-spec caster's draw overwrite the first light's map before the transparent pass reads it).

#### Scenario: Every declared sampler is bound every draw

- **WHEN** any shader declares a sampler (2D, cube, or array)
- **THEN** the draw path SHALL bind a matching-target texture to it even when the feature using it is off (dummies allowed) — core GL rejects `glDrawElements` on sampler/target mismatch, and an unbound sampler defaults to unit 0's texture, which silently kills the whole draw

#### Scenario: Shadowed light contributes nothing past its occluder

- **WHEN** a transparent surface is occluded from a shadow-casting point light but lit by a second non-casting light
- **THEN** the first light's additive contribution is zero in the occluded region and the second light's contribution is unaffected

#### Scenario: CSM receive on smoke with sun + CSM on

- **WHEN** the sun is a casting directional, `CASCADED_SHADOW_MAP` is on, and a smoke emitter is in tree shadow
- **THEN** the shadowed region of the emitter darkens per the cascaded compare (cascade selected on linear view depth) and the un-shadowed region stays bright

#### Scenario: Spot receive through occluders

- **WHEN** a casting spot light is aimed through an occluder at a smoke emitter and a BLEND pane
- **THEN** both surfaces darken in the cone's occlusion and remain lit outside it

#### Scenario: Particle picks dominant emitter-local shadow source

- **WHEN** a `ParticleRenderer` has 2 point + 1 spot + the sun casting, and the renderer is closer to the spot than to either point
- **THEN** the renderer receives shadows from the spot OR the sun (whichever dominates the per-renderer rank) — single shadow source per draw

### Requirement: The transparent pass SHALL composite before the postprocess chain

The transparent pass SHALL write into the same composite target the postprocess chain reads (`hdr_composite` in HDR, `gbuffer_light` in LDR), so chain effects apply to the blended result. The transparent pass's bucket of drawable units SHALL include `ParticleRenderer` instances in addition to `MeshRender` instances whose material's alpha mode is `BLEND` or `MASK`.

#### Scenario: DOF blurs transparent objects

- **WHEN** a scene contains a BLEND object and a DOF chain effect tuned to blur its depth range
- **THEN** the transparent object is blurred like neighboring opaque geometry

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

### Requirement: Transparency SHALL be validated against glTF sample assets

The engine SHALL render the `GlassVaseFlowers` glTF sample (BLEND glass + MASK foliage) and the `AlphaBlendModeTest` glTF sample (mixed OPAQUE/MASK/BLEND) with results visually matching the sample references: glass transmits background, cutout foliage has hard edges, and each test row exhibits its labeled alpha behavior.

#### Scenario: GlassVaseFlowers sample scene

- **WHEN** the GlassVaseFlowers sample scene is opened in the editor
- **THEN** the vase glass shows the flowers/background through it and the leaves render as cutouts

#### Scenario: AlphaBlendModeTest sample scene

- **WHEN** the AlphaBlendModeTest sample scene is opened
- **THEN** OPAQUE blocks render solid, MASK blocks cut out per their cutoff, and BLEND blocks blend with the background

