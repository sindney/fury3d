# postprocess-effects (delta)

## ADDED Requirements

### Requirement: Effect inputs MAY reference pipeline textures via reserved names

An effect descriptor's `inputs` list MAY use `$`-prefixed reserved names to bind pipeline textures instead of the previous chain output: `$gbuffer_depth`, `$gbuffer_normal`, `$gbuffer_diffuse`, `$hdr_light`, and `$scene` (explicit alias for the previous chain output). Non-prefixed input names SHALL keep their existing behavior (bound to the previous chain output). Reserved textures SHALL be bound read-only and SHALL NOT be ping-ponged or released by the chain runner. An unresolvable reserved name SHALL skip the effect with a warning, not abort the chain.

#### Scenario: SSAO binds depth and normals

- **WHEN** an effect declares inputs `["$scene", "$gbuffer_depth", "$gbuffer_normal"]`
- **THEN** the first sampler receives the previous chain output and the latter two receive the pipeline's G-buffer textures

#### Scenario: Existing effects unchanged

- **WHEN** an effect declares a plain input name (e.g. `"u_texture"`)
- **THEN** it is bound to the previous chain output exactly as before

#### Scenario: Missing reserved texture

- **WHEN** an effect declares `$gbuffer_depth` but the pipeline has no such texture
- **THEN** that effect is skipped with a logged warning and the remaining chain still executes

### Requirement: G-buffer depth SHALL be exposed to effects as linear depth

The pipeline's `gbuffer_depth` texture stores view-space depth divided by camera far plane (written via `gl_FragDepth`). Effects sampling `$gbuffer_depth` SHALL receive this linear value directly in the red channel with no additional decode step.

#### Scenario: Depth reconstruction

- **WHEN** an effect samples `$gbuffer_depth` at a pixel covered by opaque geometry
- **THEN** the sampled value × camera far equals the view-space distance of that fragment

### Requirement: Chain order is engine-owned via declared stages

An effect descriptor SHALL declare its chain placement with a `stage` field (`pre_tonemap` | `tonemap` | `post_tonemap`) and MAY declare an integer `order` tie-breaker (default 0). When `stage` is omitted it SHALL be inferred: descriptors with any `$gbuffer_*` input are `pre_tonemap`, all others `post_tonemap`. The pipeline SHALL execute the enabled effects sorted by (stage, order, name) — `pre_tonemap` effects (SSAO/SSR) on linear scene color, `tonemap` (ACES) as the HDR→LDR pivot, `post_tonemap` effects (FXAA/CRT) last on LDR — regardless of the order entries appear in the scene file. Duplicate entries for the same effect SHALL collapse (first enabled wins).

#### Scenario: Saved order is ignored

- **WHEN** a scene's chain lists entries as `[ACES, FXAA]` in HDR mode
- **THEN** the runtime chain executes `[FXAA, ACES]` (canonical order)

#### Scenario: Stage inference

- **WHEN** an effect descriptor omits `stage` but declares a `$gbuffer_depth` input
- **THEN** it is treated as `pre_tonemap`

### Requirement: Tonemapping follows the HDR switch, not the chain

Exactly one tonemap-stage effect SHALL run whenever HDR mode is on: if no enabled entry resolves to a tonemap-stage effect, the pipeline SHALL inject the registered `ACES` effect (carrying overrides from a saved ACES entry when one exists); duplicate tonemap entries SHALL collapse with a warning. In LDR mode all tonemap-stage entries SHALL be stripped from the runtime chain. The tonemap entry's `enabled` flag SHALL be ignored in both modes (forced on in HDR, forced off in LDR).

#### Scenario: HDR chain without ACES gets one

- **WHEN** HDR is on and the enabled chain is `[FXAA]`
- **THEN** the runtime chain is `[FXAA, ACES]`

#### Scenario: LDR strips ACES

- **WHEN** HDR is off and the saved chain contains an enabled ACES entry
- **THEN** ACES does not run and the remaining effects apply in canonical order

#### Scenario: Disabled ACES still runs in HDR

- **WHEN** HDR is on and the saved chain contains a disabled ACES entry with a `u_exposure` override
- **THEN** ACES runs with that override applied

### Requirement: Effect descriptors MAY declare editor help metadata

An effect descriptor MAY carry a top-level `description` string (shown as the chain row's hover tooltip) and per-uniform `tip` / `min` / `max` fields. The Edit dialog SHALL show the tip (plus the range when both bounds are present) as a hover tooltip on each uniform widget and SHALL clamp edited values into the declared range. Metadata SHALL round-trip through the descriptor's Save.

#### Scenario: Hovering a uniform shows its tip

- **WHEN** the user hovers `u_max_distance` in the SSR Edit dialog
- **THEN** a tooltip explains the world-unit semantics and shows the declared adjustment range

#### Scenario: Value clamped to range

- **WHEN** the user drags a uniform past its declared `max`
- **THEN** the stored override stays within the range

### Requirement: Chain draws SHALL establish their own render state

The chain runner SHALL NOT inherit blending, depth-test, depth-write, or cull state from pipeline passes; each effect draw SHALL execute with blending disabled, depth test disabled, and depth writes disabled. (Regression: the final chain draw once inherited `GL_BLEND` + `glBlendFunc(ONE, ONE)` left by the transparent pass's additive light loop, so single-effect chains accumulated into the render target every frame — progressive overexposure.)

#### Scenario: Single-effect chain after the transparent pass

- **WHEN** the runtime chain is a single effect (e.g. HDR with only the auto ACES) and the previous pass left additive blending enabled
- **THEN** the effect replaces the target contents instead of blending into them, and output is frame-stable

### Requirement: Chain entries SHALL support per-instance uniform overrides

A chain entry MAY carry uniform overrides (name → float1-4 value). During chain execution the effect's JSON-declared default uniforms SHALL be applied first, then the entry's overrides SHALL replace matching names. Overrides SHALL NOT mutate the shared effect descriptor; two entries referencing the same effect with different overrides SHALL render differently.

#### Scenario: Override applies to instance

- **WHEN** two chain entries reference the SSAO effect, one overriding `u_strength` to 2.0 and one using the default
- **THEN** the first renders with strength 2.0 and the second with the descriptor default

#### Scenario: Unknown override name ignored

- **WHEN** an override names a uniform the effect does not declare
- **THEN** the override is ignored and the effect renders with its declared defaults
