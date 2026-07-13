## Why

Three defects surface in the "generate LODs → save `scene_lod.bin` → reopen" workflow:

1. **Textures load as 0×0 after reopening a saved scene.** A saved `.bin` stores a multi-segment project-relative texture path (e.g. `Resource/Scene/wheels.jpg`). Reopening via `Importer.LoadScene` sets the scene working directory to the `.bin`'s own folder, and `Scene::Path` blindly prepends it — producing `Resource/Scene/Resource/Scene/wheels.jpg`. `LoadImage` fails and the texture stays at its default 0×0. File-backed textures are never made sibling-relative on save (unlike memory-backed textures), so a saved scene is not portable.
2. **The runtime LOD never switches, even zoomed out in LOD-debug mode.** The active-LOD selection comparison is inverted, so every mesh is pinned to LOD 0 regardless of camera coverage — the debug color and triangle count never change.
3. **LOD generation is too aggressive with no alternative.** The simplifier hard-codes the "sloppy" grid-based path; the border-preserving quadric path is dead code, and the dialog offers no way to pick a gentler method.

## What Changes

- **Fix runtime active-LOD selection**: correct the inverted threshold comparison in `MeshRender::UpdateActiveLod` so the active LOD follows screen-coverage, and compute coverage from the mesh's **world-space** AABB (apply the owning node's world transform) so non-origin / non-unit-scale meshes select correctly. LOD debug color and poly count now change as the camera zooms.
- **Make saved scenes portable (fix 0×0 textures)**: on save, treat file-backed textures the same as memory-backed ones — copy the source image to a sibling file next to the output and rewrite the stored path to a bare filename resolved against `Scene::Path`. This removes the double-prepend and makes `Save As` to any directory reload correctly.
- **Add a LOD-generation method selector**: extend the "Generate LODs…" dialog with a **Method** dropdown (Sloppy / Quadric border-preserving / Quadric legacy), thread the choice through `MeshSimplifyOptions`, and dispatch on it in `MeshSimplifier` instead of the hard-coded `use_sloppy = true`. Default to the less-aggressive border-preserving quadric method.

## Capabilities

### New Capabilities
- `mesh-lod-generation`: user-selectable mesh simplification method in the editor LOD-gen dialog, threaded through `MeshSimplifyOptions` into the `MeshSimplifier` dispatch (Sloppy / Quadric / Quadric-legacy).

### Modified Capabilities
- `mesh-lod`: correct the runtime active-LOD selection — fix the inverted coverage/threshold comparison and compute coverage from the world-space AABB so the active LOD (and the LOD-debug color / poly count) responds to camera zoom.
- `embedded-textures`: extend the save-time extraction so file-backed textures are also relocated to sibling files with bare filenames, making saved scenes portable and eliminating the 0×0 reload failure.

## Impact

- Engine: `engine/Fury/MeshRender.cpp` (LOD selection + world-space coverage), `engine/Fury/MeshSimplifier.h`/`.cpp` (method enum + dispatch), `engine/Fury/FileUtil.cpp` (file-backed texture relocation on save), `engine/Fury/Editor/EditorAssetWindows.cpp` (method dropdown in LOD-gen dialog).
- Behavior: saved `.bin`/`.json` scenes become relocatable; existing scenes still load. LOD-debug view becomes functional. Default LOD-gen output is less aggressive (may change generated LOD triangle counts vs. today).
- No public data-format break: the LOD chain and texture serialization formats are unchanged; only path values written for file-backed textures change (bare filename instead of multi-segment path).
