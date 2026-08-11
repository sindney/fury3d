# Proposal: add sky atmosphere + heightmap terrain with time-of-day

## Why

Fury3D renders cleared pixels as flat clear color — there is no sky at all —
and has no terrain primitive; the outdoor demo stands on a hand-placed "Grid"
mesh. An outdoor engine demo needs a believable sky and walkable landform.
This change ports the Unreal Engine SkyAtmosphere technique (EGSR 2020,
sebh/UnrealEngineSkyAtmosphere, MIT) to our GL 3.3 pipeline, adds sun/moon,
clouds and time-of-day, and adds a Unity-style heightmap terrain with
slope/height-based splat texturing and Jolt heightfield collision — all
demonstrated on a derived outdoor physics scene where the existing dominant
directional light doubles as the sun.

## GL 3.3 feasibility (what's in / what's out)

Verified against the reference HLSL and our renderer:

- **Atmosphere LUTs: doable.** Transmittance (256x64) and sky-view (192x108)
  LUTs are pixel shaders in the reference; the multi-scattering LUT (32x32) is
  the only compute shader and its groupshared tree-reduction becomes a plain
  per-fragment 64-direction loop. All render into `rgba16f` targets, which the
  engine already renders into today (`hdr_light`/`hdr_composite`).
- **Aerial-perspective volume: doable with a small engine addition.** The
  camera scattering volume (32 slices x 4 km) is a 3D texture; `TextureType`
  has no `TEXTURE_3D` yet, so we add it (enum + `glTexStorage3D` branch in
  `Texture::CreateEmpty` + per-slice `glFramebufferTexture3D` attach in `Pass`,
  mirroring the existing `SetArrayTextureLayer` precedent). GL 3.3 core
  guarantees 3D textures and 16F render targets.
- **No compute shaders, no tessellation, no UBOs** (GL 3.3): LUT generation
  uses fullscreen-quad fragment passes; terrain LOD uses CPU-built chunked
  meshes (geomipmap) instead of GPU tessellation; atmosphere constants go
  through ordinary per-draw uniforms (the engine has no uniform-block support
  and doesn't need it at this scale).
- **UE-style volumetric clouds: scoped down.** True 3D-worley volumetric
  clouds with temporal upsampling are out of scope; we ship a raymarched 2D
  cloud layer (coverage + detail noise textures, generated in Python) rendered
  at half resolution into the sky pass. Same visual family, fraction of the
  cost and complexity.
- **Aerial perspective applies to opaque geometry only** (in the combine
  pass); transparent surfaces don't receive inscatter — noted limitation.
- **HDR (PBR) pipeline only.** Sky atmosphere is inherently HDR; the LDR
  Lambert pipeline keeps the legacy clear color.

## What Changes

- **New `SkyAtmosphere` component** (serialized, inspector-editable):
  atmosphere parameters (planet/atmosphere radii, Rayleigh/Mie/ozone, ground
  albedo), sun angular diameter, and time-of-day settings (time in hours,
  day length, auto-advance). Owns the LUT textures and regenerates static
  LUTs on param change, view-dependent LUTs per frame.
- **New sky pass in the PBR pipeline** (`pass_sky`, DrawMode `SKY`, between
  `pass_combine` and `pass_transparent`): fills sky pixels (gbuffer depth
  >= 1.0, the convention already used by SunLight/SSAO/SSR) with raymarched
  atmosphere + sun disc + moon disc + cloud layer, writing into
  `hdr_composite` before transparents blend over it.
- **Aerial perspective in the combine pass**: `PbrCombine.glsl` gains a
  `u_atmosphere_enabled`-gated block that applies the camera scattering
  volume (inscatter + transmittance) to opaque scene pixels. No new pipeline
  permutation; samplers always bound (dummy 3D texture when no sky).
- **Sun/moon**: sun disc in the ray march (limb-darkened); moon as a
  Python-generated textured disc with a sun-lit phase terminator. When a sky
  is enabled, the scene's dominant directional light **is** the sun: TOD
  drives that light's rotation/color/intensity and the sky's sun direction
  reads back from it (single source of truth = the light).
- **Time-of-day**: `SkyAtmosphere` advances time on `Engine::OnUpdate`
  (opt-in), evaluates sun elevation/azimuth, day/night sun-moon intensity
  crossfade, and writes the bound directional light.
- **New `Terrain` component** (Unity Terrain/TerrainData-inspired):
  heightmap asset (16-bit raw `.r16` + JSON sidecar: resolution, world size
  in cm, height scale), 4 splat layers (grass/rock/mud/snow: albedo texture +
  tiling) blended by a splatmap generated from slope/height rules, chunked
  geomipmap LOD meshes built at load (runtime-built, not serialized) that
  render through the existing `MeshRender`/LOD-chain/octree path with a
  terrain gbuffer shader (per-pass shader override precedent).
- **`BodySetup` gains a `HeightField` shape type** building a
  `JPH::HeightFieldShape` from the sibling `Terrain` component's heights
  (static only, world transform baked per the existing convention), so the
  CharacterController fox walks on the terrain.
- **Python asset generator** `tools/gen_terrain_assets.py`: writes the
  startup resources — tileable Perlin/fbm heightmap (`.r16` + preview PNG),
  grass/rock/mud/snow albedo+roughness textures (noise-synthesized), splatmap
  from slope/height rules, cloud coverage/detail noise, moon albedo. Pure
  stdlib + no new engine dependency.
- **Demo scene**: repeatable `setup_terrain_sky_scene.lua` derives
  `outdoor_terrain.bin` from `outdoor_physics.bin` (source stays pristine):
  removes the flat Grid ground (mesh + collider), adds Terrain + heightfield
  BodySetup, adds SkyAtmosphere bound to the scene's directional light,
  re-seats the player spawn and crates on terrain heights.
- **Lua bindings** for both components + `docs/LUA_API.md` regen.
- **Editor inspector sections** for `SkyAtmosphere` (TOD slider, atmosphere
  params) and `Terrain` (asset paths, layer list, chunk/LOD settings).

## Capabilities

### New Capabilities

- `sky-atmosphere`: the `SkyAtmosphere` component, GL 3.3 LUT pipeline
  (fragment-pass port of the UE technique), sky pass integration, aerial
  perspective hook, sun/moon discs, 2D cloud layer, time-of-day and the
  dominant-directional-light-as-sun binding.
- `heightmap-terrain`: the `Terrain` component (heightmap asset format,
  chunked LOD mesh build, splat shader), the Python resource generator, and
  terrain Lua bindings.

### Modified Capabilities

- `body-setup-component`: new `HeightField` shape type sourced from a sibling
  `Terrain` component (static, world-baked).
- `hdr-pipeline`: PBR pipeline gains a sky pass between combine and
  transparent, and the combine pass applies aerial perspective when a sky is
  active.
- `lua-scene-scripting`: new bindings (`SkyAtmosphere`, `Terrain`,
  `Color.Lerp`) used by the demo setup script and tests.

## Impact

- **Core engine** (`engine/Fury/`): new `SkyAtmosphere.{h,cpp}`,
  `Terrain.{h,cpp}`; `TextureType` gains `TEXTURE_3D` (+ `Texture::CreateEmpty`
  branch, `RenderUtil::GetDummyTexture3D`); `Pass` gains a 3D-slice attach;
  `DrawMode` gains `SKY`; `PrelightPipeline` gains LUT update + `DrawSky`;
  `PbrCombine.glsl` aerial-perspective block; `SceneNode::ComponentRegistry`
  +2 entries; `BodySetup` heightfield branch (Jolt header already vendored).
- **Resources** (`examples/Resource/`): new `Shader/Atmosphere/*.glsl`
  (transmittance, multiscatter, skyview, camera-volume, sky+clouds raymarch),
  `Shader/Terrain/DrawTerrain.glsl`, pipeline JSON pass entry; generated
  terrain/sky textures under `examples/Projects/outdoor/Terrain/`.
- **Tools**: `tools/gen_terrain_assets.py` (stdlib-only; no engine build
  dependency).
- **Lua**: bindings in `LuaBindings.cpp` (+ typed `GetComponent` getters),
  `docs/LUA_API.md` regenerated.
- **Editor**: 2 new inspector sections in `EditorNodeProperties.cpp` +
  `ComponentRenderTable` rows.
- **Content**: `examples/Projects/outdoor/setup_terrain_sky_scene.lua` →
  `outdoor_terrain.bin`; `tests/lua/` smoke tests (terrain heights vs physics
  raycast, scene build re-run).
- **Non-breaking**: scenes without the new components render exactly as today
  (sky pass no-ops without an enabled `SkyAtmosphere`; combine AP block gated
  off; `ShapeType` enum only appends).

## Known traps accounted for (from project memory)

- **GL sampler trap**: every new sampler (LUTs, 3D volume, splat layers, noise
  textures) is bound every draw, with dummy 2D/3D fallbacks when sky/terrain
  are absent — unbound samplers alias unit 0 and core GL kills the draw.
- **No `#include` in shaders**: the shared atmosphere math is duplicated
  across the 5 small atmosphere shaders via a generated common section (the
  Python/Lua-free option: keep a single `AtmosphereCommon` text block that a
  tiny generator inlines, or hand-maintain copies — decided in design).
- **x100 scaled-ancestor trap**: terrain heightfield + static colliders bake
  world transforms through matrix math (`MathUtil::Decompose`), never
  piecewise `GetWorld*` reads; terrain sits at scene root unscaled.
- **CWD-relative texture paths**: generated textures are referenced
  working-dir-relative from the scene (`Terrain/height.r16` convention), same
  as the Fox texture fix in `setup_physics_scene.lua`.
- **Headless `fury exec` has no GL**: LUT/shader verification goes through
  windowed screenshot automation (`--screenshot`), physics/height logic
  through headless lua tests (existing `Physics.Step` pattern).
- **ASCII-only engine source** (enforced at HEAD): all new shaders/C++ stay
  ASCII, comments compact.
