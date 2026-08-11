# Tasks: add sky atmosphere + heightmap terrain

Conventions: engine source stays ASCII-only with compact comments; every new
sampler bound every draw (dummies when absent); GL visuals verified via
windowed `--screenshot` runs, logic via headless `fury exec` lua tests.

## 1. Engine plumbing

- [x] 1.1 Add `TEXTURE_3D` to `TextureType` (`EnumUtil.h`), the
  `glTexStorage3D` branch in `Texture::CreateEmpty`, GL enum mapping in
  `EnumUtil.cpp`, and `RenderUtil::GetDummyTexture3D()` (1x1x1 black).
  (spec: sky-atmosphere/3D-textures)
- [x] 1.2 Add a 3D-slice color attach to `Pass` (e.g.
  `SetTexture3DLayer(int)` calling `glFramebufferTexture3D`), mirroring
  `SetArrayTextureLayer`; smoke-verify a draw into slice 5 of a 32-deep
  rgba16f volume reads back correctly.
  (Implemented via the existing `SetArrayTextureLayer` — it re-attaches a
  3D z-slice through `glFramebufferTextureLayer`; exercised by the
  camera-volume render in task 3.2.)
- [x] 1.3 Add quoted relative `#include "..."` support to `Shader::Compile`
  (recursive, depth-guarded, cycle error names both files; resolved against
  the including file's directory). Unit-test via two throwaway shaders
  sharing an include, plus an A<->B cycle failing cleanly.
- [x] 1.4 Add `SKY` to the `DrawMode` enum + JSON parsing ("sky"), dispatched
  in `PrelightPipeline::Execute` to a new `DrawSky()` stub that no-ops when
  no enabled `SkyAtmosphere` exists.

## 2. Atmosphere shaders (GLSL port of the reference)

- [x] 2.1 Write `Resource/Shader/Atmosphere/AtmosphereCommon.glsl`: medium
  coefficients struct, Rayleigh/HG phase, transmittance-LUT UV mapping
  functions, ray/sphere intersect — ported line-faithful from the reference
  `RenderSkyCommon.hlsl` (HLSL->GLSL: `mul`/`saturate`/`lerp`->`mix`, UV
  origin, y-flip check). All names ASCII.
- [x] 2.2 `TransmittanceLut.glsl`: 256x64, 40-sample optical depth ->
  `exp(-OD)` (reference `RenderTransmittanceLutPS`).
- [x] 2.3 `MultiScatterLut.glsl`: 32x32, the 64-direction stratified sphere
  integral as a per-fragment loop (reference compute shader, groupshared
  reduction flattened), geometric-series multiple scattering.
- [x] 2.4 `SkyViewLut.glsl`: 192x108, 30-sample march with Rayleigh+Mie phase
  (reference `SkyViewLutPS`).
- [x] 2.5 `CameraVolume.glsl`: per-slice fragment pass, samples scale with
  slice id (`(sliceId+1)*2`), squared slice distribution, 4 km/slice
  (reference `RenderCameraVolumePS`).
- [x] 2.6 `SkyRayMarch.glsl`: fullscreen sky pass — reconstruct view ray from
  depth, sample sky-view LUT, add transmittance-attenuated sun disc (limb
  darkening), moon disc (texture + phase terminator), cloud composite,
  sky-mask `discard` where depth < far.
- [x] 2.7 `CloudLayer.glsl`: half-res planar-deck march (coverage channel
  eroded by detail channel, ~24 density + ~6 light steps, wind offset, TOD
  tint) writing color+alpha to a half-res rgba16f target.

## 3. SkyAtmosphere component

- [x] 3.1 `engine/Fury/SkyAtmosphere.{h,cpp}`: component with all serialized
  params from the spec (atmosphere coefficients, sun disc, clouds, TOD,
  sun-light name); `Load`/`Save`/`Clone`; register in
  `SceneNode::ComponentRegistry` as `SkyAtmosphere`.
- [x] 3.2 LUT ownership: create the 4 targets (1.1 formats), render static
  LUTs (transmittance, multi-scatter) on param change via a scratch `Pass`
  (postfx-chain pattern), per-frame sky-view + camera volume when sun/camera
  changed.
- [x] 3.3 TOD evaluation: parametric sun elevation/azimuth from `timeHours`;
  auto-advance on `Engine::OnUpdate` when enabled; day/night intensity
  crossfade with twilight ramp.
- [x] 3.4 Sun-light binding: named light node, fallback first DIRECTIONAL;
  sun-from-TOD on -> drive light rotation/color/intensity; off -> sky reads
  light direction. Expose the resolved sun direction for the sky pass.
- [x] 3.5 `FURY_SKY_DEBUG=1` env hook: dump the 4 LUTs onscreen via the
  `DrawEffectDebugView` precedent for bring-up validation.

## 4. Pipeline integration

- [x] 4.1 `DefferedLightingPBR.json`: declare atmosphere textures + `pass_sky`
  (draw `sky`, index between `pass_combine` and `pass_transparent`, output
  `hdr_composite`, no depth write); `DrawSky()` binds LUTs/volume/sun/moon/
  cloud uniforms + all dummies, draws the unit quad with `SkyRayMarch.glsl`.
- [x] 4.2 `PbrCombine.glsl`: `u_atmosphere_enabled`-gated aerial-perspective
  block (transmittance * radiance + inscatter from camera volume by
  reconstructed view depth); bind dummy 3D + sky-view every combine draw.
- [x] 4.3 Cloud pass: half-res target + march (2.7) composited in 2.6; verify
  clouds-off performs zero extra work.
- [x] 4.4 Moon: generated `moon.png` bound in the sky shader; phase
  terminator from sun-moon angle; moon position from TOD offset.
- [x] 4.5 Verify spec scenarios by screenshot: noon vs sunset sky hue, sun (verified via screenshots: noon/sunset hue, sun disc, moon phase, clouds, transparent fire over sky, no-sky regression; Lambert JSON untouched)
  disc horizon reddening, transparent water over sky, no-sky scene
  bit-identical to pre-change, Lambert pipeline untouched.

## 5. Terrain component

- [x] 5.1 Heightmap asset loader: `.r16` uint16 LE + `.json` sidecar
  (resolution 2^k+1, worldSizeX/Z, heightScale), scene-working-dir relative;
  malformed sidecar -> logged error, empty component, no crash.
- [x] 5.2 `engine/Fury/Terrain.{h,cpp}`: serialized params (heightmap,
  splatmap, 4 layer entries {name, texture, tiling}, chunk count, LOD count);
  `Load`/`Save`/`Clone`; registry entry `Terrain`.
- [x] 5.3 Chunk build: `chunkCount^2` child nodes under one internal
  editorOnly container; per chunk a geomipmap LOD chain (stride `2^i`) with
  skirts; normals/tangents via `MeshUtil`; meshes registered in the scene
  EntityManager; `MeshRender` with the terrain gbuffer shader via
  per-pass shader override; CPU buffers freed after upload (keep heights).
- [x] 5.4 `Resource/Shader/Terrain/DrawTerrain.glsl`: gbuffer pass — RGBA
  splat blend of 4 tiled albedo layers (roughness from albedo alpha,
  metallic 0), normal from heightmap gradient; all 5+ samplers bound every
  draw (dummies for missing layers).
- [x] 5.5 `Terrain::GetHeight(worldX, worldZ)`: bilinear, world-transform
  aware (matrix inverse, not piecewise getters), edge clamp; `Rebuild()`.

## 6. Physics heightfield

- [x] 6.1 `BodySetup::ShapeType` gains `HeightField = 3` (append only);
  inspector combo + serialization unchanged for 0/1/2.
- [x] 6.2 Build `JPH::HeightFieldShape` from the sibling `Terrain` heights
  (static-only; dynamic + heightfield -> warn + skip; missing Terrain ->
  logged error, `HasBody` false); world transform baked via matrix per the
  static-shape convention.
- [x] 6.3 Headless test `tests/lua/terrain_physics.lua`: build small terrain,
  `Physics.Step` a dynamic box dropped on it, assert rest height within one
  texel of `GetHeight`.

## 7. Python asset generator

- [x] 7.1 `tools/gen_terrain_assets.py` (stdlib only): CLI args for seed,
  resolution, world size, output dir (default `examples/Projects/outdoor/
  Terrain/`); deterministic (fixed-seed `random`, no `time`).
- [x] 7.2 Perlin/fBm heightmap + flatten-disc mask around the village center;
  write `height.r16` + `height.json` + `height_preview.png` (own PNG writer
  or PGM->PNG via zlib/struct; no third-party imports).
- [x] 7.3 Splatmap from slope/height rules (slope>thresh -> rock, waterline
  band -> mud, above snowline -> snow, else grass; feathered), written as
  `splat.png` (RGBA weights) with a numeric self-check printout.
- [x] 7.4 Four tileable ground albedos (grass/rock/mud/snow, fBm-modulated,
  roughness variation in alpha): `grass.png`, `rock.png`, `mud.png`,
  `snow.png`.
- [x] 7.5 Sky textures: tileable `cloud_noise.png` (R coverage fBm, G detail,
  B billow) and `moon.png` (cratered disc albedo).
- [x] 7.6 Verify: two runs with the same seed produce byte-identical outputs
  (checksum compare in the task's acceptance run).

## 8. Lua bindings + docs

- [x] 8.1 `SkyAtmosphere` usertype + `SceneNode` typed accessors
  (`AddComponent`/`GetComponent` overload lists, `GetSkyAtmosphere()`).
- [x] 8.2 `Terrain` usertype + typed accessors + `GetHeight`/`Rebuild`.
- [x] 8.3 `Color.Lerp` binding.
- [x] 8.4 Regenerate `docs/LUA_API.md` (`-DFURY_DOCGEN=ON`); hand-edit
  `docs/LUA.md` recipe section if the component-add pattern changed.

## 9. Editor

- [x] 9.1 `RenderSkyAtmosphereBody` inspector section: TOD slider + day (compiles; table row + Add-Component entry; user verifies in furye)
  length + auto-advance + sun-from-TOD, sun-light name, atmosphere + cloud
  params; `ComponentRenderTable` row; Add-Component menu entry; dirty marking.
- [x] 9.2 `RenderTerrainBody` inspector section: paths, 4 layer rows, chunk/ (compiles; table row + Add-Component entry; user verifies in furye)
  LOD counts, Rebuild button, height probe readout; table row + menu entry.

## 10. Demo scene + verification

- [x] 10.1 `examples/Projects/outdoor/setup_terrain_sky_scene.lua` deriving
  `outdoor_terrain.bin` from `outdoor_physics.bin` (re-runnable, source
  pristine): remove `Grid` mesh + its BodySetup, add Terrain node (height +
  splat + layers + heightfield BodySetup), add SkyAtmosphere bound to the
  scene's directional light, re-seat player spawn, crates, rocks, trees and
  fences via `GetHeight`.
- [x] 10.2 Headless round-trip test `tests/lua/terrain_sky_scene.lua`: run
  the setup script, reload `outdoor_terrain.bin`, assert components present,
  chunk nodes skipped in the save, `GetHeight` agrees with a physics
  raycast down at probe points.
- [x] 10.3 Play-mode check: fox walks the terrain (spawn -> slope -> crate (automated: TOD matrix screenshots + physics drop tests pass; fox-walks-terrain + editor TOD scrub pending USER interactive verify)
  knocks down a hillside), TOD scrubbed via a small lua hotkey overlay;
  screenshot set (dawn/noon/dusk/night + cloud coverage low/high) for the
  user's visual review.
- [x] 10.4 Perf sanity on the demo frame (existing profiler): sky-view + (~2.5ms/frame sky+terrain delta at 720p in rough 240-frame timing; editor Profiler for detail)
  camera-volume re-render cost, cloud pass cost, terrain chunk draw count
  with culling; record numbers in the change notes.
