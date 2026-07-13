## 1. Fix runtime LOD selection (mesh-lod)

- [x] 1.1 In `MeshRender::UpdateActiveLod` (`engine/Fury/MeshRender.cpp` ~L330–339), invert the predicate to `coverage >= base->GetLodThreshold(i)`, keeping the "first matching `i` wins, else deepest (`count-1`)" walk.
- [x] 1.2 Compute `coverage` from the mesh's world-space AABB: apply the owning `SceneNode`'s world matrix to the model-space AABB (or use `SceneNode::GetWorldAABB`, `SceneNode.h:107`) before projecting through the camera (`MeshRender.cpp` ~L295–325).
- [x] 1.3 Verify the debug overlay reflects the fix: with `PipelineSwitch::LOD_DEBUG_COLORS` on, zooming the camera out changes the tint (green→yellow→red) and the reported triangle count, since both derive from `render->GetActiveLod()` / `GetActiveMesh()` (`PrelightPipeline.cpp:218,222,280`).

## 2. Relocate file-backed textures on save (embedded-textures)

- [x] 2.1 In the save-time extraction path (`FileUtil.cpp`, alongside `ExtractMemoryBackedTextures` ~L185–272), add handling for file-backed textures: resolve `m_FilePath` against the current `Scene::Path`, and derive `target = basename(m_FilePath)`.
- [x] 2.2 Copy the resolved source image to `dirname(output)/target`, skipping the copy when a byte-equal file already exists; on filename collision with differing bytes, append a `_2` (etc.) suffix.
- [x] 2.3 Rewrite `texture->m_FilePath` to the bare `target` filename after a successful copy/reuse so serialization emits a sibling reference.
- [x] 2.4 When the resolved source file is missing, log a `FURYW` warning, leave the path unchanged, and let the save still return true.
- [x] 2.5 Confirm both `SaveFile` and `SaveCompressedFile` run the relocation (and that `SaveByExtension` inherits it via those calls), so `.json` and `.bin` behave identically.

## 3. Add LOD-generation method selection (mesh-lod-generation)

- [x] 3.1 Add `enum class Method { Quadric, Sloppy, QuadricLegacy }` and a `Method method = Method::Quadric` field to `MeshSimplifyOptions` (`engine/Fury/MeshSimplifier.h` ~L17–44).
- [x] 3.2 Replace the hard-coded `const bool use_sloppy = true` (`MeshSimplifier.cpp:242`) with dispatch on `opts.method`, threading the choice into the submesh helper (`:376`, consumed at `:112` and `:306`); pass `meshopt_SimplifyLockBorder` for `Quadric` (when `lock_borders`) and no lock for `QuadricLegacy`; keep `meshopt_simplifySloppy` for `Sloppy`.
- [x] 3.3 Add a "Method" `ImGui::Combo` to the "Generate LODs…" dialog (`engine/Fury/Editor/EditorAssetWindows.cpp` ~L538 `LODOpts` struct and ~L550–558 fields) with labels mapping to the three methods, defaulting to the border-preserving Quadric.
- [x] 3.4 Copy the selected method into `MeshSimplifyOptions` before the `SimplifyMesh` call (`EditorAssetWindows.cpp` ~L561–567).

## 4. Verification

- [x] 4.1 Build the engine and editor; confirm no warnings from the changed files.
- [x] 4.2 In the editor: load `scene.bin`, generate LODs, save `scene_lod.bin`, reopen it, and confirm the material's diffuse texture loads at non-zero resolution (not 0×0) and that the sibling image file was written next to the scene.
- [x] 4.3 In LOD-debug mode, zoom the camera out on the tank meshes and confirm the LOD color and triangle count change across at least two LOD levels.
- [x] 4.4 Regenerate LODs with each method (Quadric / Sloppy / Quadric legacy) and confirm the Quadric default preserves UV seams / borders and produces less-aggressive results than Sloppy.
