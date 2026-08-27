# Fury3D — FFT ocean (waves, rendering, buoyancy)

> Status (2026-08-19): shipped with change `add-fft-ocean`. GL 3.3 baseline:
> wave data is baked offline and replayed; on GL 4.3+ machines the same data
> can be generated on the GPU at load instead (never per frame).

## Overview

- **Wave data**: 3 looping FFT bands per ocean (swell 100 m tile + ripple
  8 m + chop 3 m) — displacement (choppy dx/dz + vertical dy) and a
  Jacobian foam channel, baked offline by `tools/gen_ocean_assets.py`
  (Phillips directional spectrum, loop-quantized angular frequencies so
  frame N wraps to frame 0). Swell + ripple drive the vertex displacement
  and buoyancy; the chop band is **normals + foam only** (its job is the
  fine glint texture - it never moves vertices). The committed baseline
  bake is an ENGINE asset (`examples/Resource/Ocean/`, referenced
  `Engine/Ocean/ocean.json`); `examples/Projects/ocean/OceanStorm/` holds a
  storm sample (22 m/s wind, 2.5 m swell) used by `ocean_storm.bin`.
- **Rendering**: `pass_ocean` in `DefferedLightingPBR.json` between the sky
  and transparent passes; the vertex shader displaces the grid from the baked
  bands, the fragment shader shades (wrapped diffuse body + sun GGX with a
  distance-widened lobe + sky-view-LUT reflection + crest/trough height
  tint + foam) and writes view normal + roughness into `gbuffer_normal` so
  SSR treats water as a smooth reflector. Band detail fades camera-radially
  (see below), normals flatten with distance, and an analytic fog lerps far
  water onto the sky's horizon color (sampled from the sky-view LUT), so the
  6 km skirt edge never reads. Crest foam and swell normals get a second
  rotated/rescaled fetch each (37 deg, incommensurate tile scales) so the
  100 m band tiling never reads from elevated views.
- **Buoyancy**: `BuoyancyComponent` floats Jolt bodies in play mode by
  querying the CPU `WaveSampler` at each authored float point.

## Bake tool

```
python3 tools/gen_ocean_assets.py --out examples/Projects/ocean/Ocean
```

Stdlib-only, seeded/deterministic, self-checking (edge-wrap, loop-wrap,
determinism, sidecar match). Key params: `--seed`, `--swell-resolution`
(128) / `--ripple-resolution` (64), `--frames` (32), `--loop-seconds` (12),
`--swell-tile-cm` (10000) / `--ripple-tile-cm` (800), `--wind-speed`,
`--wind-direction`, `--fetch-cm`, `--choppiness`. Auto-detects numpy for a
~50x speedup; the pure-Python radix-2 IFFT path is minutes-scale at defaults.
The mini fixture (`tests/lua/fixtures/ocean_mini/`, 32^2 x 8 frames) bakes in
seconds for tests.

## Asset layout (per bake directory)

- `ocean.json` — sidecar: all params, per-band tile/resolution/frames/loop,
  amplitude range, per-band `maxFoam` (peak of the raw Jacobian channel; the
  shader normalizes crest foam by it so thresholds work at any sea state),
  per-band preview PNG filenames, sha256 of payloads.
- `<band>_disp.f16` — frames x N x N x RGBA16 half: dx, dy, dz, foam.
- `<band>_nrm.u8` — frames x N x N x RGBA8: normal xyz *0.5+0.5, alpha = foam
  copy (cheap preview; the FS reads crest foam from it directly).
- `<band>_height_preview.png` / `<band>_foam_preview.png` — human-viewable
  previews (frame-0 wave height, foam max-projection; both normalized
  grayscale via a stdlib PNG writer). Not engine inputs — the .f16/.u8 stay
  authoritative (16-bit displacement range and negative values don't survive
  8-bit image channels). The Content Browser's OW tile shows the height
  preview as its thumbnail.

At load, frames become layers of `TEXTURE_2D_ARRAY`s (REPEAT wrap, per-layer
mips); a float32 CPU copy feeds `WaveSampler`. Two components referencing the
same `ocean.json` share one `OceanWaves` (EntityManager `Resolve` cache).

## Compute shaders (GPU generation removed)

The GPU FFT generation path was removed (change: `remove-gpu-ocean-generation`)
because (a) every shipped water system we benchmarked - Unreal's built-in
Water System and Unity HDRP's Crest - bake offline and replay at runtime
(no runtime FFT) and (b) the GPU path was untestable on macOS (GL 4.1
ceiling) so bugs would have shipped silently. The engine now resolves the
baked `.f16`/`.u8` asset exclusively; `OceanComponent.waveSource` retains
`Auto`/`Baked` for back-compat serialization but they are functionally
identical (both use the baked asset). The compute-infrastructure surface
(context negotiation, `HasComputeShaders()`, `HasEffectiveCompute()`,
`Shader::CompileCompute`, the `FURY_COMPUTE_SHADER` toggle) is kept in
`engine/Fury` for any future compute writer that wants to plug in - none
ships today.

## OceanComponent

Geometry modes:
- **Finite**: one static grid on the node transform (size x resolution) —
  inland water (the terrain lake).
- **Infinite**: dense center grid + 3 ring frames doubling cell size + a far
  skirt disc (defaults: 64x64 cells of 100 cm, skirt radius 6 km, ~15.3k
  verts total, well under the 250k budget). ALL pieces snap to whole cells
  of the center (finest) cell grid — per-piece snapping left the shared
  edges up to half a coarse cell apart in world space, which read as
  background slits at grazing angles. Each LOD pair is joined by a
  **stitch ring** (3 triangles per coarse segment + 2 per corner fan):
  without it the pieces merely abut, and the fine edge's midpoint vertices
  displace past the coarse edge's chord — grazing slivers. The skirt's
  inner circle overlaps the last ring's square footprint by 5% with a 2 cm
  tuck so the circle/square join never opens. Owner AABB is infinite so
  frustum culling never kills it.

Band fades are **camera-radial smoothsteps in the vertex shader**
(`u_fade_ranges`, derived from the ring radii in `BuildMeshes`): ripple
displacement fades across [R0/2, R0] (fully out AT the ring-1 boundary so
the 8 m band never straddles a T-junction), swell fades to 0 across the
last ring (the skirt's ~250 m fan sampling aliases the 100 m band into
glint dashes; a residual swell floor was subpixel at 6 km and only fed
that aliasing). Ripple **normals** outlive ripple displacement 4x
(`v_band_fade.z`) — 8 cm chop is subpixel long before 8 m wavelet normals
stop being useful mid-field detail. Fades feed the VS displacement and the
FS band normals together via the `v_band_fade` varying.

Shading fields: absorb/scatter colors (defaults: scatter 0.025/0.11/0.15,
absorb 0.006/0.04/0.08 — the deep dark blue-green plate), roughness
(default 0.12), normal strength, foam amount (default 0.6), shore-foam
depth range. The fragment shader flattens normals with view distance
(~80% by 500 m, kills far tiling/shimmer), widens the sun lobe with
distance (the glitter path), darkens troughs and glows crests from the
displaced height (`v_height` varying), fades foam out beyond ~200-600 m,
and converges to the sky horizon color from ~1 km. The sky reflection
samples the per-frame **sky-view LUT** (true sky color at any
time-of-day, exact horizon convergence); a 2-color analytic gradient is
the no-atmosphere fallback. Debug views: 1 foam mask, 2 displacement
heatmap, 3 ring-LOD wireframe (glPolygonMode + per-piece color) — settable
in the inspector AND in the viewport's Debug droplist (Ocean submenu; the
SSAO/SSR buffer views live in its PostProcess submenu; the droplist fits
its rows without a scrollbar and stays open across item clicks until you
click outside). The spectrum section renders disabled with the reason
where `HasEffectiveCompute()` is false. The wave asset is set through the
standard Change/->/x asset chip (Terrain heightmap precedent) with a stats
line; `OceanWaves` appears in the Content Browser (filter + "OW" badge +
the bake's preview PNG as thumbnail).

Foam comes from the Jacobian copies in the normal arrays' alpha (the FS
reads them from the same fetches as the normals — no extra displacement
read): crest foam thresholds the ABSOLUTE summed foam
(smoothstep(0.40, 0.60) after a 0.45 ripple / 0.3 chop weighting), so the
calm baseline (p99 ~0.30) stays foam-free and only storm-strength bakes
whitecap. Crest foam fades 150-350 m (subpixel whitecaps only shimmer at
range). One mild rotated gate (1.37x ripple) breaks the 8 m crest-to-crest
regularity; a swell-scale gate was dropped (it painted 373 m blobs).
Shore foam is depth-based and unaffected. Foam is lit: white * (ambient +
sun wrap*0.9 + moon wrap*0.5) — foam shades with the sky and never glows
at night.

Night: the ocean is lit by a cool moon (color ~0.85,0.92,1.05 * 0.12
intensity, sky-driven) — wrapped diffuse + a capped glint path keep the
surface from going pitch black. Verified at FURY_TOD=0.5.

SSAO/SSR gating: water writes its true roughness to `gbuffer_normal.a`
unconditionally; SSAO exempts low-roughness pixels
(`ao = mix(1.0, ao, smoothstep(0.05, 0.3, nrm.a))`, mirroring SSR's
roughFade) so wave detail normals don't self-occlude into dirt patches. The
serialized `ssr` component flag no longer gates anything (kept for scene
compat; SSR's own `roughness < 0.95` gate decides reflection).

## BuoyancyComponent

Sibling of a dynamic `BodySetup`; ticks pre-step via the PhysicsWorld
registration list (same-tick force application). Fields: float points
(node-local offset + radius), water density, linear/angular drag, righting
strength, ocean node name, debugDraw flag.

Per point per fixed tick: choppy-corrected `WaveSampler` height -> submersion
s = clamp((waterY - y + r) / 2r) -> `BodyInterface::AddForce(bodyID, F, pos)`
with F = s * (mass/n) * |g| * density, plus point-velocity drag when s > 0.
Density 1.0 is neutral at full submersion; ~2 floats half-submerged.
Rotational terms (angular drag, righting) gate on *proximity to the surface*,
not on wet points — an inverted floater hangs with its bottom-face points
dry, and gating on them would trap it upside-down forever.

Tuning notes (from `tests/lua/buoyancy_smoke.lua`):
- Splash settling needs strong drag: `linearDrag` ~2.5 settles a 3 m drop in
  ~10 s; 0.8 bounces for 16+ s.
- A point-model floater can stably **capsize** under impact: differential
  submersion of the dry-side points forms a real inverted equilibrium. The
  righting term is the escape and must dominate that buoyancy pendulum —
  righting 20 levels the 3 m test drop; 8 still trapped. Righting torque is
  ~sin(theta/2) with an exact-inverted escape clause.
- Component reads the **live Jolt body state** (GetPosition/GetRotation),
  never node transforms: SyncNodes runs after the step batch, so a node read
  is the stale pre-simulation pose (fatal in headless `Physics.Step` batches).
- **Angular drag must clamp per-axis against the REAL inverse inertia**
  (read via `BodyLockRead` + `GetLocalSpaceInverseInertia`): the crude
  `mass*size^2` estimate overshoots a thin long shape's short axes by ~44x
  (plank roll: Iest 128300 vs Ireal 2900), and at the 25 Hz tick with Jolt's
  2 substeps the unclamped factor flips omega's sign per substep - on live
  (animated) water that pumps a rolling limit cycle (the "plank spins" bug).
  The clamp caps the per-tick kill factor at 0.9; shapes that were already
  stable keep ~their old damping. Note the righting spring keeps the
  estimate-based scaling: stay under omega_n * dt < ~0.8 (spring stability
  at 25 Hz) when authoring new shapes.

## Traps (read before touching this code)

- **Linear gbuffer depth**: the pipeline's depth convention is
  `gl_FragDepth = -viewZ / cameraFar` (GBuffer/DrawTerrain/Forward/Particle).
  Any new pass sharing `gbuffer_depth` MUST write the same convention — the
  first ocean version wrote default NDC depth and every water-over-terrain
  pixel was rejected (water only rendered over the void). The shore-foam
  depth copy is also already-linear: `sceneLinear = d * cameraFar`.
- **Feedback loop**: shore foam needs scene depth *behind* the water, which
  is also the pass's depth attachment. `DrawOcean` blits `gbuffer_depth` into
  a `Texture::GetTemporary` once per frame (glBlitFramebuffer; RenderUtil::Blit
  is color-only) and the shader reads the copy.
- **Ring snapping**: ALL ring piece origins snap to whole cells of the
  CENTER (finest) cell grid; displacement is a pure function of world xz +
  time, so vertices never swim and shared edges coincide exactly. Snapping
  each piece to its own (coarser) cell left shared edges up to half a coarse
  cell apart — grazing views showed the background through the slits.
- **Abutting LOD edges always sliver at grazing**: the shared edges must
  be STITCHED, not just positionally aligned - the fine edge's midpoint
  vertices displace with the wave while the coarse edge chords straight
  past them. The -2 cm/ring tuck masked it for smooth spectra but the
  full-spectrum swell's short modes outgrew it. The stitch rings (3
  triangles per coarse segment + corner fans) are the watertight fix;
  only the skirt keeps a tuck (its fan overlaps the last ring).
- **Player free-fly camera far**: Player.lua's fallback default is
  500000 cm (5 km) like the editor's — the 5000 cm default clipped every
  horizon. `FURY_CAM` farCm overrides per run.
- **The skirt must wind UP**: `BuildSkirt`'s radial fan wound downward and
  `pass_ocean` backface-culls, so the skirt never rendered at all — the far
  "water" was the sky's below-horizon row showing through (read as an
  unfogged band at the horizon). If the far water ever looks wrong, check
  the skirt with the wireframe debug view first.
- **The bake's high-frequency cutoff must be tile-relative**: a
  fetch-relative `damp_len` (16 m at the default fetch) sat inside the
  small tiles' passband and zeroed every mode — the 8 m "ripple" band was
  a single-mode plane wave (visible dash repetition), and a 3 m chop tile
  degenerated to zero amplitude. Now `damp_len = tile * 2 / resolution`.
- **The noon specular white-out needs an elevation-scaled SOFT cap**: with a
  near-zenith sun the view/sun half-vector is near-vertical over the whole
  down-sun region, so the GGX lobe saturates it at any fixed cap; and a
  hard `min()` cap pops glints on/off as the waves animate (the storm
  flicker). The cap is a rational soft-knee `x/(1+x/cap)` with the cap
  scaled by sun elevation (mix(1.2, 0.35, smoothstep(0.3, 0.9, sunDir.y)));
  the chop band's normals break the field into glints.
- **Octree root prune**: `OcTree::WalkScene` prunes whole subtrees by cell
  AABB; root-resident infinite-bounds nodes (directional lights, oceans,
  particle systems) are now always per-node tested — before the fix, roaming
  ~10 m+ off the tree extent lost the ocean AND the sun.
- **Jolt velocity clamp**: Jolt's `cMaxPhysicsVelocity` (500) is meter-scale;
  `BodySetup::CreateBody` sets `mMaxLinearVelocity = 50000` (500 m/s in cm).
  Without it, falls cap at 5 m/s and buoyant bodies freeze mid-air on sleep.
  Discrete motion quality has no CCD — thin floors (< impact speed * tick)
  tunnel.
- **MeshUtil primitives carry normals + UVs** (`EnsureNormalsUVs`): a shader
  declaring `vertex_normal`/`vertex_uv` against an empty buffer is an
  out-of-range attribute error in core GL (GL_INVALID_OPERATION, zero
  fragments, nothing rendered). `TransformMesh` recomputes the AABB.
- **Fresh Materials are COLOR_ONLY** (texture_flags): a zero default matched
  the pass's first *textured* shader and sampled unit-0's leftover texture.
- **ASCII + LF**: `tests/check_engine_ascii.py` runs over engine sources;
  new shaders/Lua stay ASCII-only, LF endings.
- **Determinism**: the wave replay is a pure function of the component's wave
  clock (accumulated from `Engine::OnUpdate`). Headless `Physics.Step` never
  advances it — tests drive `SetWaveTime` by hand. Do NOT reload-and-resim a
  physics scene in one Lua script: Lua-held references keep the old scene's
  Jolt bodies alive and the new drop lands on the old body.
- **Do not alpha-fade the far water into the sky**: the sky pass renders
  BELOW-horizon sky (dark void/ground bounce) behind every unoccluded water
  pixel, so a semi-transparent skirt shows a dark band at the horizon, not
  the bright haze. Converge by COLOR instead: fog target = the sky-view
  LUT's horizon texel toward the fragment (`skyview_uv(horizontalDir)`),
  which matches the sky pass exactly at any TOD.
- **gbuffer alpha is the water gate**: both SSAO and SSR read roughness from
  `gbuffer_normal.a`. Water must write its true roughness even when some
  effect is off — writing a sentinel (e.g. 1.0 for "SSR off") silently
  re-enables SSAO dirt on water.
- **Importer.MergeInto must transfer every asset type**: it enumerates
  Texture/Material/Mesh/AnimationClip/ParticleSystem/Heightmap/OceanWaves
  explicitly. A missing type is stranded in the discarded source scene -
  rendering keeps working (components hold shared_ptrs) but the Content
  Browser and asset pickers come up empty. `tests/lua/ocean_asset_flow.lua`
  guards the OceanWaves case.

## Lua API

New usertypes (see `docs/LUA_API.md`): `OceanComponent` (fields,
`SetWaveAssetPath`, `GetWaveTime`/`SetWaveTime`, `WaveHeightAtWorld`,
`GetResolvedSource`/`GetResolvedReason`, `UpdateCameraFollow`,
`GetOceanVertexCount`), `BuoyancyComponent` (fields, `AddFloatPoint` /
`SetFloatPoint` / `RemoveFloatPoint` / `ClearFloatPoints`,
`GetLastSubmersion`), `OceanWaves` (`Resolve`, `LoadWaves`, band accessors),
plus `SceneNode:GetOceanComponent()` / `GetBuoyancyComponent()`,
`SceneNode:GetLocalRoattion()`/`GetWorldRoattion()`, `MathUtil.QuatToEulerRad`,
`Color` channel accessors.

Player.lua verification hooks: `FURY_CAM="px,py,pz,yawDeg,pitchDeg[,farCm]"`
pins a free-fly camera; `FURY_TOD=hours` pins time-of-day;
`FURY_OCEAN_DEBUG=0..3` pins the ocean debug view.

## Demo scenes (examples/Projects/ocean/)

Re-runnable builders (never write their source):

```
./fury exec Projects/outdoor/outdoor_water.bin Projects/ocean/setup_ocean_base.lua     # -> ocean_base.bin
./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_infinite.lua      # + 8 floatable props
./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_lake.lua          # finite lake in a basin
./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_island.lua        # island + shore foam + beached crate
```

- **ocean_infinite.bin**: 4 spheres + 3 cubes + a plank with BuoyancyComponent,
  half dropped 1 m up. `./fury Player.lua Projects/ocean/ocean_infinite.bin`.
- **ocean_lake.bin**: `TerrainLake` (gen_terrain_assets.py seed 12,
  `--flatten-radius 15000`: 2795 cm bed, banks 3200+) + finite ocean at 3095 cm.
- **ocean_island.bin**: `TerrainIsland` (`--island 0.7 --height-scale 4000`:
  18 m peak, 1 km map) + infinite ocean at 400 cm; foam rings the waterline;
  a crate floats in the shallows at (19500, ~420, 0).

Tests: `tests/lua/ocean_waves_spec.lua` (loop/tile continuity, determinism,
CPU-vs-payload), `tests/lua/buoyancy_smoke.lua` (flat fallback, settle at the
sampler height, cube levels, missing-ocean warning, clock-return determinism,
plank spin regression on live water), `tests/lua/ocean_scene_roundtrip.lua`
(every field survives save/load), `tests/lua/ocean_gpu_gen.lua` (gated
cross-path equivalence, skips cleanly), `tests/lua/ocean_asset_flow.lua`
(MergeInto transfers OceanWaves into the active scene's EntityManager).
