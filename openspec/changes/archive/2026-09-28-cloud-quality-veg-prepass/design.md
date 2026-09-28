# Design: cloud-quality-veg-prepass

## Context

The SkyAtmosphere (UE-style LUT atmosphere, GL 3.3 fragment passes) reads as physically real, but its companion cloud layer (`CloudLayer.glsl`) evaluates coverage with ONE 2D noise tap per ray at the slab midpoint plus a single straight-up shading probe. Result: flat, hard-edged white cutouts (see user screenshots) next to a photographic sky. The reference look (Horizon Zero Dawn / Forbidden West talks) is achieved NOT by huge sample counts but by the density model: Perlin-Worley base noise shaped by per-type height gradients, coverage applied as a remap, detail Worley erosion via remap, plus Beer-Powder lighting with a Henyey-Greenstein phase and cone-sampled sun transmittance. Guerrilla shipped this on PS4 at 1.2-2.0 ms (960x540 working res, 60-90 view samples, 6 light samples, two-LOD marcher); mobile titles use the same model at lower budgets. Our gap is the model, not the step count.

Vegetation (kraut trees/grass: alpha-tested MASK + two-sided + wind, instanced) shades every overdrawn foliage layer in the gbuffer pass. No depth prepass exists anywhere in the engine; the shadow system already ships the exact depth-shader variants a prepass needs (ALPHA_TEST x WIND x INSTANCED x SSBO).

Existing infra this design reuses: `TEXTURE_3D` alloc + per-slice FBO rendering (camera aerial-perspective volume precedent), shader `#include`, half-res cloud target + premultiplied composite in `pass_sky`, `atm_sun_transmittance()` LUT access for cloud sunlight, `DrawMode` extension pattern, material flag bits + shader variant assembly (`PrelightPipeline.cpp:1534`), draw-command cache keyed by node/tier/submesh/pass, Tracy zones, `Texture::GetTemporary` pool, editor settings registry (`Settings.*` + `FURY_*` env overrides).

Constraints: GL 3.3 floor (macOS caps at 4.1; compute unusable in practice), all GL on the render thread, no TAA/reprojection infra anywhere (greenfield), shaders are data files under `examples/Resource/Shader/`, passes declared in `examples/Resource/Pipeline/DefferedLightingPBR.json`.

## Goals / Non-Goals

**Goals:**
- Plump, volumetric-looking clouds (rounded cumulus, wispy bases, silver linings, TOD-tinted) from ground view at <= ~2 ms GPU at 1080p half-res on the dev machine.
- Multi-layer sky: volumetric tropospheric deck + cheap 2D cirrus above it.
- Clouds occluded by / occluding terrain correctly in both directions.
- Vegetation pre-z pass that measurably cuts gbuffer shading cost on foliage-dense views, pixel-identical output, opt-in per material.
- Headless-verifiable: deterministic screenshots (frozen wind), Tracy budgets, A/B env toggles.

**Non-Goals:**
- Fly-through / in-cloud rendering (step 2; explicitly out, as in HZD).
- Temporal reprojection / 16-frame accumulation (no TAA infra; revisit only if half-res + presets miss budget).
- Superstorm stencils, vortex rings, lightning (HFW features; future change).
- Cloud shadows onto the world, cloud-into-SSR reflections.
- Prebaked/cooked noise assets via the pak pipeline (runtime cache is enough; can promote later).

## Decisions

### D1: Density model = HZD/Nubis, not "more 2D octaves"

Density = weather-map (coverage, type from 2D RG texture over world XZ) -> per-type height gradient (stratus/cumulus/cumulonimbus ramp functions of height fraction h, blended by type) -> base shape `remap(perlin_worley, 1 - erosion_strength * detail_worley, 1, 0, 1)` -> coverage as remap erosion `remap(d, 1 - coverage, 1, 0, 1)` -> bottom wisp reduction `* smoothstep(0, 0.15, h)`-style factor. Remap (not multiply) preserves core density while carving - this plus the Perlin-Worley billow dilation is what makes cumulus read plump instead of foggy.

Alternatives rejected: (a) keep the single-tap 2D deck and add fake shading - that IS today's fake look; (b) billboard/impostor clouds - pop artifacts, no TOD integration; (c) curl-noise fluid advection - cost + authoring complexity, HFW skipped fluid sim too.

Wind scrolls only the noise sample offsets; the weather map stays static so large-scale structure is art-stable (HFW p.21).

### D2: Fragment shaders on GL 3.3; no compute

All 26 engine shaders are `#version 330`; macOS caps at GL 4.1 and the compute path has zero in-tree consumers. The LUT suite already proves heavy math works in fragment passes. Noise generation = one fullscreen pass per 3D slice (128 + 32 passes, once per cache miss) using the existing `Pass::SetArrayTextureLayer`/`SetTexture3DLayer` precedent from the camera volume.

### D3: Noise textures generated at load, cached on disk

Textures: 128^3 RGBA8 (R = Perlin-Worley = Perlin fBm dilated by inverted-Worley billows; GBA = Worley fBm octaves), 32^3 RGB8 detail Worley, 256x256 RGBA8 weather map (generated from tileable 2D fBm with coverage/type channel split + user bias). ~8.5 MB total (HZD's budget). Generated on the render thread during sky init; result read back (per-slice `glReadPixels` from the slice FBO, or `glGetTexImage` where available) and written to `<content>/Cache/cloud_noise_<paramhash>.bin`; warm start uploads via a new `Texture::SetPixels3D` (the one real gap in the 3D texture path - `glTexSubImage3D` entry point already loaded). Cache key = hash of generation params + format version byte.

Alternatives rejected: (a) CPU generation - 128^3 Worley is seconds of CPU time at startup; (b) shipping prebaked assets - couples to the cook pipeline; the cache gives the same steady-state cost; (c) regenerate every run - wastes ~100-300 ms per launch, annoying in editor iteration.

### D4: March schedule - two-LOD, budgeted, ground-view

- March the spherical shell [cloud_base_km, cloud_top_km] (planet-radius curvature, so clouds sink into the horizon); skip entirely for rays starting above the shell (ground-view assumption).
- Two-LOD: cheap sampler (base noise x height gradient x coverage remap; 1x 3D tap + 1x weather tap) at coarse step until density > 0, then step back and march fine (full erosion model) until transmittance < ~0.01. Consecutive empty cheap samples stay cheap.
- Step budget scales with in-shell ray length (short near-zenith rays ~24 steps, horizon rays up to the preset cap 48/64/96 for Low/Med/High); step size grows with distance from camera.
- Pixel kill: march only where the cloud can be visible - terminate at scene depth (linear gbuffer depth, same convention SSR uses), so terrain-covered pixels exit immediately.
- Lighting: 6 cone-spread sun samples (PS4 count) with Beer `exp(-d)` x Powder `(1 - exp(-2d))`, multi-lobe HG (base g 0.2 + forward lobe ~0.7, both tunable), sun color/intensity x `atm_sun_transmittance()` so TOD matches the sky automatically. Ambient = `pow(1 - coarse_density, 0.5)` (HFW p.37 - reuses the coarse sample, no ambient march) x height gradient x sky ambient tint. Light sampling switches to the cheap sampler once alpha > 0.3 (HZD LOD trick).

Why not temporal accumulation (HZD's 16-frame / HFW's motion-vector reprojection): zero reprojection/history infra exists; it is the single biggest complexity item in those decks. Half-res + two-LOD + pixel-kill gets PS4-class budgets without it. If profiling misses budget, add a 2-phase checkerboard (no motion vectors, just 2-frame alternate sampling + clamp) as a follow-up.

### D5: Composite restructure - clouds on all pixels, clipped by scene depth

Today `SkyRayMarch.glsl` discards non-sky pixels, so clouds can never appear in front of terrain. New flow: the cloud march clips at scene depth (from gbuffer linear depth), and `pass_sky` composites premultiplied cloud over `hdr_composite` for every pixel (`c = cloud.rgb + (1-a)*c`), while sky LUT/sun/moon stay behind the sky mask. This fixes both occlusion directions with one depth sample per march ray. Cloud target becomes RT-relative (half/quarter of RT) instead of fixed 640x360. Upsample stays bilinear in v1 (as today); a depth-aware bilateral upsample is the documented escape hatch if mountain-edge halos show in verification.

### D6: Cirrus = today's 2D technique, re-purposed up high

The current single-tap deck is exactly the right cost profile for thin high cirrus (HZD renders the alto/cirro class as 2D for the same reason). Keep a variant of `CloudLayer.glsl` as the cirrus pass (re-tuned wispy: lower density, higher softness, anisotropic stretch), evaluated after the march at `cirrus_alt_km` > slab top, attenuated by the deck's transmittance, independently togglable. This delivers the multi-layer read at ~zero extra cost.

### D7: Vegetation pre-z = flagged materials, one extra pass, EQUAL reuse in gbuffer

- `Material.m_PreZ` (serialized, editor checkbox; Kraut importer sets it on foliage materials). Project render setting `vegetation_prez` (default on) + `FURY_VEG_PREZ` env override.
- The prepass is an inline depth-only pre-phase at the start of `pass_gbuffer` execution (not a separate JSON pass): same FBO, same depth attachment, color mask off, flagged units only, using the EXISTING depth-shader variants (`leagcy_depth_alphatest_wind_instanced[_ssbo]` etc. - same code path as shadow depth, camera projection instead of light). A separate JSON pass was considered and dropped: it would need a new DrawMode + texture rewiring for zero benefit over two GL state flips.
- After the pre-phase: draw non-flagged units first (unchanged `LESS` + depth write - they now also early-out against foliage-seeded depth, a bonus saving), then flagged units with `glDepthFunc(EQUAL)` + `glDepthMask(false)`. Two state flips per pass, not per unit (flagged units are contiguous in the instanced loop). Wind uniforms come through the same per-frame patch path, so vertex displacement is bit-identical and EQUAL is reliable.
- In `pass_gbuffer`, draw non-flagged units first (unchanged `LESS` + depth write - note they now also early-out against foliage-seeded depth, a bonus saving), then flagged units with `glDepthFunc(EQUAL)` + `glDepthMask(false)`. Two state flips per pass, not per unit (flagged units are contiguous in the instanced loop). Wind uniforms come through the same per-frame patch path, so vertex displacement is bit-identical and EQUAL is reliable.
- Billboard LOD tiers excluded (single quad, negligible overdraw). Shadow passes untouched.

Alternatives rejected: (a) full-scene prez - doubles vertex cost on everything for a foliage-localized problem; (b) sorting foliage front-to-back - helps but still shades every visible layer (leaf holes don't align); (c) Hi-Z/stencil culling - overkill at this scale.

Risk control: the A/B requirement (FURY_VEG_PREZ screenshots identical) catches any divergence between the two depth paths immediately.

### D8: Settings, bindings, verification hooks

- `SkyAtmosphere` gains the volumetric cloud block (per spec) with Load/Save/Clone; legacy fields map forward (coverage/altitude/thickness/wind/density/fade/scale carry over). Lua bindings extended to match (existing `SkyAtmosphere` usertype pattern). Editor: sky window Clouds section + inspector body get the new sliders (preset dropdown, coverage, type, erosion, lighting params, cirrus sub-section).
- Tracy zones: `Clouds` (GPU), `VegPreZ`, plus existing gbuffer zone for the A/B comparison.
- Debug: `FURY_CLOUD_QUALITY=low|med|high`, `FURY_CLOUD_FREEZE=1` (wind offset frozen for deterministic screenshots), cloud debug views (density / transmittance / step-count heatmap) via the existing postfx debug-view mechanism.
- Headless tests in `tests/lua/` driven by `fury exec`: coverage sweep, TOD tint, terrain occlusion both directions, round-trip, prez A/B identity, prez timing sanity.

## Risks / Trade-offs

- [GL 3.3 dynamic-loop march cost on the macOS GPU is unknown] -> Land the marcher behind the quality preset from day one; measure with Tracy on the island scene before tuning; Low preset (quarter-res, 24/48 steps, 4 light taps) is the floor.
- [Half-res clouds show aliasing/crawl at hard edges without temporal accumulation] -> Depth-clipped march + bilinear first; documented follow-up: 2-phase checkerboard; do NOT build reprojection in this change.
- [EQUAL depth reuse z-fights if any uniform diverges between prez and gbuffer draws (wind time, alpha cutoff)] -> both paths share the draw-command uniform patch code; A/B screenshot test must pass.
- [Prepass adds a second geometry submission for flagged units; on foliage-sparse scenes it is pure overhead] -> pass skips itself when no flagged units are visible; setting can disable globally.
- [3D noise cache stale across param edits] -> cache key hashes every generation parameter + version byte; mismatch regenerates.
- [Weather map authored procedurally may look samey] -> expose per-channel scale/bias; allow swapping in a painted texture later (same sampling contract).
- [Reading back 3D slices is sync-stall territory] -> one-time cost at load behind the cache; never per-frame.
- [Memory: +8.5 MB textures, +half-res RGBA16F target] -> trivial vs LUT suite; target pooled via existing temp/owned-target conventions.

## Migration Plan

1. Land infra (SetPixels3D, noise gen + cache) - no visual change.
2. Land marcher + composite behind `clouds_enabled` with legacy field mapping - visual change intended; old `CloudLayer.glsl` path deleted (cirrus variant keeps a trimmed copy).
3. Land prez behind the render setting, default on; Kraut importer flag; A/B verified.
4. Verify: Lua screenshot suite + Tracy budgets + ASAN; then user visual pass on island + sunset TOD.
5. Rollback: `clouds_enabled=false` restores sky-only behavior; `FURY_VEG_PREZ=0` / setting off restores old veg path. No asset or scene-format break (legacy fields map; unknown-old-fields policy already tolerant).

## Open Questions

- Exact default preset budgets (step caps) - set during implementation from Tracy on the dev machine; spec target <= 2 ms at 1080p half-res Med.
- Whether bilinear half-res upsample holds up at mountain silhouettes, or the depth-aware upsample escape hatch is needed - decided in visual verification.
- Weather map: procedural-only at ship, or also expose a texture path override (the 2D deck already had `SetCloudNoisePath`; cheap to keep an override hook) - lean: keep an override.
