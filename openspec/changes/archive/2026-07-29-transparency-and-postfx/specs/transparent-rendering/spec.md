# transparent-rendering (delta — new capability)

## ADDED Requirements

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

### Requirement: The transparent pass SHALL composite before the postprocess chain

The transparent pass SHALL write into the same composite target the postprocess chain reads (`hdr_composite` in HDR, `gbuffer_light` in LDR), so chain effects apply to the blended result.

#### Scenario: DOF blurs transparent objects

- **WHEN** a scene contains a BLEND object and a DOF chain effect tuned to blur its depth range
- **THEN** the transparent object is blurred like neighboring opaque geometry

### Requirement: Transparency SHALL be validated against glTF sample assets

The engine SHALL render the `GlassVaseFlowers` glTF sample (BLEND glass + MASK foliage) and the `AlphaBlendModeTest` glTF sample (mixed OPAQUE/MASK/BLEND) with results visually matching the sample references: glass transmits background, cutout foliage has hard edges, and each test row exhibits its labeled alpha behavior.

#### Scenario: GlassVaseFlowers sample scene

- **WHEN** the GlassVaseFlowers sample scene is opened in the editor
- **THEN** the vase glass shows the flowers/background through it and the leaves render as cutouts

#### Scenario: AlphaBlendModeTest sample scene

- **WHEN** the AlphaBlendModeTest sample scene is opened
- **THEN** OPAQUE blocks render solid, MASK blocks cut out per their cutoff, and BLEND blocks blend with the background
