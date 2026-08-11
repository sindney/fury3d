# Design: sky atmosphere + heightmap terrain

## Context

Fury3D is a GL 3.3 core deferred engine (gbuffer -> per-light accumulation ->
combine -> transparent -> postfx/final), data-driven from pipeline JSON +
single-file GLSL shaders (`#ifdef VERTEX_SHADER/FRAGMENT_SHADER`, no
`#include`, no UBOs, no compute, no tessellation). It has no sky rendering at
all (far-depth "sky mask" pixels pass clear color through the chain) and no
terrain primitive. Jolt physics is integrated (`BodySetup` statics bake world
matrices into shapes; cm units); `HeightFieldShape` is vendored but unused.
Scenes are built by repeatable `fury exec` Lua scripts; the outdoor physics
demo (`outdoor_physics.bin`, fox character controller) is the target demo
scene. Reference technique: sebh/UnrealEngineSkyAtmosphere (MIT), the EGSR
2020 UE sky — transmittance LUT 256x64, multi-scatter LUT 32x32 (the only
compute pass), sky-view LUT 192x108, camera scattering volume 32 slices x 4
km, all `float4` targets.

## Goals / Non-Goals

**Goals:**

- Physically-based sky (Rayleigh/Mie/ozone) with sun disc, textured moon with
  phase, 2D cloud layer, and aerial perspective on opaque geometry, in the
  HDR pipeline.
- Time-of-day that drives the scene's dominant directional light as the sun,
  with the sky and lighting sharing one sun direction.
- Unity-style heightmap terrain: chunked geomipmap LOD meshes, 4-layer
  slope/height splat, runtime height queries, Jolt heightfield collision.
- Stdlib-only Python generator producing every startup texture/asset.
- Repeatable demo-scene build script deriving `outdoor_terrain.bin`.

**Non-Goals:**

- Volumetric 3D clouds (UE VolumetricCloud with 3D worley + temporal
  upsampling) — scoped out; the 2D raymarched layer is the cloud answer.
- Sky ambient/IBL contribution to materials (ambient stays a material
  factor); atmosphere affects sky pixels and distance haze only.
- Terrain painting/editing tools, grass/detail/tree scattering, terrain
  holes, runtime heightmap modification.
- LDR Lambert pipeline support (sky is HDR-only).
- Real astronomy (orbital mechanics, stars field, moon phases from date) —
  TOD is parametric, not astronomical.

## Decisions

### D1: Port the LUT pipeline to fragment-shader passes; keep the reference math verbatim

The reference's only compute shader is the 32x32 multi-scatter LUT; its
groupshared tree reduction becomes a per-fragment loop over the same 64
stratified directions (each fragment independent — no barriers needed).
Transmittance/sky-view are already pixel shaders; the camera volume renders
one slice at a time into a 3D texture. All targets are `rgba16f`, which the
engine already renders into.

*Alternatives considered:* (a) CPU-precomputed tables baked to textures at
load — accurate but pushes physics code onto the CPU and blocks live
parameter editing; (b) upgrading to GL 4.3 for compute — rejected, breaks the
engine's GL 3.3 baseline for zero visual gain (LUT cost is one-time).

### D2: Add real `TEXTURE_3D` support instead of a 2D slice atlas

The aerial-perspective volume wants a 3D texture (32 slices). Adding
`TEXTURE_3D` is small: one enum value, a `glTexStorage3D` branch in
`Texture::CreateEmpty`, a `glFramebufferTexture3D` slice attach in `Pass`
(mirror of `SetArrayTextureLayer`), one dummy 3D texture in `RenderUtil`.

*Alternative:* pack slices into a 2D atlas and index manually in the shader —
no engine change, but manual filtering between slices and uglier sampling;
rejected because the 3D path is genuinely small and reusable (clouds detail
noise could later move to 3D).

### D3: Sky is a pipeline pass with a new `SKY` draw mode, not a postfx effect

The sky must land in `hdr_composite` **after** combine (it fills far-depth
pixels) and **before** transparent (water/glass must blend over sky). The
postfx chain runs after transparent, so a `PRE_TONEMAP` effect — the only
zero-C++ option — would composite sky over water. A `pass_sky` entry with
`DrawMode::SKY` lets `PrelightPipeline::DrawSky()` bind the component's LUTs,
3D volume, sun/moon/cloud uniforms and draw the unit quad with the sky-mask
stencil-free convention (`depth >= 1.0` in shader, matching SunLight/SSAO/SSR).

*Alternative:* inline hook in `Execute()` like `DrawEditorGrid` — works but
hides a render pass in C++ where passes are otherwise data-driven JSON; the
JSON entry keeps pass order visible and editable.

### D4: Aerial perspective lives in the combine shader, uniform-gated

Applying haze to opaque geometry needs scene color + depth together; the only
such point is `pass_combine`. Doing it in the sky pass would require reading
`hdr_composite` while writing it (undefined). So `PbrCombine.glsl` gains a
`u_atmosphere_enabled`-gated block sampling the camera volume by
reconstructed view depth. Uniform gate instead of a shader `define`
permutation because pipeline JSON permutations are load-time, and sky on/off
is a per-scene runtime condition. Samplers are always bound (dummy 3D/2D when
no sky) per the engine's sampler trap.

*Trade-off:* transparents get no aerial perspective (they shade after
combine). Acceptable for water/glass at demo scale; noted in specs.

### D5: One sun source of truth: the bound directional light

`SkyAtmosphere` names a sun light node (fallback: first `DIRECTIONAL` light,
mirroring the "dominant" notion users expect). With **sun-from-TOD on**, TOD
evaluates a parametric sun path (elevation/azimuth from time; noon = due
south overhead arc) and writes the light's rotation/color/intensity each
frame; the sky pass reads the light's direction back for rendering. With
sun-from-TOD **off**, direction flows the other way: the sky follows the
user-rotated light. This satisfies "if we have sky enabled then the dominant
dir light is used as sun light" without a parallel sun state to desync.

*Editor wrinkle:* auto-driving the light every frame would fight manual
rotation in the editor — hence the explicit flag; the inspector exposes it
next to the TOD slider.

### D6: TOD is parametric, moon is a cheap textured disc

TOD state = `timeHours`, `dayLengthMinutes`, `autoAdvance`, evaluated to sun
elevation `sin` curve + azimuth sweep. No orbital mechanics. The moon rides a
fixed offset from the sun (opposition at midnight), renders from a
Python-generated albedo with a Lambert-style terminator from the sun-moon
angle. Intensity crossfade: sun fades out through twilight, night keeps a
faint sky floor.

### D7: Clouds = half-res 2D layer march composited in the sky pass

A planar deck at configurable altitude: coverage noise (R channel of
generated RGBA noise) eroded by detail noise (G channel), ~24 density steps +
~6 light steps toward the sun, animated by `windSpeed * time`. Rendered into
a half-res `rgba16f` target, upsampled in `pass_sky` (depth-weighted upsample
only matters at horizon geometry intersections; plain bilinear acceptable at
the cloud deck's distance). Skip entirely when disabled — no target, no cost.

*Alternative:* full volumetric 3D clouds — rejected for scope (needs 3D
noise, temporal reprojection, much bigger shading budget; GL 3.3 can do it
but the payoff doesn't justify it here).

### D8: Terrain = one component building chunked geomipmap meshes through existing MeshRender machinery

`Terrain` (on one node) builds a `chunkCount^2` grid of child nodes, each
with a `MeshRender` whose mesh carries a geomipmap LOD chain (LOD i strides
`2^i` samples) built with `MeshUtil::CalculateNormal/CalculateTangent`, plus
one-ring skirts to hide LOD cracks. This reuses octree culling, LOD screen
thresholds, shadow depth passes, and material/shader override
(`SetShaderForPass` with the terrain gbuffer shader) instead of inventing a
parallel draw path. Chunk children are flagged `editorOnly` — the existing
serialization skip — so scenes stay small and chunks rebuild on load.

*Alternatives:* (a) single 513x513 mesh — no culling, no LOD, one 500k-tri
draw; (b) CDLOD/clipmaps with GPU morphing — needs vertex-texture-fetch
plumbing and morph logic the engine lacks; (c) tessellation — not in GL 3.3.

### D9: Heightmap = `.r16` raw + JSON sidecar; splat baked at generation time

16-bit heights avoid the banding of 8-bit PNG; raw `.r16` sidesteps stb's
16-bit PNG uncertainty and is a 20-line loader. The JSON sidecar carries
resolution + world size + height scale. Splat weights are computed **by the
Python generator** from slope/height rules (with a flatten-disc mask around
the village center so the existing props still sit right) — runtime splat
recompute is out of scope; the shader just samples weights.

### D10: Terrain collision reuses BodySetup; Terrain never talks to Jolt

`BodySetup` gains `HeightField = 3`, reading heights from the **sibling**
`Terrain` component (same node), preserving the existing split where
collision authoring lives in `BodySetup`. Static-only, world transform baked
per the static-shape convention (cm units match Jolt's configured world).
CharacterController then walks terrain with zero controller changes.

### D11: Minimal `#include` support in `Shader::Compile` for atmosphere common math

Five atmosphere shaders share ~200 lines of medium/phase/LUT math. A quoted,
relative, recursive-with-depth-guard `#include "..."` preprocessor (~40
lines, resolved against the including file's directory) beats hand-synced
copies. Restricted to quoted relative includes; no system paths.

### D12: Unity API mapping (reference: Unity Terrain docs)

| Unity | Fury3D |
|---|---|
| `TerrainData` (heights, size, alphamaps) | heightmap asset (`.r16`+`.json`) + splatmap texture + serialized params on `Terrain` |
| `Terrain` component | `Terrain` component (draw/LOD/chunks) |
| `TerrainLayer` (diffuse, tileSize) | layer entries on `Terrain` (name, albedo path, tiling cm) |
| `TerrainCollider` | `BodySetup` with `HeightField` shape |
| `TerrainData.GetHeight` | `Terrain::GetHeight(worldX, worldZ)` (Lua-exposed) |
| Lighting/sun (RenderSettings.sun) | `SkyAtmosphere` sun-light binding |

## Risks / Trade-offs

- **LUT math port bugs** (HLSL->GLSL, UV conventions, y-flip) -> Validate in
  stages: transmittance LUT sanity (horizon < zenith), then sky-view gradient
  vs sun elevation screenshots, then full march; add a `FURY_SKY_DEBUG` env
  hook that dumps LUTs as the debug-view precedent (`DrawEffectDebugView`)
  does for postfx.
- **Per-frame camera-volume cost** (32 slice passes) -> tiny volume XY
  (96x54), squared slice distribution, skip re-render when camera and sun are
  static; measure with the existing profiler; worst case gate re-render to N
  frames.
- **Geomipmap cracks at chunk borders** (border vertices decimate
  differently) -> skirts on every chunk; accepted minor overdraw.
- **Terrain chunk node spam in the editor tree** (256 children) -> chunks
  live under a single internal container node marked editor-only; acceptable
  clutter, collapsed by default.
- **Props placed for the flat Grid float above/sink into terrain** ->
  generator flatten-disc around the village + setup script re-seats
  rocks/trees/fences/crates via `GetHeight`.
- **Heightfield/memory cost at 513x513** -> Jolt heightfield is ~1 byte
  sample with its own LOD; fine. Render mesh is chunked; CPU copies freed
  after upload (`DeleteRawData`) except the heights array the heightfield and
  `GetHeight` need.
- **Headless verification gap** (no GL in `fury exec`) -> logic tests
  headless (heights, raycast vs terrain, build script re-run), visual checks
  via `--screenshot` windowed automation; user does the final visual pass
  (same pattern as the particle/shadow change).

## Migration Plan

Purely additive; no migration. Old scenes load unchanged (verified by the
round-trip scenario in specs). The outdoor demo lineage gains one more
derived scene: `outdoor_water.bin` -> `outdoor_physics.bin` ->
`outdoor_terrain.bin`; earlier links untouched.

## Open Questions

- Cloud quality bar: if the 2D layer reads too flat in review, the upgrade
  path is a 32^3 3D detail noise (the D2 plumbing already supports it) —
  decide after first screenshots.
- Whether ambient material factors need a small sky-tinted boost at dusk
  (no IBL); decide visually on the demo scene.
