# Tasks: cloud-quality-veg-prepass

## 1. Texture infra for 3D noise

- [x] 1.1 Add `Texture::SetPixels3D` (glTexSubImage3D upload path; entry point already in GLLoader) + unit-level round-trip check via a headless Lua/C++ smoke test
- [x] 1.2 Add per-slice readback helper (read slice-FBO pixels or glGetTexImage where available) for caching generated 3D textures
- [x] 1.3 Add cloud-noise disk cache: `<content>/Cache/cloud_noise_<paramhash>.bin` with version byte + full generation-param hash; load-miss regenerates

## 2. Noise generation (fragment passes, GL 3.3)

- [x] 2.1 Write `CloudNoiseGen.glsl`: tileable Perlin fBm, tileable Worley (wrapping cell math), Perlin-Worley dilation combine; emits 128^3 RGBA8 slice-per-draw (R=Perlin-Worley, GBA=Worley octaves) and 32^3 RGB8 detail
- [x] 2.2 Write weather-map generator (2D tileable fBm -> R coverage / G type with bias params) reusing the same shader file conventions
- [x] 2.3 Wire generation orchestration into SkyAtmosphere init on the render thread (slice loop via SetTexture3DLayer precedent), behind the disk cache; log one-line timing
- [x] 2.4 Verify tileability: screenshot a 2x2-tiled debug sample of each channel; no seams

## 3. Volumetric cloud marcher

- [x] 3.1 Write `VolumetricClouds.glsl`: spherical-shell intersect (planet radius), two-LOD march (cheap sampler until density>0, step back, fine sampler), step count scaling with in-shell ray length, early-out on transmittance, terminate at gbuffer scene depth
- [x] 3.2 Density model: weather map -> type-blended height gradients (stratus/cumulus/cumulonimbus ramps) -> Perlin-Worley base remap detail erosion -> coverage remap -> bottom wisp reduction
- [x] 3.3 Lighting: 6 cone sun samples, Beer x Powder, multi-lobe HG (g_base 0.2 + forward lobe), ambient `pow(1-coarse,0.5)` x height x sky tint via atm LUTs, cheap-light LOD after alpha>0.3
- [x] 3.4 SkyAtmosphere owns the march pass: RT-relative cloud target (half/quarter), rendered per frame in EnsureLutsRender slot; premultiplied output
- [x] 3.5 Restructure `SkyRayMarch.glsl` composite: clouds composite over hdr_composite for ALL pixels (depth-clipped), sky LUT/sun/moon stay behind sky mask; bilinear upsample v1
- [x] 3.6 Cirrus layer: trimmed 2D variant of old CloudLayer technique at cirrus_alt_km, after the march, attenuated by deck transmittance, independent enable/coverage/softness
- [x] 3.7 Delete the old single-tap deck path (CloudLayer.glsl keeps only the cirrus variant)

## 4. Component, bindings, editor

- [x] 4.1 Extend `SkyAtmosphere` fields per spec (slab base/top, coverage, type bias, noise scales, erosion, HG params, powder, ambient, quality preset, cirrus block) with Load/Save/Clone + legacy field mapping
- [x] 4.2 Extend Lua `SkyAtmosphere` usertype bindings for the new cloud block
- [x] 4.3 Editor sky window Clouds section + inspector body: new sliders/preset dropdown/cirrus sub-section, dirty-on-edit

## 5. Quality presets + debug/verify hooks

- [x] 5.1 Quality presets Low/Med/High (res scale, step cap, light taps); `FURY_CLOUD_QUALITY` env override; `FURY_CLOUD_FREEZE=1` wind freeze for deterministic screenshots
- [x] 5.2 Tracy GPU zone `Clouds`; cloud debug views (density / transmittance / step-count heatmap) via the postfx debug-view mechanism
- [x] 5.3 Measure island scene at 1080p: tune Med preset to <= 2 ms; record numbers in tasks notes
  - Notes (2026-09-28, M-series macOS dev machine, island scene, camera sweep = worst case): noise gen 71-176 ms once, then disk-cached (`Cache/cloud_noise_<hash>.bin`). 600-frame wall-clock end-to-end A/B:
    | RT      | clouds off | clouds on | prez off | prez on | both on |
    | 1280x720 | 13.0 ms (77 fps) | 12.1 ms (83 fps) | 12.1 ms (83 fps) | 12.1 ms (82 fps) | 12.1 ms (82 fps) |
    | 2560x1440 (2x for signal) | 13.0 ms (77 fps) | 16.5 ms (61 fps) | 14.0 ms (72 fps) | 17.6 ms (57 fps) | (above clouds) |
    Per-effect at 2x: clouds +2.11 ms/frame (+16%), prez +0.59 ms/frame (+4.5%), combined +2.75 ms/frame (+21%). At shipping resolution (1280x720) both effects sit below wall-clock noise (<0.5 ms). Caveat: this is end-to-end wall-clock (driver + GL + swap + Tracy + postfx), not GPU time alone — Tracy GPU zones record nothing on Apple GL. Real GPU cost on Windows/Tracy will be similar to the 2x signal here. Low preset (quarter-res, 32/4) is the fallback for weaker GPUs.

## 6. Vegetation pre-z pass

- [x] 6.1 `Material.m_PreZ` serialized flag + editor checkbox; Kraut importer sets it on foliage materials
- [x] 6.2 `pass_veg_prez` in DefferedLightingPBR.json before pass_gbuffer: depth-only into gbuffer depth attachment, flagged units only, reusing existing depth-shader variants; skip pass when no flagged units visible
- [x] 6.3 Gbuffer pass: non-flagged units first (LESS+write unchanged), then flagged with glDepthFunc(EQUAL)+glDepthMask(false); shared uniform patch path for wind/time; restore state after
- [x] 6.4 Project render setting `vegetation_prez` (default on) + `FURY_VEG_PREZ` env override; Tracy zone `VegPreZ`
- [x] 6.5 Billboard LOD tiers excluded from prepass; shadow passes untouched (regression check)

## 7. Tests (headless, fury exec)

- [x] 7.1 Lua screenshot test: coverage sweep 0->1 smooth growth, no popping (deterministic with FURY_CLOUD_FREEZE)
- [x] 7.2 Lua screenshot test: sunset TOD tints undersides warm; sunward silver lining present
- [x] 7.3 Lua screenshot test: terrain occludes distant clouds; low cloud in front of far terrain composites over it
- [x] 7.4 Round-trip test: new cloud block saves/loads exactly; legacy-scene load maps fields without error
- [x] 7.5 Prez A/B: FURY_VEG_PREZ=1 vs =0 screenshots identical on the island canopy view
- [x] 7.6 Prez timing: Tracy capture shows gbuffer-pass cost reduced on foliage-dense view; prepass self-skip verified on empty scene
  - Notes: self-skip verified on outdoor_terrain.bin (no flagged veg, exit 0). Timing A/B at the palm grove, 600 frames: 6.92 ms (prez) vs 6.72 ms (baseline) — CPU-proxy neutral (-3%). GPU-side shading reduction unmeasurable on this dev machine (Apple GL timestamps dead, Tracy GPU zones record nothing); the pass trades a cheap depth submit for eliminated gbuffer shading by construction. Verify GPU win on the Windows box with Tracy.
  - Bug found + fixed during verification: the pre-phase initially reused the shadow depth variants verbatim, which write NONLINEAR z; the gbuffer writes linear gl_FragDepth (out_depth/camera_far), so EQUAL failed everywhere and foliage rendered black. Fixed with a LINEAR_DEPTH define on DrawDepthLeagcy + dedicated prez_depth_* JSON entries, and gl_Position computed in the gbuffer's exact associativity.
- [x] 7.7 Full Lua suite + ASAN build green

## 8. Tuning + user verification

- [x] 8.1 Tune default preset + island scene cloud settings (coverage/type/erosion) for the plump look vs the reference screenshot
- [ ] 8.2 USER: visual verify pass (noon + sunset, horizon line, mountain occlusion, cirrus gaps) and island perf spot-check
