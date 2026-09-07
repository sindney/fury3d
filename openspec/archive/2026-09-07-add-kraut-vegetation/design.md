# add-kraut-vegetation — design

## Context

Vegetation is greenfield for this engine. Code reconnaissance established:

- **No GPU instancing anywhere** — `glDrawElementsInstanced`/divisors/SSBOs exist only in the GLLoader; zero call sites. Every draw is one `glDrawElements` per (mesh, pass).
- **LOD is fully supported**: `Mesh::m_LodMeshes` + screen-coverage thresholds, `MeshRender::UpdateActiveLod` picks per visible instance, `GltfImporter` already auto-groups `<base>_LOD<n>` mesh names into a LodGroup, meshopt-based generation exists, LOD debug colors exist.
- **Alpha-cutout exists**: glTF MASK mode sets the ALPHA_TEST shader variant in the gbuffer path. But shadow depth shaders (`DrawDepth.glsl`, `DrawDepthCube.glsl`) have **no discard** — leaf cards would cast solid-quad shadows.
- **No billboard/impostor path** — particles are CPU-billboarded into a per-emitter dynamic mesh.
- **No vertex-color channel** — `Mesh` has no Colors buffer; GltfImporter skips `COLOR_0`. Kraut stores wind weights in vertex colors.
- **No two-sided lighting** — cull mode is per-pass; no backface normal flip in any shader.
- **No wind** — the only vertex animation precedent is ocean displacement, which is ocean-pass-specific.
- **Shadow passes ignore LOD** — always draw LOD 0 (`Pipeline.cpp:632/732/838/938`).
- The "plugin" surface is the `fury` CLI subcommand table (`Cli.cpp`); `FbxConverter` (subprocess wrapper around the vendored FBX2glTF binary, copied next to the executables POST_BUILD) is the direct precedent for wrapping an external converter.
- Built-in sample assets live in `examples/Resource/` (`"Engine/"`-prefixed paths resolve there) and are baked by repo-root `tools/gen_*.py` scripts (ocean, terrain).
- GL floor is 3.3 core (macOS caps at 4.1) — SSBOs (4.3) are NOT guaranteed; attribute-divisor instancing is the safe instancing mechanism.

Kraut-CLI (user's fork of jankrassnigg/Kraut) provides a headless agent-controllable toolchain: `KrautCLI` (info/generate/export/roundtrip on `.tree` descriptors, JSON output, deterministic per descriptor+seed) and `KrautPreview` (SDL2+ImGui+GL3.3 viewer with LOD selector and `--screenshot`). It computes up to 5 LOD meshes with Impostor/Billboard modes. Export today: OBJ + `.kraut` v2 — neither carries LOD metadata, billboard atlases, or vertex colors.

## Goals / Non-Goals

**Goals:**
- On-demand, deterministic tree generation: agent runs one command from `.tree` descriptor + seed to a previewed, engine-importable asset.
- Trees render correctly in fury: full LOD chain, alpha-cut foliage with leaf-shaped shadows, two-sided lighting, wind sway, camera-facing billboard terminal tier.
- Large vegetation sets render efficiently via ISM/HISM-style instanced draws with per-instance culling and instanced shadow casting.
- Sample tree assets shipped as engine resources (like ocean/sky), and `ocean_island.bin` populated as the reference scene.

**Non-Goals:**
- The Qt Kraut editor (extra deps, not needed for the headless pipeline).
- Runtime/procedural tree generation inside the engine (generation is offline via KrautCLI).
- GPU compute culling / indirect draws (requires GL 4.3+; deferred).
- Impostor atlas *re-rendering* at runtime (billboards use the atlas baked by Kraut).
- Grass/ground-cover scattering systems, terrain-aware auto-placement (manual placement in v1).

## Decisions

### D1: Vendor Kraut-CLI as a submodule; build via ExternalProject behind an option
`engine/ThirdParty/Kraut` -> https://github.com/sindney/Kraut-CLI.git, pinned to a stable commit. Engine CMake gains `option(FURY_WITH_KRAUT ... ON)` driving an `ExternalProject_Add` that builds **KrautCLI + KrautPreview only** (`-DKRAUT_BUILD_EDITOR=OFF`), with the two binaries copied next to `fury`/`furye` POST_BUILD — the FBX2glTF binary-distribution pattern. Rationale: Kraut's CMake is a standalone superbuild (Tools/ glob-discovered, Output/Bin layout); `add_subdirectory` would force its targets/options into our graph. Alternative considered: prebuilt binaries checked in like FBX2glTF — rejected, we want source-level control of the fork (D2 needs changes in it).

Risk containment: if SDL2 (preview dependency) is unavailable on a machine, a sub-option `FURY_WITH_KRAUT_PREVIEW` (default ON) lets the CLI build alone.

### D2: Bridge format is glTF-binary with kraut extras — NOT a new binary parser in fury
The Kraut-CLI fork gains `KrautCLI export --format glb` producing:
- one node per LOD mesh named `<tree>_LOD<n>` (0 = full detail), reusing GltfImporter's existing LodGroup auto-grouping;
- a billboard quad node named `<tree>_Billboard` with the atlas texture;
- `COLOR_0` vertex colors carrying wind weights;
- PBR materials (baseColor+alpha MASK, normal, metallic-roughness) and textures;
- `asset.extras.kraut = { lod_thresholds: [...], billboard: { atlas_cols, atlas_rows, mode }, wind: {...}, seed, descriptor }`.

Fury side imports through the existing `GltfImporter` + a `KrautImportPostprocess` that reads extras: applies `lod_thresholds` to the auto-grouped chain, flags the billboard tier (D5), and sets foliage material flags (D6). Alternatives rejected: (a) parse `.kraut` v2 binary in fury — version churn (v14-v18), big parser, duplicates Kraut's own reader; (b) OBJ + sidecar manifest — OBJ has no vertex colors (wind weights lost), no materials worth speaking of, no extras channel. glTF reuses the engine's most-tested import path and keeps the contract in one file.

### D3: `fury kraut` CLI subcommand wraps the whole flow
`fury kraut generate <descriptor.tree> [--seed N] [--out dir]` -> subprocess KrautCLI generate + export glb + KrautPreview `--screenshot` for a visual check (FbxConverter subprocess precedent); `fury kraut import <tree.glb> [scene]` -> loads the glb through the kraut-aware importer and saves a fury scene fragment. Help text in `kTopHelp`, docs in `docs/CLI.md`, spec coverage in the `cli` capability style. This is the agent on-demand entry point.

### D4: Instancing via `InstancedMeshRender` component; SSBO instance stream preferred, attribute-divisor VBO fallback
New component `InstancedMeshRender` (serialized, editor-visible): one `Mesh` (with LOD chain) + materials + a vector of instance transforms (TRS, static). Per frame per pass:
1. CPU frustum-cull per instance against cached per-instance world AABBs (octree bypassed for instances; the component itself holds one octree entry covering all instances for coarse culling).
2. HISM mode (default): bucket visible instances by LOD tier using the same screen-coverage rule as `MeshRender`, evaluated per instance; ISM mode: single LOD picked from the component's aggregate bounds.
3. Stream visible instance matrices to the GPU and draw `glDrawElementsInstanced` once per (pass, LOD tier), using one of two instance-data paths selected by runtime GL capability:
   - **SSBO path (preferred, UNTESTED)**: when the context is GL 4.3+ (checked once via the existing `gl::HasComputeShaders()`-style capability query), instance matrices go into an SSBO read in the VS by `gl_InstanceID`. Scales better and keeps attribute layout untouched.
   - **Divisor-VBO path (fallback)**: on GL 3.3/4.1 (macOS), matrices stream through a dynamic VBO bound with `glVertexAttribDivisor` (mat4 = 4 vec4 attributes past the mesh attribs).

The SSBO path is the preferred implementation per project direction but is **not tested on any target yet** — the divisor path is the validated baseline (macOS can only run it), and both paths must produce identical output. Shaders gain an `INSTANCED` define (plus `INSTANCE_SSBO` selecting the read mechanism) where the world matrix comes from instance data instead of the `u_world` uniform — applied to the gbuffer static-mesh shader and the depth shaders (shadow pass draws instanced casters the same way, using each shadow pass's existing light matrix).

Per-instance data beyond the transform (e.g. per-instance tint variation) is out of scope for v1.

### D5: Billboard as a flagged terminal LOD tier, GPU-faced
`Mesh` gains a parallel `m_LodBillboardFlags` (serialized with the `lod_meshes` sub-object). A flagged tier's mesh is the Kraut billboard quad and is drawn with a `BILLBOARD` shader variant: VS builds a cylindrical camera-facing quad from the instance/node position (billboards stay upright for trees), and the FS selects the atlas cell by view angle around the trunk axis (atlas dimensions from kraut extras). In the instanced path, the billboard bucket draws with the same instanced stream (D4). Billboards are skipped in shadow passes (terminal tier = far away; the deepest non-billboard LOD casts instead). Alternative rejected: CPU-billboarding like particles — defeats instancing, burns main-thread CPU for thousands of trees.

LOD thresholds come from kraut extras (artist-tuned in Kraut), falling back to the importer's synthesized linear thresholds.

### D6: Foliage shading = MASK + two-sided + wind defines on existing shader variants
- **Two-sided**: `Material` gains `m_TwoSided` (serialized, editor checkbox); when set, the draw sets cull OFF for that unit and the shader gets a `TWO_SIDED` define that flips the normal for back faces (`gl_FrontFacing`). Per-material flag chosen over per-pass cull config — foliage two-sidedness is a material property, and pipeline JSON stays untouched.
- **Leaf-shaped shadows**: depth shaders gain an `ALPHA_TEST` variant (diffuse texture + `u_alpha_cutoff` discard), selected for MASK materials exactly like the gbuffer path does. Applies to both 2D (dir/spot/CSM) and cube (point) depth shaders.
- **Wind**: `WIND` define on the gbuffer + depth VS: sway offset computed from `vertex_color` channels (Kraut wind weights) + `u_time`/`u_wind_params` uniforms, applied pre-projection so shadows sway too. Ocean displacement stays its own thing; this is the general static-mesh hook.
- **Vertex colors**: `Mesh` gains a `Colors` vec4 channel (`vertex_color` attribute), GltfImporter reads `COLOR_0`.

### D7: Sample assets baked by `tools/gen_tree_assets.py`, committed under `examples/Resource/Trees/`
Follows the `gen_ocean_assets.py` pattern: stdlib-python driver invoking KrautCLI (from `engine/ThirdParty/Kraut` build output or a system override) on checked-in `.tree` descriptors, exporting glb + textures into `examples/Resource/Trees/`, plus a `trees.json` index. Assets are committed so the engine samples work without building Kraut. `ocean_island.bin` gets palm/broadleaf InstancedMeshRender components scattered on the island (placement via a small seedable scatter helper in the generator script or editor manual placement).

### D8: Content browser and editor surface
Tree assets surface as existing Mesh/Material/Texture tiles (no new asset type in v1). `InstancedMeshRender` gets an inspector panel (instance count, ISM/HISM toggle, add/remove instance, scatter-on-terrain helper button if cheap). LOD debug colors already work for tier validation.

## Risks / Trade-offs

- Fork-side glb export is companion work in the Kraut-CLI repo -> this change's importer tasks are blocked on it; mitigation: the fork already exports per-LOD OBJ, so a stopgap OBJ+manifest importer path can unblock fury-side work, but the glb contract is the target. Track the fork change as a linked openspec change in that repo.
- SDL2 availability for KrautPreview on all dev machines -> `FURY_WITH_KRAUT_PREVIEW=OFF` escape hatch; preview is a nicety, CLI is the requirement.
- CPU per-instance culling + matrix streaming cost at 10k+ instances -> acceptable at target scale (thousands of trees); note GPU culling via compute (engine has optional 4.3 compute) as a documented follow-up.
- The SSBO instance path is preferred but untested on any target (macOS dev machines only exercise the divisor fallback) -> both paths share the cull/bucket logic and must be diffed for identical output on a GL 4.3+ Windows machine before trusting SSBO; validation tasks include a Windows run.
- Alpha-tested shadows double texture binds in the shadow pass and can thrash depth prepass state -> variant selection mirrors the existing gbuffer path; measure with Tracy once that lands.
- Billboard cylindrical vs spherical facing: cylindrical chosen for trees; Kraut impostor mode (octahedral-ish multi-view) is richer but a bigger shader lift — v1 uses the billboard atlas; impostor mode is a follow-up.
- Instanced draw submission touches the hot `DrawUnit` path -> keep instanced units in a separate list on `RenderQuery` so the existing per-mesh path is untouched when no instancing components exist.
- `.bin` scenes embedding thousands of instance transforms inflate the LZ4-JSON format -> acceptable; a binary instance blob is a possible optimization later.

## Migration Plan

Purely additive: new options default ON for the tool build but Kraut submodule absence is guarded like other submodules (clear FATAL_ERROR with instructions); no existing scene/material/mesh format changes except additive serialized fields (vertex colors, billboard flags, TwoSided) which default to off/absent — old scenes load unchanged.

## Open Questions

- Exact kraut billboard atlas layout (rows x cols, angle mapping) to lock in the fork's exporter — resolve during the fork-side task, record in `kraut-tree-import` spec notes.
- Whether HISM LOD bucketing should share `MeshRender::UpdateActiveLod`'s exact coverage math per instance (plan: yes, extracted into a shared helper).
- Scatter placement UX: generator-script-driven (v1) vs editor paint tool (follow-up).
