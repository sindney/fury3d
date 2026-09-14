# add-kraut-vegetation — tasks

## 1. Fork-side Kraut-CLI export (companion work in the Kraut-CLI repo)

- [x] 1.1 In the Kraut-CLI fork, add `KrautCLI export --format glb`: per-LOD mesh nodes `<tree>_LOD<n>`, billboard quad node `<tree>_Billboard`, `COLOR_0` wind weights, PBR materials + textures, `asset.extras.kraut` (lod_thresholds, billboard atlas grid + facing mode, seed, descriptor) — track as an openspec change in that repo
- [x] 1.2 Lock the billboard atlas layout (rows x cols, angle-to-cell mapping) and record it in the fork's docs
- [x] 1.3 Verify round-trip: generate from a checked-in descriptor with a fixed seed twice -> byte-identical glb

## 2. Vendor and build the toolchain

- [x] 2.1 Add `engine/ThirdParty/Kraut` submodule (https://github.com/sindney/Kraut-CLI.git, pinned commit), `.gitmodules` entry, missing-submodule FATAL_ERROR guard
- [x] 2.2 CMake: `option(FURY_WITH_KRAUT ON)` + `option(FURY_WITH_KRAUT_PREVIEW ON)`; ExternalProject building KrautCLI (+KrautPreview when enabled) with `-DKRAUT_BUILD_EDITOR=OFF`; copy binaries next to `fury`/`furye` POST_BUILD (FBX2glTF pattern)
- [~] 2.3 Verify macOS + Windows builds: full, and `-DFURY_WITH_KRAUT_PREVIEW=OFF` without SDL2 — macOS verified both configs; Windows verification pending (no Windows machine in this session)

## 3. Mesh and material data model

- [x] 3.1 Add optional vec4 `Colors` channel to `Mesh` (`vertex_color` attribute name registered with the other attribs in Mesh.cpp), serialized with the mesh
- [x] 3.2 Add `m_LodBillboardFlags` (parallel to `m_LodMeshes`) + `IsLodBillboard(i)` + validation in `SetLodMeshes` + serialization in the `lod_meshes` sub-object
- [x] 3.3 Add serialized `Material::m_TwoSided` and `m_WindEnabled` flags + editor checkboxes
- [x] 3.4 GltfImporter: read `COLOR_0` into `Mesh::Colors`

## 4. Kraut import

- [x] 4.1 `KrautImportPostprocess`: read `extras.kraut`, apply `lod_thresholds` to the auto-grouped LodGroup chain, attach `<tree>_Billboard` as flagged terminal tier, set TwoSided/WindEnabled on foliage materials
- [x] 4.2 `fury kraut import <tree.glb>` CLI path saving a fury scene fragment; fallback to synthesized thresholds when extras absent
- [x] 4.3 Round-trip test asset: import a sample glb, save `.bin`, reload, verify chain/thresholds/flags/materials intact

## 5. Vegetation shading

- [x] 5.1 `TWO_SIDED` define: per-material cull-off at draw submission + `gl_FrontFacing` normal flip in gbuffer shaders
- [x] 5.2 `ALPHA_TEST` variants of `DrawDepth.glsl` and `DrawDepthCube.glsl` (diffuse texture + `u_alpha_cutoff` discard); shadow-pass variant selection for MASK materials
- [x] 5.3 `WIND` vertex-shader variant (gbuffer + depth): sway from `vertex_color` weights + `u_time`/`u_wind_params`, pre-projection
- [x] 5.4 `BILLBOARD` shader variant: cylindrical camera-facing quad in VS, atlas cell by view angle, alpha-test + two-sided; plain-mesh path first
- [x] 5.5 Shadow-pass LOD policy: draw deepest non-billboard tier instead of LOD 0 for chained meshes

## 6. Instanced static mesh (ISM/HISM)

- [x] 6.1 `InstancedMeshRender` component: shared mesh + materials + instance TRS list, serialization (JSON + .bin), `cast_shadows` flag
- [x] 6.2 Instance stream, two paths: SSBO read by `gl_InstanceID` on GL 4.3+ (preferred, untested — validate on Windows GL 4.3+) with divisor-VBO (`glVertexAttribDivisor` mat4) fallback for GL 3.3/4.1; runtime capability check picks the path; `INSTANCED` (+ `INSTANCE_SSBO`) defines in gbuffer + depth shaders selecting instance matrix over `u_world`
- [x] 6.3 `RenderQuery` gains an instanced-units list; `PrelightPipeline` submits one `glDrawElementsInstanced` per (pass, LOD tier); non-instanced path untouched
- [x] 6.4 Per-instance frustum culling from cached world AABBs + coarse octree entry; HISM per-instance LOD bucketing (shared coverage math with `MeshRender`), ISM single-tier mode
- [x] 6.5 Instanced shadow casting in all four shadow passes (deepest non-billboard tier)
- [x] 6.6 Billboard bucket in the instanced path (BILLBOARD + INSTANCED combined variant)
- [x] 6.7 Editor inspector: mesh/material slots, ISM/HISM toggle, instance editing, seeded scatter-over-terrain helper

## 7. CLI subcommand

- [x] 7.1 `fury kraut generate <desc.tree> [--seed N] [--out dir]`: KrautCLI subprocess (generate + export glb), KrautPreview `--screenshot` previews per LOD tier; exit codes 0/1/2
- [x] 7.2 Subcommand registration + `kTopHelp`/docs/CLI.md updates

## 8. Sample assets and scene

- [x] 8.1 Check in 2+ `.tree` descriptors (broadleaf + palm/conifer) under `examples/Resource/Trees/Descriptors/`
- [x] 8.2 `tools/gen_tree_assets.py`: descriptor -> KrautCLI -> glb -> `fury kraut import` validation -> committed assets + `trees.json` index
- [x] 8.3 Populate `ocean_island.bin` with InstancedMeshRender tree components on the island (seeded scatter)

## 9. Validation

- [x] 9.1 Headless visual verification with `fury` (not furye) + fixed frame render: LOD tiers transition with distance (LOD debug colors), billboards face the camera, alpha-cut leaves + leaf-shaped shadows, wind sway over time, instanced forest draws
- [x] 9.2 Check shader compiles for every new variant (STATIC/SKINNED x ALPHA_TEST x WIND x INSTANCED x BILLBOARD in gbuffer + depth) before trusting renders
- [x] 9.3 Perf sanity: ocean_island with ~2000 instanced trees holds interactive frame rate; draw-call count flat vs instance count (1 draw per tier per pass)
- [~] 9.4 SSBO path validation on a GL 4.3+ (Windows) machine: identical output vs the divisor-VBO fallback (screenshot diff on the same camera); mark the path tested or keep the untested caveat — UNTESTED this session (no GL 4.3 machine); the untested caveat stays (docs/VEGETATION.md) and `FURY_INSTANCE_SSBO=0` forces the validated divisor path
- [x] 9.5 Save screenshots to `screenshots/`; update docs (CLI.md, a vegetation page)

## 10. Follow-ups (in progress — greenlit 2026-09-01)

Drafted from the 2026-09-01 editor review of ocean_island (ToD 9.62h vs 14.77h
screenshots): LOD tiers shade inconsistently across distance (light/dark patches
shift with sun angle), and tier transitions pop.

- [x] 10.1 Diagnose LOD shading mismatch: at several times of day, compare a tree at LOD0 vs deep mesh tier vs billboard tier at the SAME world position; isolate which tier class mismatches. Suspects: billboard camera-facing normal (flat lighting, dark when sun is behind the camera), generator-simplified normals on deep tiers, alpha-cutoff differences, wind on/off per tier. Record findings on this task before touching shaders

  **Findings (2026-09-01)** — method: `tree_preview.lua` gained `FURY_FORCE_LOD` /
  `FURY_SUN_PITCH` / `FURY_SUN_YAW` / `FURY_WIND_STRENGTH` env knobs; PalmTree2 at a
  fixed 1200 cm camera, forced tiers {0, 3 (deepest mesh), 4 (billboard)}, wind
  strength 0, 2 sun configs (A = high/front, B = low/side). Mean luminance of the
  tree crop + `screenshots/diag_sheet.png`:

  1. **Billboard tier is the dominant mismatch** (A: 2.4 vs LOD0's 25.7 = 9%;
     B: 31.5 vs 38.9 = 81%). `VegetationBillboard` sets the quad's shading normal
     to the horizontal camera-facing vector, so N·L collapses under overhead sun
     and the whole quad shades flat near-ambient. ~9x brightness swing with ToD =
     the reported "patches shift with sun angle". -> fix in 10.3.
  2. **Deep mesh tiers shade darker/flatter than LOD0** (A: t3 = 55% of t0;
     B: 89%). Generator-merged frond cards lose the up-facing normal spread.
     Secondary; shrink naturally once finding 4 lands shallower tiers.
  3. **The billboard atlas has baked-in lighting** (KrautPreview bakes soft
     top-down shading into the texture; pale trunk vs mesh). Engine N·L applies on
     top = double lighting; acceptable at terminal-tier distances after 10.3.
  4. **LOD selection is off by one vs documented intent (engine-wide,
     pre-existing)**: `PickLodForCoverage` picks the first tier with
     `coverage >= threshold(i)` walking shallow->deep, with `threshold(0)`
     hardcoded 1.0 — so tier 0 only owns the coverage=1.0 clamp point and every
     realized boundary lands one tier deeper than `Mesh.h`'s contract ("LOD 1's
     threshold is the coverage below which LOD 1 becomes active"; "LOD 0 is
     active whenever the model is on-screen"). For PalmTree2 (thresholds
     [1, 0.69, 0.37, 0.25]) kraut LOD1 is unreachable dead content; at 12 m the
     tree renders LOD2 where kraut intended LOD1 (verified: baseline run picked
     tier 2/4). The billboard also appears much closer than authored. Amplifies
     finding 2 everywhere. -> fix lands with 10.2 (same function; the transition
     band builds on the corrected semantics). Terrain/glTF synthesized ramps
     audited: both produce t1 ~ 1-1/N, sensible under the corrected walk.
  5. Wind (pinned at strength 0) and alpha-cutoff (both paths 0.5) ruled out.
- [x] 10.2 Dithered LOD transitions: replace the hard coverage threshold with a transition band. Default approach: stochastic per-instance tier jitter in HISM bucketing (hash(instance) perturbs coverage inside the band so neighbors swap tiers at different distances — no double draws). Alternative if jitter reads as noisy: screen-door dither discard in a LOD_DITHER gshader variant drawing both tiers across the band (2x cost inside the band only). Validate: camera dolly across a boundary at fixed time — no visible pop in a frame series

  **Done (2026-09-01)** — two parts:
  1. **Off-by-one fix** (10.1 finding 4): `PickLodForCoverage` now walks
     deep->shallow promoting `picked = i-1` per met threshold — tier 0 owns
     `[t1, 1.0]` per the Mesh.h contract. Kraut LOD1 is reachable again;
     terrain's authored thresholds now behave as their comment intended.
  2. **Jitter band**: `MeshRender::ComputeLodJitter(worldPos)` — FNV-1a hash of
     the cm-quantized position -> stable per-object coverage multiplier in
     [0.85, 1.15]; applied in `MeshRender::UpdateActiveLod` (owner position)
     and per instance in HISM bucketing (instance world position).
     `FURY_LOD_JITTER=0` disables for A/B. ISM aggregate mode stays
     hard-threshold (one tier for the whole component by design).

  Validation (`tests/lua/lod_transition_check.lua`, line of 13 identical palms
  at identical view-z, batch readback via new Lua `GetBatchCount/GetBatchInfo`):
  - tier2/tier3 cliff: jitter OFF flips all 9-11 visible instances between
    d=3200 and d=3300; ON smears individual swaps across d=3000..3600.
  - mesh/billboard cliff: OFF flips all 13 between d=4800 and d=5000; ON
    smears across d=4200..5400 (e.g. d=4600: 9 mesh + 4 billboard).
  - dolly frame series across both cliffs: max/median frame-diff spike 1.1-1.2x
    with jitter ON (no pop).
  - debug-tint speckle screenshot: `screenshots/lod_jitter_speckle.png`.

  **Macro formalization (same sitting, user-directed)**: the LOD debug tint
  was `#if WITH_EDITOR`-gated (headless could not use it). Moved the palette
  to `Pipeline.cpp` and the gate to the new `WITH_DBG_OVERLAY` macro
  (default ON incl. headless; forced OFF by the new `FURY_SHIPPING=ON`
  packaging option, which also defines `FURY_BUILD_SHIPPING` alongside the
  existing FURY_BUILD_DEBUG/PROFILE/RELEASE markers). Shader-side define
  injected in `Shader::Compile` like `WITH_EDITOR`. Verified: headless tint
  works; Shipping build renders pixel-identical output with the switch on or
  off. Convention documented in `docs/BUILD_FLAGS.md` for future overlays.
- [x] 10.3 Billboard lighting consistency: bias billboard shading normals away from camera-facing (e.g. up-biased or away-from-camera-hemisphere normal) so billboards track the mesh tiers' sun response at any ToD; re-run the 10.1 comparison to close the loop

  **Done (2026-09-04)** — `VegetationBillboard` shading normal is now
  `normalize(mix(fwd, up, u_billboard_up_bias))` with a tuned default of
  **0.28** (uniform on the billboard material, set by the kraut importer;
  per-material overridable). Swept candidates empirically on PalmTree2 at a
  fixed camera (billboard luminance vs the deepest mesh tier):

  | normal | noon-overhead | low front-axial | side 45° |
  |---|---|---|---|
  | camera-facing (old) | **17%** | 91% | 22% |
  | pure up | 285% | 7% | 139% |
  | dome/spheroid proxies | 161-277% | 9-25% | 232%+ |
  | **mix(fwd, up, 0.28)** | **177%** | **88%** | **84%** |

  Findings from the sweep: canopy-dome and spheroid normal fields fail
  because the visible (non-discarded) billboard pixels are all crown --
  tilting them up reproduces the pure-up overshoot; and no fixed normal
  can match a two-sided canopy at perpendicular side sun (a quad has no
  side leaves) -- the -50% residual at 90°-side sun is inherent and
  acceptable (billboards sit at 45m+; at low sun the whole scene trends
  to ambient). Closed the loop with the 10.1 comparison, billboard vs
  LOD0: 97% / 79% / 69% across the three suns (was 9% / 81% / n.a.).
  Assets re-baked via `tools/gen_tree_assets.py` so the serialized
  billboard materials carry the uniform (older .bin scenes fall back to
  the shader initializer, same 0.28). Contact sheet:
  `screenshots/diag_billboard_normalbias.png`.

- [x] 10.4 Grass asset gen (offline, NOT Kraut — too heavy for ground cover): procedural python baker emitting a small grass-clump mesh set with the kraut vertex-color wind encoding (sway 0 at root, 1 at blade tip, per-blade phase) + a gradient/alpha texture; LOD chain: full clump -> reduced card count -> single billboard card (reuse the billboard shader path; no Kraut atlas needed). Ship under examples/Resource/Trees/Grass*/ with the gen script committed

  **Done (2026-09-04)** — `tools/gen_grass_assets.py` bakes
  `examples/Resource/Trees/Grass/`: blade-silhouette textures (PIL, tapered
  bezier ribbons, root->tip gradient), a hand-rolled `GrassClump.glb`
  speaking the kraut contract (`_LOD0` 3 crossed cards / `_LOD1` 2 cards /
  `_Billboard` 1 quad, COLOR_0 = kraut wind packing, `extras.kraut` with
  lod_thresholds [8m -> LOD1, 18m -> billboard] + 1x1 atlas block), then
  imports through the stock `fury kraut import` path -> `GrassClump.bin`
  (thresholds/billboard flag/MATERIAL flags all via the existing
  postprocess). Vertex normals are all +Y so grass lights like terrain.
  Verified: chain 3 tiers, forced-tier renders at 3m (LOD0), 15m (LOD1),
  billboard tier renders the dense atlas, wind sway animates.

- [x] 10.5 Grass rendering + scatter: reuse InstancedMeshRender (ISM/HISM) as-is; grass materials = MASK + two-sided + wind; rolling wind WAVES need position-based phase (world pos -> phase), not just per-branch phase — extend the WIND variant with a position-phase term (matches the shadertoy dd2cWh field look). Scatter helper: dense mode (10k+ instances) — measure per-instance cull cost at that scale; if hot, add coarse cell batching (group instances into per-cell clusters culled as units)

  **Done (2026-09-04)** —
  - Wind: `VegetationWindOffset` phase is now `per-branch jitter + time +
    dot(worldPos.xz, windDir) * 0.00785` (~800 m wavelength -> rolling
    fronts across a field; trees inherit a gentle coherent sway gradient,
    shadows sway in sync via the shared depth variants).
  - Perf (`tests/lua/grass_field_perf.lua`, HISM, shadows off):
    3k = 3.1ms, 12k = 9.1ms, 48k = 36ms avg frame; **draw calls flat (6)
    at every scale**. Total pipeline cost ~0.7us/instance/frame ->
    per-instance cull is NOT hot at the 10k island target; cell batching
    deferred (note: beyond ~50k instances the CPU cull dominates -- add
    per-cell cluster culling then).
  - Trap recorded: headless `fury` without `--screenshot-frame` runs an
    open-ended loop (test scripts must `os.exit(0)` when done).
- [x] 10.6 Populate ocean_island grass layer (seeded, terrain-height + splat-aware so grass avoids beach/rock), then validate against the reference field look (dense + wind waves): screenshots at 2 distances + 2 ToD, draw calls flat, frame rate interactive

  **Done (2026-09-04)** —
  - `tools/gen_island_grass.py`: reads `TerrainIsland/height.r16` (513^2,
    1km x 1km, 4000cm scale) + `splat.png` (R=grass dominant, slope < 0.6,
    > 30cm above waterline) -> `grass_points.lua` (25k seeded points).
    Also copies the two grass PNGs next to the scene (the island's
    bare-sibling texture convention -- the Engine/-prefix retarget only
    fixes the entity-level path; material blocks embed their own copy).
  - `tests/lua/populate_island_grass.lua`: merges GrassClump.bin into the
    island via Importer.MergeInto, removes the template node, bakes a
    `GrassField` InstancedMeshRender (25k instances, cast_shadows off),
    saves to a caller-named file (reviewed, then promoted over
    ocean_island.bin; backup at /tmp/ocean_island_backup.bin).
  - Validation (`tests/lua/grass_island_check.lua`, env FURY_SCENE /
    FURY_CAM / FURY_TOD_HOURS / FURY_WIND_TIME): 70fps avg, 123 draw calls
    (flat vs 25k instances), HISM buckets visible (t0=34 t1=2 from the
    vista camera). Screenshots: `screenshots/island_grass_{vista,close}
    [_tod2].png`.
  - **ToD control trap**: the island's SkyAtmosphere component (node "Sky",
    `sun_from_tod: true`, `sun_light_name: "DefaultSun") overwrites the
    sun node's rotation every frame -- direct node rotation/intensity
    edits are inert. Set `SkyAtmosphere:SetTimeHours()` instead (Lua-bound,
    `SetAutoAdvance(false)` to pin). Headless: `FURY_TOD_HOURS=17.5`
    produces proper long-shadow evening light.
  - **Finding (new follow-up 10.10)**: deferred CSM shadow-receive appears
    inert on the PBR pipeline -- grass + palm trunks inside canopy shadow
    pools stay lit while the terrain (own CSM sampling in DrawTerrain)
    darkens. SunLight.glsl has the CSM samplers; needs a receive-path
    audit (m_LastShadowTextures wiring vs the PBR pass order).

- [x] 10.10 Deferred CSM shadow audit (was: "meshes render lit inside canopy shadows that darken the terrain"): **root cause = the 5.5 shadow-LOD policy applied to terrain chunks** -- the shadow pass drew the coarsest terrain LOD (8x-decimated grid, meters off the rendered surface), so open slopes self-shadowed in blotch patches (the user's "dark areas" screenshot) and those false pools made correctly-lit grass look unshadowed by contrast. Fix: `PickShadowLodMesh` (and `InstancedMeshRender::GetShadowLodTier`) demote past LOD 0 only for billboard-terminated chains (kraut trees); plain chains (terrain, meshopt props) cast from LOD 0 again. Verified on the island: blotches gone, frond canopy shadows intact, grass blades correctly darken inside them.

- [x] 10.11 Trees too bright at ToD 15-17 (user report: island canopy reads flat/pale and too bright in the afternoon)

  **Done (2026-09-04)** — root cause: the TWO_SIDED `gl_FrontFacing` normal
  flip. Flipping every visible leaf's normal toward the camera makes the
  whole canopy catch a low sun uniformly (the camera-facing hemisphere
  ~= the sun-facing hemisphere when the sun is behind the viewer), killing
  underside shading -- the canopy reads as flat pale wash. (Ruled out:
  sky/sun intensity -- SkyAtmosphere already scales both by elevation;
  material ambient_color -- absent, defaults to 0; CSM bias -- rebalance
  made no visible difference.) Fix, part 1: the gbuffer TWO_SIDED variant
  no longer flips (cull stays off per material).
  Fix, part 2 (landed in the round-3 review): removing the flip alone
  exposed that kraut leaf-card normals are HORIZONTAL (perpendicular to the
  hanging fronds; 1% up in PalmTree2) -- the canopy went dark from high sun.
  Final: **canopy-normal bend at import** (KrautImportPostprocess) --
  foliage submesh (MASK+two-sided) normals rewritten to a flattened
  crown-dome proxy `normalize(dir.x, dir.y*0.4 + crownR*0.6, dir.z)` per
  vertex, per tier; trunk/bark keep authored normals; meshes under 2 m
  skipped (grass keeps its all-up normals). The flip stays removed. Result:
  canopy bright AND structured from every sun angle (harness lum ~35 stable
  vs 8.9 dark-raw / 25.7 flat-flip); deep LOD tiers shade like LOD 0 (their
  generator-degraded normals replaced). Existing scenes need the one-off
  height-gated bend migration (ran on ocean_island) or a tree re-import.
  The TWO_SIDED shader define now compiles identical-to-base code (kept in
  the JSONs for plumbing stability -- dropping the flag + the dead JSON
  variants is a cleanup follow-up).
  Enabler fix: Lua `Mesh.Set*` channel setters never marked buffers dirty --
  runtime mesh edits silently never re-uploaded (now channel + mesh-level
  SetDirty; verified by the flat-up test 8.9 -> 37.5 lum).

- [x] 10.12 Dither visibility + grass density/height (user review): dither imperceptible; grass too tall and sparse

  **Done (2026-09-04)** —
  - Dither: the mechanism verified working (wall-rig batch counts: lockstep
    cliff with band 0, smeared buckets with band on). It's subtle BY DESIGN
    (transitions shouldn't draw the eye); the band is now tunable at runtime:
    `FURY_LOD_JITTER=<fraction>` (0 = off, 0.4 = visible speckle demo, default
    0.15). Also note: PalmTree2's t1 sits at the 1.0 clamp, so the tier0/1
    boundary spread is limited by construction -- visible at the mesh->billboard
    boundary instead.
  - Grass look: clump 0.6m -> 0.38m tall, blade texture 26 -> 40 fatter
    blades, instance scale 140-220 -> 90-150 (34-57cm tall), and placement
    switched to **meadow patches** (habitat seeds -> ~10-25m gaussian blobs;
    the grass splat covers ~250k m^2 so uniform density can never read dense).
    45k instances.
  - Perf follow-through: 45k grass pushed the island to 35ms -- the per-frame
    per-instance matrix compose was the hot spot. InstancedMeshRender now
    caches world matrices + AABBs (rebuilt only when instances or the owner
    transform change; memcmp-compared). Island with 45k grass + 2.3k trees:
    **3.6ms (278fps), 123 draws**.
  - Review-round-2 fixes: (a) `populate_island_grass.lua` is now idempotent
    (drops an existing GrassField first) -- a re-run had double-populated the
    scene to 70k instances, which was the "island got slower" report (stale
    pre-cache editor binary made it worse). (b) New serialized
    `InstancedMeshRender::m_CullDistance` (cm, 0 = no cap; editor field +
    Lua binding) -- grass drops out entirely past 60 m instead of paying
    for far billboards. (c) Grass LODs switch nearer: LOD1 at 5 m, billboard
    at 12 m (was 8/18). (d) Placement is 60% meadow blobs + 40% uniform
    lawn baseline (clusters alone left the grove's grass strips bare).
    Final: 3.2-3.3ms avg, 123 draws, grass visible from the grove vantage.
  - Review-round-3 (zoom review via `--screenshot-series` dolly): (a) the
    kill distance popped as a hard line -- the cull is now jittered per
    instance (same position hash as the LOD dither), so the edge is a
    staggered band; cull distance 60 -> 80 m. (b) grass went dark at the
    billboard tier: its shading normal (up_bias 0.28 for tree canopies)
    mismatched the cards' all-up normals -- the importer now reads optional
    `extras.kraut.billboard.up_bias` and grass writes 1.0; tier-consistent.
    (c) tree mesh->billboard pop: the kraut exporter's default terminal
    threshold put billboards at ~25% coverage (~180 px tall trees, texture
    swap visible). Importer caps the billboard tier at 0.12 coverage (~2x
    farther). Verified with `billboard_pop_check.lua` dolly series: old
    assets rebound ink+lum 1.8x at the swap; new assets decline smoothly
    through 128 m. (d) density 45k -> 80k instances. Final island: 5.8-6.3 ms
    avg (160-170 fps), 121 draws.

- [x] 10.14 CR round (2026-09-07): comment hygiene + layout policy

  - Removed history-justification comments (buoyancy debugDraw notes,
    PhysicsWorld's pointer comment, OceanWaves' "compute path" reference,
    FileUtil's TGA story -- the STBI_ONLY_TGA define stands alone);
    `kKrautHelp` dedented to match its sibling blocks; `LuaBindings.h`
    LauncherEngineOptions re-aligned; KrautConverter platform checks now use
    the existing `PLATFORM_MACOS/PLATFORM_WINDOWS/PLATFORM_LINUX` family
    (Macros.h), not raw `__APPLE__`/`_WIN32`.
  - `EditorDebug.{h,cpp}` deleted outright (the LOD palette lives in
    Pipeline.cpp; EditorWindows.cpp's include dropped; CMake delisted).
  - **Inspector width policy** (new `engine/Fury/Editor/EditorUiRow.h`):
    `FieldRow` (label trails the field when the pair fits, wraps above when
    it doesn't), `FieldRowN` (N equal fields sharing a row), `CheckboxRow`,
    `ButtonRow`; hints ride the label as hover tooltips instead of trailing
    text runs. Applied to the offenders: SkyAtmosphere, OceanComponent,
    InstancedMeshRender, BodySetup, BuoyancyComponent. New inspector rows
    should use these helpers instead of raw ImGui widget labels.
  - GltfImporter canopy-bend complexity: O(unique foliage verts) per mesh
    one-time at import (added a visited-dedupe so shared verts aren't
    reworked per triangle corner). PalmTree2 LOD0 ~12k verts = trivial;
    a 1M-tri tree is single-digit ms.

- [x] 10.13 Foliage color tuning (user review: leaves too light/lime): the leaf color comes from the Kraut content textures the descriptors reference (e.g. Palm1.tga, mean rgb (156,186,48)), baked in by the fork's exporter -- not engine-side. **AND the user's sRGB suspicion was right**: the island's tree texture records carried `srgb: false` (stale from an early pipeline iteration; fresh imports are correct, srgb:true on baseColor), so the HDR pipeline sampled gamma-encoded albedo as linear = over-bright/lime. Fixed in place (entity-level AND material-embedded records -- they're separate texture objects; added `Material::SetTextureSRGB`/`GetTextureSRGB` Lua bindings to reach the embedded ones). After the gamma fix the raw kraut lime persists at canopy level (139,163,60), so the tone-map stays as art direction on top: `tools/gen_tree_assets.py` tone-maps foliage textures (material names matching frond/leaf/needle/foliage) AND the billboard atlas by `FOLIAGE_TONE` (0.55, 0.62, 0.85; `FURY_FOLIAGE_TONE="r,g,b"` env overrides). Final island canopy: (80,111,56) -- deep palm green. The island's sibling texture copies refreshed in place.

- [x] 10.7 InstancedMeshRender inspector: replace the raw per-instance row replication (currently capped at 64 rows) with a fixed-height scrolling ImGui list (ImGuiListClipper); show index + compact TRS readout per row, select-to-edit

  **Done (2026-09-04)** — the inspector now renders a fixed-height (10-row)
  clipper list: `idx (x, y, z) yaw° xscale` per row, click to select; the
  selected instance gets position drags, a yaw drag (Y-twist), uniform
  scale, and Remove/Duplicate. Scales to 10k+ instances (only visible rows
  are built). Yaw readout is exact for pure-Y rotations (the scatter case).

- [x] 10.8 HISM debug visualization: per-instance LOD-tier tint in the instanced path (the LOD_DEBUG_COLORS switch currently only tints per-component batches uniformly — extend GetLodDebugColor usage so each instance's bucket tier shows its palette color; consider a viewport overlay mode listing bucket sizes per tier)

  **Done (2026-09-04)** — the instanced path already tints per batch (= per
  tier, so per instance); validated headless via the 10.2 speckle shot
  (mixed cyan/magenta in one row of palms). New: `RenderLodBucketOverlay`
  in the editor viewport (top-left panel, per-tier instance counts in the
  tier palette colors) shown while LOD Debug Colors is on; gated
  WITH_DBG_OVERLAY. The palette itself moved from EditorDebug.cpp to
  Pipeline.cpp (see 10.2's macro note) so headless `fury` can tint too.

- [x] 10.9 Buoyancy debug-draw gating: move the per-component float-point debug draw out of the always-on path into the Debug views dropdown list, behind a global debug option that is OFF by default

  **Done (2026-09-04, revised per review)** — the per-component `debugDraw`
  flag is GONE (inspector checkbox deleted, serialization dropped with the
  legacy key ignored on load, Lua bindings removed). Markers draw solely
  under the new `PipelineSwitch::BUOYANCY_DEBUG` (Debug views dropdown row
  "Buoyancy Float Points", off by default); the DrawDebug call gate checks
  the switch + a non-empty buoyancy list so markerless scenes never pay
  for the debug pass. `PhysicsWorld::HasBuoyancyDebugDraw` removed.


