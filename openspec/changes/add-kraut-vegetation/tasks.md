# add-kraut-vegetation — tasks

## 1. Fork-side Kraut-CLI export (companion work in the Kraut-CLI repo)

- [ ] 1.1 In the Kraut-CLI fork, add `KrautCLI export --format glb`: per-LOD mesh nodes `<tree>_LOD<n>`, billboard quad node `<tree>_Billboard`, `COLOR_0` wind weights, PBR materials + textures, `asset.extras.kraut` (lod_thresholds, billboard atlas grid + facing mode, seed, descriptor) — track as an openspec change in that repo
- [ ] 1.2 Lock the billboard atlas layout (rows x cols, angle-to-cell mapping) and record it in the fork's docs
- [ ] 1.3 Verify round-trip: generate from a checked-in descriptor with a fixed seed twice -> byte-identical glb

## 2. Vendor and build the toolchain

- [ ] 2.1 Add `engine/ThirdParty/Kraut` submodule (https://github.com/sindney/Kraut-CLI.git, pinned commit), `.gitmodules` entry, missing-submodule FATAL_ERROR guard
- [ ] 2.2 CMake: `option(FURY_WITH_KRAUT ON)` + `option(FURY_WITH_KRAUT_PREVIEW ON)`; ExternalProject building KrautCLI (+KrautPreview when enabled) with `-DKRAUT_BUILD_EDITOR=OFF`; copy binaries next to `fury`/`furye` POST_BUILD (FBX2glTF pattern)
- [ ] 2.3 Verify macOS + Windows builds: full, and `-DFURY_WITH_KRAUT_PREVIEW=OFF` without SDL2

## 3. Mesh and material data model

- [ ] 3.1 Add optional vec4 `Colors` channel to `Mesh` (`vertex_color` attribute name registered with the other attribs in Mesh.cpp), serialized with the mesh
- [ ] 3.2 Add `m_LodBillboardFlags` (parallel to `m_LodMeshes`) + `IsLodBillboard(i)` + validation in `SetLodMeshes` + serialization in the `lod_meshes` sub-object
- [ ] 3.3 Add serialized `Material::m_TwoSided` and `m_WindEnabled` flags + editor checkboxes
- [ ] 3.4 GltfImporter: read `COLOR_0` into `Mesh::Colors`

## 4. Kraut import

- [ ] 4.1 `KrautImportPostprocess`: read `extras.kraut`, apply `lod_thresholds` to the auto-grouped LodGroup chain, attach `<tree>_Billboard` as flagged terminal tier, set TwoSided/WindEnabled on foliage materials
- [ ] 4.2 `fury kraut import <tree.glb>` CLI path saving a fury scene fragment; fallback to synthesized thresholds when extras absent
- [ ] 4.3 Round-trip test asset: import a sample glb, save `.bin`, reload, verify chain/thresholds/flags/materials intact

## 5. Vegetation shading

- [ ] 5.1 `TWO_SIDED` define: per-material cull-off at draw submission + `gl_FrontFacing` normal flip in gbuffer shaders
- [ ] 5.2 `ALPHA_TEST` variants of `DrawDepth.glsl` and `DrawDepthCube.glsl` (diffuse texture + `u_alpha_cutoff` discard); shadow-pass variant selection for MASK materials
- [ ] 5.3 `WIND` vertex-shader variant (gbuffer + depth): sway from `vertex_color` weights + `u_time`/`u_wind_params`, pre-projection
- [ ] 5.4 `BILLBOARD` shader variant: cylindrical camera-facing quad in VS, atlas cell by view angle, alpha-test + two-sided; plain-mesh path first
- [ ] 5.5 Shadow-pass LOD policy: draw deepest non-billboard tier instead of LOD 0 for chained meshes

## 6. Instanced static mesh (ISM/HISM)

- [ ] 6.1 `InstancedMeshRender` component: shared mesh + materials + instance TRS list, serialization (JSON + .bin), `cast_shadows` flag
- [ ] 6.2 Instance stream, two paths: SSBO read by `gl_InstanceID` on GL 4.3+ (preferred, untested — validate on Windows GL 4.3+) with divisor-VBO (`glVertexAttribDivisor` mat4) fallback for GL 3.3/4.1; runtime capability check picks the path; `INSTANCED` (+ `INSTANCE_SSBO`) defines in gbuffer + depth shaders selecting instance matrix over `u_world`
- [ ] 6.3 `RenderQuery` gains an instanced-units list; `PrelightPipeline` submits one `glDrawElementsInstanced` per (pass, LOD tier); non-instanced path untouched
- [ ] 6.4 Per-instance frustum culling from cached world AABBs + coarse octree entry; HISM per-instance LOD bucketing (shared coverage math with `MeshRender`), ISM single-tier mode
- [ ] 6.5 Instanced shadow casting in all four shadow passes (deepest non-billboard tier)
- [ ] 6.6 Billboard bucket in the instanced path (BILLBOARD + INSTANCED combined variant)
- [ ] 6.7 Editor inspector: mesh/material slots, ISM/HISM toggle, instance editing, seeded scatter-over-terrain helper

## 7. CLI subcommand

- [ ] 7.1 `fury kraut generate <desc.tree> [--seed N] [--out dir]`: KrautCLI subprocess (generate + export glb), KrautPreview `--screenshot` previews per LOD tier; exit codes 0/1/2
- [ ] 7.2 Subcommand registration + `kTopHelp`/docs/CLI.md updates

## 8. Sample assets and scene

- [ ] 8.1 Check in 2+ `.tree` descriptors (broadleaf + palm/conifer) under `examples/Resource/Trees/Descriptors/`
- [ ] 8.2 `tools/gen_tree_assets.py`: descriptor -> KrautCLI -> glb -> `fury kraut import` validation -> committed assets + `trees.json` index
- [ ] 8.3 Populate `ocean_island.bin` with InstancedMeshRender tree components on the island (seeded scatter)

## 9. Validation

- [ ] 9.1 Headless visual verification with `fury` (not furye) + fixed frame render: LOD tiers transition with distance (LOD debug colors), billboards face the camera, alpha-cut leaves + leaf-shaped shadows, wind sway over time, instanced forest draws
- [ ] 9.2 Check shader compiles for every new variant (STATIC/SKINNED x ALPHA_TEST x WIND x INSTANCED x BILLBOARD in gbuffer + depth) before trusting renders
- [ ] 9.3 Perf sanity: ocean_island with ~2000 instanced trees holds interactive frame rate; draw-call count flat vs instance count (1 draw per tier per pass)
- [ ] 9.4 SSBO path validation on a GL 4.3+ (Windows) machine: identical output vs the divisor-VBO fallback (screenshot diff on the same camera); mark the path tested or keep the untested caveat
- [ ] 9.5 Save screenshots to `screenshots/`; update docs (CLI.md, a vegetation page)
