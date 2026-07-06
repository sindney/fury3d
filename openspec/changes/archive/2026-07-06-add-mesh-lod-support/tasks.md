## 1. Engine data model

- [x] 1.1 Add `LodGroup` class to `Fury/Mesh.h` (value type, not an `Entity`): ordered `Mesh::Ptr` list, parallel `float` thresholds, getters/setters, validation (`FURYE` on out-of-order thresholds).
- [x] 1.2 Add `LodGroup::Save` / `LodGroup::Load` helpers (write `thresholds` float array + `meshes` name array). The names are resolved through the supplied `EntityManager` so load failures can `FURYE` and reject.
- [x] 1.3 Add `MeshRender::SetLodGroup` / `MeshRender::GetLodGroup` and `MeshRender::GetActiveLod` (cached on the instance).
- [x] 1.4 Add `MeshRender::UpdateActiveLod(camera)` that computes screen coverage from the mesh's AABB + the camera projection and stores the chosen index. Wire it into the per-frame draw path used by the renderer (the same draw path that currently calls `Shader::BindMesh`).
- [x] 1.5 Extend `MeshRender::Save` to write the `lod_group` sub-object (only when the group is non-empty) and `MeshRender::Load` to read it as optional.

## 2. glTF importer

- [x] 2.1 In `GltfImporter::HasUnsupportedFeatures`, allow `MSFT_lod` in `extensionsRequired` (other extensions still reject).
- [x] 2.2 After `TranslateMesh` produces the engine meshes, scan the glTF nodes a second time: for each node carrying `extensions.MSFT_lod`, build a `LodGroup` from the listed `mesh` / `screen coverage` pairs and attach it to the matching `MeshRender`.
- [x] 2.3 Add a name-suffix pass: collect engine `Mesh` names matching `<base>_LOD<N>` (regex), group by `<base>`, require ≥2 entries per group, build a `LodGroup` with linear `1.0 → 0.0` thresholds, and assign to any `MeshRender` whose original bound mesh is the highest-detail (`LOD0`) entry.
- [x] 2.4 Log `FURYI` for each group formed (base name + entries) and `FURYD` for any lone `_LOD<N>` mesh that didn't form a group. `FURYW` for non-contiguous indices.

## 3. Editor — mesh window LOD dropdown

- [x] 3.1 In `EditorAssetWindows::RenderMeshMetadata`, render an `LOD` dropdown listing `LOD 0..N-1` when the mesh has a `LodGroup`. Hold the selection in a per-window `static int`. Show "(no LOD chain)" otherwise.
- [x] 3.2 Plumb the selected LOD index into `RenderMeshPreview` (extend the signature to take an LOD index) so the preview renders the selected `LodGroup::GetMesh(i)` instead of the highest-detail mesh.
- [x] 3.3 In the metadata block, when an LOD is selected, show per-LOD vertex/index/submesh counts for that LOD (read from the picked `Mesh::Ptr`). When no `LodGroup` is bound, keep the existing single-mesh metadata.

## 4. Editor — scene inspector MeshRender body

- [x] 4.1 In `EditorNodeProperties::RenderMeshRenderBody`, add a read-only `Active LOD: <n>` line below the bound-mesh row. Add a `Thresholds:` block listing `{threshold, mesh name}` pairs when a `LodGroup` is bound.
- [x] 4.2 When no `LodGroup` is bound, hide the threshold block and show `LOD: (single mesh)`.

## 5. Verification

- [x] 5.1 Build the engine (`cmake --build build` or equivalent for this repo) and confirm no regressions in the existing `convert gltf Box.gltf out.json` flow.
- [x] 5.2 Convert a name-suffix LOD sample from `glTF-Sample-Assets/Models/` (e.g. duplicate a simple model as `<name>_LOD0` and `<name>_LOD1` if no real sample is present) and confirm the output `Scene`'s root has a `MeshRender` with a 2-entry `LodGroup`.
- [x] 5.3 Round-trip the output through `Save` + `Load` and confirm the `LodGroup` is preserved.
- [x] 5.4 Open the mesh editor on the LOD asset and verify the dropdown lists both LODs and the preview switches when the user picks one.
- [x] 5.5 Select a `MeshRender` with a `LodGroup` in the scene inspector and verify the active-LOD readout and threshold list render.
