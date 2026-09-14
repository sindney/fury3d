# add-kraut-vegetation

## Why

The engine has no vegetation: outdoor scenes (ocean_island) are bare terrain. Authoring trees by hand is expensive, and rendering forests needs LODs, billboards, and instancing the engine does not yet have. Kraut-CLI (fork of jankrassnigg/Kraut with a headless, agent-controllable toolchain) can generate deterministic, LOD'd, billboarded trees from `.tree` descriptors + seed — exactly the missing content source.

## What Changes

- Add Kraut-CLI as a vendored ThirdParty submodule (build `KrautCLI` + `KrautPreview` only; skip the Qt editor), with a `fury kraut` CLI subcommand wrapping it — the FbxConverter subprocess precedent. Agents/scripts can generate trees on demand from `.tree` descriptors with JSON output and `--screenshot` previews.
- Add a Kraut -> fury bridge: the Kraut-CLI fork gains a glTF export (`export --format glb`) carrying per-LOD meshes (`<name>_LOD<n>`), billboard quad + atlas, vertex-color wind weights, PBR textures, and kraut metadata in glTF `extras`; fury imports it through the existing GltfImporter, mapping Kraut LODs onto the existing `Mesh::m_LodMeshes` chain (`mesh-lod`) and leaf/frond materials onto the PBR material model.
- Add instanced static mesh rendering (ISM + HISM, Unreal-style): an instancing render component that draws N transforms of one mesh via instanced draw calls, HISM mode bucketing instances per LOD tier each frame, with per-instance culling and instanced shadow participation. Instance transforms stream via SSBO where GL 4.3 is available (preferred, untested path) with an attribute-divisor VBO fallback for GL 3.3/4.1 (macOS).
- Add vegetation shading support: two-sided alpha-cutout foliage material (leaf-shaped shadows via alpha-tested depth shader variants), camera-facing billboard rendering as the terminal LOD tier, wind vertex sway driven by vertex-color weights, and a new `vertex_color` mesh channel.
- Ship sample assets: generated Kraut tree types as engine resources under `examples/Resource/Trees/` (like the ocean/sky samples, baked by `tools/gen_tree_assets.py`); populate `ocean_island.bin` with instanced trees as the reference scene.

## Capabilities

### New Capabilities

- `kraut-toolchain`: Kraut-CLI vendored as a ThirdParty tool building KrautCLI + KrautPreview headless; on-demand deterministic tree generation from `.tree` descriptors with JSON results and preview screenshots.
- `kraut-tree-import`: conversion of Kraut-exported trees (meshes, LODs incl. billboard/impostor tiers, textures) into fury-native scene/mesh assets, reusing the `mesh-lod` chain.
- `instanced-static-mesh`: ISM/HISM-style render component for large vegetation sets — one component, N transforms, instanced draw calls, per-instance culling, LOD-aware, shadow-casting.
- `vegetation-rendering`: foliage material path — alpha-cutout in gbuffer/shadow passes, two-sided lighting, camera-facing billboards, wind sway.

### Modified Capabilities

- `mesh-lod`: LOD chain gains a per-tier billboard flag (billboard tier rendered by the billboard shader path instead of plain mesh draw); chain population from externally-authored sources is exercised by the Kraut import.

## Impact

- **New code**: `engine/ThirdParty/kraut` (vendored Kraut-CLI submodule), fork-side glTF export (companion work in the Kraut-CLI repo), `fury kraut` CLI subcommand + `KrautImporter` (kraut extras handling), `InstancedMeshRender` component + instanced draw paths (gbuffer/shadow), vegetation shader variants (TWO_SIDED / WIND / BILLBOARD defines, alpha-tested depth), `tools/gen_tree_assets.py` sample generator, `Mesh` vertex-color channel.
- **Existing code touched**: `Mesh` (vertex colors, billboard LOD flag), `MeshRender`/pipeline shader selection (instanced + billboard variants), `PrelightPipeline`/`Pipeline` (instanced draw submission, instanced + alpha-tested shadow draws), `GltfImporter` (COLOR_0, kraut extras), content browser (tree samples surface as Mesh+Material assets), `Cli.cpp` (subcommand table, help text).
- **Dependencies**: Kraut-CLI submodule (CMake, no Qt for CLI+Preview; SDL2/ImGui/GL3.3 for preview), no new runtime engine deps; instancing prefers SSBO on GL 4.3+ with an attribute-divisor fallback on GL 3.3/4.1.
- **Sample content**: `examples/Resource/Trees/` committed tree assets + descriptors; `ocean_island.bin` updated with instanced vegetation.
