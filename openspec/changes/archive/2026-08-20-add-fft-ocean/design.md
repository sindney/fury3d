# Design

## Context

Fury3D is a GL 3.3-baseline engine (no tessellation; compute shaders require
GL 4.3 and are NOT in the baseline - but see D11-D13: the app bootstrap
currently pins `sf::ContextSettings` to 3.3 at `examples/main.cpp:131-136`,
which would hide compute even on capable drivers, so context creation must
negotiate the highest available core profile first). The pipeline is
JSON-described deferred light pre-pass
(`examples/Resource/Pipeline/DefferedLightingPBR.json`): `pass_gbuffer` ->
`pass_light` -> `pass_combine` (PbrCombine.glsl, aerial perspective) ->
`pass_sky` -> `pass_transparent` (forward, alpha blend, depth_write false)
-> postfx chain (SSAO/SSR/ACES/FXAA replaces `pass_final`). SSR raymarches
`gbuffer_depth` gated by roughness in `gbuffer_normal.a`, so it only sees
surfaces that wrote the gbuffer. There is no refraction/scene-color sampling
anywhere and no global time uniform (components accumulate `dt` from
`Engine::OnUpdate`, e.g. `ParticleSystem::TickOnUpdate`).

Physics is Jolt v5.6 in cm units (gravity -981), stepped at 25 Hz fixed tick
by `PhysicsWorld::TickFixed` (characters `PhysicsTick` -> `System->Update` ->
`CapturePhysicsState`). Play mode is a detached `fury Player.lua` process;
the editor never simulates. Forces go through `JPH::BodyInterface` directly;
no engine AddForce wrapper exists. Headless tests step physics via
`Physics.SetEnabled(true)` + `Physics.Step(n)` (`tests/lua/physics_smoke.lua`).

Scene authoring convention: re-runnable Lua builders run headless
(`./fury exec base.bin setup.lua`) deriving scene N from scene N-1, never
writing their source (`setup_physics_scene.lua`, `setup_terrain_sky_scene.lua`).
Asset generation convention: `tools/gen_terrain_assets.py` - stdlib-only,
deterministic (seeded), self-checking, with raw-binary + JSON sidecar
formats (`height.r16` + `height.json`). There is no water/ocean code in the
engine today; `outdoor_water.bin` is just a low-roughness plane.

Reference: Unity HDRP Water System - GPU FFT (Tessendorf/Phillips) spectrum
in bands, choppy displacement for Gerstner-style sharp crests, camera-locked
infinite geometry, and CPU-emulated wave queries for buoyancy. Our GL 3.3
port moves the FFT offline (bake) and replays it.

## Goals / Non-Goals

**Goals:**
- Tileable, seamlessly looping FFT ocean data baked offline in Python
  (Phillips spectrum, choppy displacement, Jacobian foam), 2 bands
  (swell + ripple), loadable as an engine asset.
- Opportunistic compute shader support: highest-core-profile context
  negotiation, runtime capability detection, a compute stage in the shader
  compiler, and GPU-at-load generation of the same wave arrays when
  available - with automatic baked fallback and a user on/off switch.
- `OceanComponent` with two geometry modes: finite grid (inland water) and
  infinite camera-centered concentric rings (static ring LOD instead of
  tessellation), integrated with sky TOD lighting, SSR, foam, postfx.
- CPU `WaveSampler` that replays the same wave arrays (baked or generated)
  for gameplay/physics; `BuoyancyComponent` that floats Jolt bodies in play
  mode.
- `examples/Projects/ocean` with three demo scenes (infinite ocean +
  floatable debug props, terrain lake, island) built by re-runnable Lua
  scripts, plus headless Lua tests.

**Non-Goals:**
- Per-frame GPU wave simulation (compute is used for one-time generation at
  load/param-edit only - see D12), GPU readback buoyancy (HDRP option A).
- Compute as a hard requirement: the engine MUST remain fully functional on
  a pure GL 3.3 context (macOS GL 4.1 included - compute never exists
  there).
- Refraction (distorted scene-color sampling), underwater
  fog/godray/postfx when the camera goes below the surface, foam trails
  behind moving objects, ocean-cast shadows.
- Third spectrum band (cascades) beyond swell+ripple, wave/terrain erosion
  coupling, networking/determinism guarantees beyond same-asset replay.
- Modifying `BodySetup` (buoyancy implements its own drag forces instead of
  touching Jolt damping).

## Decisions

### D1: Bake the FFT offline in Python; replay baked sequences at runtime

`tools/gen_ocean_assets.py` (sibling of `gen_terrain_assets.py`, same
conventions: stdlib-only, seeded/deterministic, self-checking) evaluates a
Phillips directional spectrum, quantizes each `w(k)` to an integer multiple
of `2*pi/T` (T = loop duration) so the sequence loops perfectly, and runs
inverse 2D FFTs per frame. Per frame it emits: displacement (choppy dx, dz
+ vertical dy), normals (finite differences of dy), and foam from the
Jacobian of the displacement field (finite differences of the baked
displacement - avoids two extra spectral derivative IFFTs). Two bands:
swell (default 128^2, 32 frames, 100 m tile) and ripple (64^2, 32 frames,
8 m tile). Outputs are committed; re-baking is rare. This Python bake is
the universal baseline; when compute shaders are effective the SAME arrays
are generated on the GPU at load instead (D12) and the bake tool becomes
the fallback plus the source of test fixtures.

*Alternatives:* per-frame runtime GPU simulation (possible only with GL 4.3
compute; rejected even when available - see D12); runtime CPU FFT
(wasteful when sea states are authored, not dynamic). Sum-of-Gerstner-waves
(rejected by the brief; also does not give a Jacobian foam signal for free).

*Trade-off:* pure-Python radix-2 FFT at default sizes is minutes-scale
(~2 complex IFFTs per frame per band after dx/dy packing). The tool tries
`import numpy` for a ~50x speedup and warns if absent; tests use a 32^2 /
8-frame mini fixture that bakes in seconds.

### D2: Asset format follows the height.r16 precedent: raw binary + JSON sidecar

Per band: `<name>_disp.f16` (frames x N x N x RGBA16 half floats: dx, dy,
dz, foam) and `<name>_nrm.u8` (frames x N x N x RGBA8: normal xyz *0.5+0.5,
alpha = foam copy for cheap preview), plus one `ocean.json` sidecar
(spectrum params, per-band tile size in cm, resolution, frame count, loop
seconds, amplitude range, content hash). Frames become layers of a
`TEXTURE_2D_ARRAY` at load (CSM precedent; per-layer mipmaps), so the vertex
shader samples `layer = frame` twice and lerps. Default GPU cost ~15 MB.
A runtime `OceanWaves` Entity (mirroring `Heightmap`: registered in the
scene `EntityManager`, resolved by name == path via `Scene::ResolveAsset`)
holds the GPU arrays plus a CPU float32 copy of dx/dy/dz/foam for the
sampler. Two components referencing the same path share one instance, as
`Terrain::ResolveHeightmap` does.

*Alternatives:* 64 separate PNGs (lossy for displacement, ugly); KTX/DDS
(new dependency); keeping spectra and summing in the vertex shader (O(N^2)
per vertex - dead on arrival).

### D3: Stateless replay; one wave time per ocean, driven by OnUpdate

`OceanComponent` accumulates `m_Time += dt` via `Engine::OnUpdate->Connect`
(ParticleSystem pattern) and binds it as `u_time`; displacement is a pure
function of (world xz, time) from the looped arrays, so pause/seek/re-play
reproduce exactly, and CPU queries use the same `m_Time`. Physics fixed
ticks run before `OnUpdate` in the frame loop, so buoyancy samples the
previous frame's time - at 25/60 Hz this is sub-frame skew on slow swell,
well inside sampler tolerance.

*Alternative:* a global engine `u_Time` uniform - touches every shader for
no benefit beyond this feature.

### D4: Infinite mode = prebuilt concentric ring LOD, camera-locked with grid snapping

GL 3.3 has no tessellation, so LOD is static geometry: R rings (default 3)
of square grid frames around a dense center grid, each ring doubling cell
size (default center 64x64 cells of 100 cm; ring radii cover roughly 64 m /
256 m / 1 km) plus a flat far skirt disc to the horizon. All rings are built
once at attach from the component's ring config (MeshBuilder-style code into
`Mesh::Positions/UVs/Indices`). Per frame the component reads
`Pipeline::Active->GetCurrentCamera()` (particle/SkyAtmosphere precedent),
snaps each ring's origin to a whole multiple of its cell size (no vertex
swimming), and binds per-ring world matrices. Higher bands fade out by ring
index so distant rings only carry swell (kills aliasing and disguises ring
seams). Ring boundaries use the classic cheap fix: each outer ring overlaps
the inner one by one cell and sits a few cm lower; identical world-space
displacement functions keep cracks sub-pixel. The owner node's AABB is
expanded to "infinite" (ParticleRenderer::OnAttaching precedent) so frustum
culling never kills it. Finite mode builds a single static grid on the node
transform - for the terrain lake.

*Alternatives:* projected screen-space grid (better screen uniformity,
degenerate when the camera looks down or flies high; rings are the user's
requested model); CDLOD-style morphing (needs per-vertex morph weights and
doubles build complexity; overlap trick is sufficient at water's low
contrast).

### D5: Dedicated `pass_ocean` between sky and transparent; write depth + gbuffer normal

New `DrawMode::OCEAN` (SKY precedent: `EnumUtil.h`, JSON `drawMode`,
dispatch in `PrelightPipeline::Execute` to `PrelightPipeline::DrawOcean`,
nodes collected via new `RenderQuery::oceanNodes` + `OcTree::GetRenderQuery`
mirroring the particle integration list). The pass: depth attachment
`gbuffer_depth` with depth_write TRUE, color0 `hdr_composite` with
`(SRC_ALPHA, 1-SRC_ALPHA)` blend, color1 `gbuffer_normal` (view normal
*0.5+0.5, low roughness in alpha). This buys three integrations at once:
(1) SSR in the postfx chain sees water as a smooth reflector and marches
against displaced water depth; (2) later transparents depth-test against
water; (3) water alpha-blends over the already-lit terrain/sky in
`hdr_composite`, so shallow bottoms show through tinted by absorption - no
refraction pass needed. Shore foam needs scene depth BEHIND the water:
sampling the bound depth attachment is a feedback loop, so `DrawOcean`
first blits `gbuffer_depth` into `Texture::GetTemporary` (once per frame,
skipped when no ocean is visible) and the shader reads the copy with the
same linearization math SSR uses.

*Alternatives:* Route A, stock `MeshRender` + BLEND material in
`pass_transparent` (zero pipeline changes, but no depth write, no gbuffer
normal -> SSR blind to water, and the shared transparent shader can't do
band sampling without material-shader overrides anyway); adding
`gbuffer_normal` as an MRT output of `pass_transparent` (every other
transparent shader would leave that attachment undefined - core GL writes
garbage for unwritten outputs).

*Limitation (accepted):* transparent objects behind water are occluded by
water depth instead of blending; none of the demo scenes submerges
transparents. SSAO runs later in the chain and will see water depth; on a
smooth water surface there are no nearby occluders, so the only effect is
mild waterline darkening - visually plausible.

### D6: Ocean shader does its own lighting + shadow-receive + aerial perspective

`examples/Resource/Shader/Ocean/OceanSurface.glsl` (single-file
`#ifdef VERTEX_SHADER/FRAGMENT_SHADER` convention, `#version 330`,
`#include "AtmosphereCommon.glsl"`-style reuse where useful). Vertex stage:
sum per-band displacement arrays (two array samples + frame lerp per band),
world-space function of the snapped ring origin. Fragment stage: band
normals + detail ripple from the ripple band, GGX sun specular using the
pipeline's last shadow textures for sun shadow-receive (transparent
shadow-variant precedent: `m_LastShadowTextures`/`m_LastShadowMatrices`),
sky reflection approximation from the sky's ambient (plus SSR at the postfx
stage), Beer-Lambert absorption + scatter color by water depth (from the
depth copy), crest foam from baked foam channel + shore foam from linearized
depth difference, alpha from submersion depth. Aerial perspective: sample
the same camera-volume LUT the sky binds (`SkyAtmosphere::GetActive()`), so
the horizon matches terrain/sky fog. Debug defines: `OCEAN_DEBUG_FOAM`,
`OCEAN_DEBUG_DISP`, `OCEAN_DEBUG_RINGS`.

*Alternative:* folding water into `PbrCombine` (opaque-style) - loses alpha
blending over the lit scene and couples water to the gbuffer model.

### D7: CPU WaveSampler mirrors the GPU path from the same decoded data

`OceanWaves` keeps float32 CPU copies; `WaveSampler` (free functions or a
small struct on `OceanWaves`) evaluates height/normal/displacement at
(x, z, t) with the same bilinear + frame-lerp math as the shader, plus a
choppy-corrected height query (`p -= D(p)`, fixed 2 iterations) for steep
crests. This is HDRP's option B: zero readback latency, bit-consistent with
rendering within sampler tolerance. Missing/corrupt asset -> sampler
returns the flat water level and the ocean renders flat (one logged
warning), per the wave-data spec.

### D8: Buoyancy runs pre-step via a PhysicsWorld registration list

`PhysicsWorld` gains `RegisterBuoyancy/UnregisterBuoyancy` +
`TickBuoyancy(fixedDt)` invoked in `TickFixed` right after characters'
`PhysicsTick` and before `System->Update` - same-tick force application,
avoiding the 40 ms latency a plain `OnFixedUpdate` connection would add.
`BuoyancyComponent` (ComponentRegistry name `BuoyancyComponent`, sibling of
`BodySetup`) holds float points (local offset + radius), water density,
linear/angular drag, righting strength, ocean node name. Per float point:
submersion `s = clamp((waterY - y + r) / 2r, 0, 1)` from the choppy-corrected
sampler, force `F = s * (mass/n) * g * densityRatio` applied at the point
via `BodyInterface::AddForce(id, F, worldPos)` (differential submersion
yields natural righting torque), plus point-velocity drag when `s > 0`.
All math in cm (gravity -981). Drag is implemented as our own force, so
`BodySetup` needs no new damping fields and the physics-world spec is
untouched. Debug draw via `RenderUtil::BeginDrawLines` behind a component
flag (editor viewport and, in play sessions, colored by submersion).

*Alternatives:* OnFixedUpdate connection (one-tick latency, wobblier);
Jolt damping fields on BodySetup (spec churn for a worse model - uniform
damping is not water drag); GPU readback (D1 rules it out).

### D9: Scene authoring via re-runnable Lua builders; verification headless-first

New Lua usertypes (`OceanComponent`, `BuoyancyComponent`) in
`LuaBindings.cpp` following the Terrain/BodySetup pattern. Project
`examples/Projects/ocean/` lineage: bake (`tools/gen_ocean_assets.py --out
examples/Projects/ocean/Ocean`) -> `setup_ocean_base.lua` (sky + TOD bound
to a sun light, camera, render settings, ocean node) -> three derived
scenes via their own scripts:
1. `ocean_infinite.bin` - infinite ocean, pure-color plastic spheres/cubes
   (MeshUtil primitives + PBR materials) with BodySetup + BuoyancyComponent
   placed at the waterline, a few dropped a meter up for splash tests.
2. `ocean_lake.bin` - heightmap terrain with a basin, finite-grid ocean at
   the basin's water level.
3. `ocean_island.bin` - island-heightmap terrain + infinite ocean; shore
   foam demos the waterline.
Tests: `tests/lua/ocean_waves_spec.lua` (loop wrap, determinism, CPU vs
baked values), `tests/lua/buoyancy_smoke.lua` (Physics.Step - ball settles
at waterline, cube levels), `tests/lua/ocean_scene_roundtrip.lua`; fixture
bake under `tests/lua/fixtures/ocean_mini/`. Visual verification via
windowed `--screenshot` runs.

### D10: Editor exposure is inspector-first, no new window

`RenderOceanBody` / `RenderBuoyancyBody` + `ComponentRenderTable` rows in
`EditorNodeProperties.cpp` (SkyAtmosphere/Terrain precedent), wave asset
chosen via `EditorAssetPicker`, debug toggles (foam mask, displacement
heatmap, ring wireframe) as component booleans feeding shader defines and
`Pipeline::AddDebugCollidable`/RenderUtil lines.

*Alternative:* a dedicated `EditorOceanWindow` (like EditorSkyWindow) -
deferred; the ocean has ~15 fields, not 60.

### D11: Context negotiation + capability probe gate all compute use

Today `examples/main.cpp` pins `sf::ContextSettings` to 3.3, which hides
compute even on capable drivers. The bootstrap SHALL attempt descending
core-profile versions (4.6, 4.5, 4.3, 4.1, 3.3), record and log the
negotiated version, and keep 3.3 as the guaranteed floor (GL 4.x core is a
superset of 3.3 core, so existing rendering is unaffected). Detection:
`GLLoader` already walks the extension list (`glGetStringi`,
`GL_NUM_EXTENSIONS`) and has `IsVersionGEQ`; add the three entry points
(`glDispatchCompute`, `glMemoryBarrier`, `glBindImageTexture`) following its
codegen pattern, and expose `HasComputeShaders()` = (version >= 4.3 OR
`ARB_compute_shader` + `ARB_shader_image_load_store`) AND pointers
resolved. Headless exec has no GL context at all, so the query returns
false there and every headless test exercises the fallback. macOS is capped
at GL 4.1 core, so compute NEVER exists on the dev machine - the baked
path remains the default-tested path, and compute verification needs one
run on a capable (Windows/Linux) machine.

*Alternative:* requiring GL 4.3 outright (breaks the engine's macOS support
and its 3.3 baseline - rejected).

### D12: Compute path = GPU-accelerated generation at load, not per-frame simulation

When `HasEffectiveCompute()` and the component's wave source is `Auto`,
`OceanComponent` generates the exact same looped per-band arrays on the GPU
at attach (and on each spectrum-param edit) instead of loading `.f16`/`.u8`
payloads: spectrum h0 pass, time evolution with hermitian packing (the 7
real fields - dy, dx, dz, and four Jacobian derivatives - fit in 3 complex
IFFTs per band per frame), one butterfly compute shader
(axis/stage/direction uniforms, ping-pong RG32F `Texture::GetTemporary`
pairs), then normal (finite-diff of dy) and Jacobian-foam passes, writing
frames as layers of the same `TEXTURE_2D_ARRAY`s. A one-time
`glGetTexImage` readback fills the CPU float copy. After generation the two
paths are indistinguishable: same arrays, same surface shader, same CPU
sampler, same buoyancy sync, same determinism. One-time cost is
~3k tiny dispatches (sub-second), never per-frame.

*Alternatives:* per-frame GPU FFT (HDRP-style live sim) - rejected for v1:
it adds a standing per-frame cost, forces async-readback lag into buoyancy
(HDRP option A, 1-2 frames), and breaks the deterministic
CPU-emulation contract; the quantized loop is imperceptible for authored
sea states. Fragment-shader IFFT per frame - the sky LUT passes show small
fragment loops are fine for 32x32 LUTs, not for 128^2 x 32 frames.

*Trade-off:* two spectrum implementations must agree statistically
(Python/NumPy vs GLSL hash-based phases); the contract is per-path
determinism and cross-path statistical equivalence, verified by the
continuity/tolerance tests, not bit-equality of phases.

### D13: One global user switch, one component policy

Compute use is gated by `HasEffectiveCompute()` = `HasComputeShaders()` AND
a global enable. The enable defaults on, persists via the editor settings
registry (`Settings.*` in imgui.ini, QoL-change convention), and is
overridden by `FURY_COMPUTE_SHADER=0/1` for runtime/test determinism.
`OceanComponent.waveSource` is `Auto` (compute when effective, else baked
asset) or `Baked` (force asset even when compute works); the inspector
shows the resolved source and the reason (`compute unavailable: GL 4.1`,
`disabled by user`, ...), so misconfiguration is visible instead of silent.

*Alternative:* a per-scene RenderSettings override - rejected; one global
switch plus per-component policy covers the stated needs without a third
knob.

## Risks / Trade-offs

- **Compute path is untestable on the dev machine** (macOS caps GL at 4.1)
  -> the baked path is the default-tested path everywhere here; compute
  verification is a gated test that skips cleanly plus one manual run on a
  capable Windows/Linux machine; `FURY_COMPUTE_SHADER` pins behavior per run.
- **Context negotiation regression** (window fails on an old driver) ->
  descending attempts end at the previous pinned 3.3; negotiated version is
  logged; worst case is identical to today.
- **Image-format driver quirks in compute writes** -> keep all compute
  intermediates RG32F/RGBA32F (core image formats), convert to RGBA16F only
  in the final array write; barriers after writes before any sampling
  (`GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` / `GL_TEXTURE_FETCH_BARRIER_BIT`).
- **Two spectrum implementations can drift** (Python vs GLSL) -> contract is
  per-path determinism + cross-path statistical equivalence (continuity,
  amplitude range, tolerance vs CPU sampler), not bit-equal phases; the
  same test file asserts both.
- **One-time readback stall** (~15 MB `glGetTexImage` at load/param edit)
  -> documented hitch, never per frame; param edits are user-paced.
- **Pure-Python bake is slow at high resolutions** -> numpy auto-detect
  (~50x), small defaults (128^2/64^2, 32 frames), committed outputs, mini
  fixture for tests; `--resolution 256` warns about runtime without numpy.
- **Ring boundary cracks (T-junctions)** -> one-cell overlap + slight
  vertical tuck + per-ring band fade; acceptance is "not visible at shading
  resolution" in the island scene; a true stitch strip is the documented
  follow-up if it shows.
- **Horizon fog mismatch** (PbrCombine AP ran before water exists) -> ocean
  samples the same sky camera-volume LUT; if that proves fragile, fallback
  is analytic distance fog matched at the horizon (Open Question).
- **Feedback-loop traps**: sampling the depth attachment being written ->
  pre-pass depth blit to a temporary; sampling `hdr_composite` while
  writing it -> never do it (absorption uses the depth copy instead).
- **Half-float precision** for large swell amplitudes -> store raw half
  (plenty at cm scale); amplitude range recorded in `ocean.json` and
  checked at load.
- **CPU/GPU sampler divergence** -> one shared decode path, shared frame
  math constants in the sidecar, and a spec scenario with a tolerance test
  (`ocean_waves_spec.lua`).
- **SSR only reflects what the gbuffer knows** -> ocean writes
  `gbuffer_normal`/depth in `pass_ocean` (D5); reflections *of* transparent
  objects on water remain impossible (accepted).
- **AO darkening at the waterline** -> judged visually acceptable; if not,
  gate SSAO by a roughness threshold (one-line shader change).
- **Vertex swimming when rings follow the camera** -> snap origins to cell
  multiples; tested by moving the camera 1 km in the infinite scene.
- **ASCII/LF hygiene** -> `tests/check_engine_ascii.py` runs over engine
  sources; new shaders/Lua stay ASCII-only; `.gitattributes` keeps LF.

## Migration Plan

Purely additive; no existing API or spec changes. Order:
1. Bake tool + default bake + mini fixture (no engine changes; repo grows
   `tools/gen_ocean_assets.py` and `examples/Projects/ocean/Ocean/`).
2. Compute infrastructure: context negotiation, GLLoader entry points,
   capability probe, Shader compute stage, dispatch/BindImage, settings +
   env switch. No consumer yet; fallback everywhere == current behavior.
3. `OceanWaves` asset + `WaveSampler` + wave spec tests (still nothing
   rendered).
4. `OceanComponent` + meshes + GPU generation path + `pass_ocean` + shader
   (renders; existing scenes untouched because no ocean nodes exist).
5. `BuoyancyComponent` + `PhysicsWorld` registration + physics tests.
6. Editor inspector + Lua bindings + demo scenes + screenshots.
7. Docs (`docs/OCEAN.md`, LUA_API additions).
Rollback: each step is independent; removing the change is deleting new
files plus the `pass_ocean` JSON entry, the `DrawMode::OCEAN` enum row, and
the context-negotiation loop (re-pin 3.3).

## Open Questions

- Exact aerial-perspective reuse in the ocean shader (bind the sky camera
  volume directly vs. a cheaper analytic fallback) - decide during the
  rendering task with side-by-side screenshots at the island horizon.
- Whether shore foam needs the depth blit every frame or only when a
  terrain/static actually intersects the water (a visibility-flag
  optimization) - profile during the rendering task.
- Ring default counts/radii after measuring real frame times on the target
  GPU; the budget guardrail is ~250k ocean vertices.
- Whether SFML context re-creation on version-failure needs a window
  recreate or can retry on the same window (SFML creates the context with
  the window) - resolved in the compute-infra task; worst case is creating
  the window once per attempted version, which already happens at startup.
