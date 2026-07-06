## Why

The engine's `Mesh` and `MeshRender` model is single-resolution: a glTF file with multiple LOD meshes (a common production pattern) is imported as N independent `Mesh` assets and the artist has no first-class way to pick which resolution is drawn at runtime, and no way to switch between them in the editor's mesh window. This forces importers and tools to flatten the LOD relationship at edit time and prevents distance-based LOD selection at runtime.

## What Changes

- Add a `LodGroup` data type that owns a chain of `Mesh::Ptr` LODs (LOD 0 = highest detail) with per-LOD `screen-relative transition height` thresholds.
- Extend `MeshRender` to optionally reference a `LodGroup` and to choose which LOD to draw based on the camera's screen-space size of the model's AABB.
- Teach the glTF importer to detect LODs: same-named meshes flagged via the `MSFT_lod` extension (nodes carry a `msft_lod[]` list with `screen coverage` thresholds), or — when the extension is absent — heuristically group multiple `gltf mesh` entries that share a name prefix (e.g. `Tree_LOD0`, `Tree_LOD1`, `Tree_LOD2`) and synthesize thresholds at 1.0, 0.66, 0.33, etc. The first form is the spec'd one; the heuristic is a best-effort fallback for the sample assets.
- Persist `LodGroup` through `Mesh::Save` / `Mesh::Load` so LOD chains survive scene file round-trips.
- In the editor's mesh window, add an LOD dropdown (`LOD 0`, `LOD 1`, …) so the user can preview each level. The MeshRender component panel in the scene inspector shows the active LOD index and the configured transition thresholds.

## Capabilities

### New Capabilities

- `mesh-lod`: LOD-aware mesh data model, runtime LOD selection, glTF MSFT_lod + name-prefix heuristic import, and the editor's LOD dropdown / transition-threshold UI.

### Modified Capabilities

- `gltf-importer`: importer must populate `LodGroup`s and emit one `MeshRender` per glTF node that is now backed by a `LodGroup` rather than a single `Mesh`. New optional `MSFT_lod` extension support (no longer an unsupported extension).
- `asset-editor-windows`: mesh window must expose an LOD selector.

## Impact

- Engine code: `Fury/Mesh.h/.cpp` (new `LodGroup` class + persistence), `Fury/MeshRender.h/.cpp` (LOD-aware draw path), `Fury/Shader.h/.cpp` (no public change; renderer queries `MeshRender::GetActiveLod()`), `Fury/GltfImporter.h/.cpp` (MSFT_lod extension + name-prefix fallback).
- Editor code: `Fury/Editor/EditorAssetWindows.cpp` (mesh window LOD dropdown + per-LOD metadata), `Fury/Editor/EditorNodeProperties.cpp` (MeshRender body shows active LOD + thresholds).
- Serialization: `Mesh` JSON gains a new `lod_group` key (object or null). Pre-LOD scene files load unchanged — `lod_group` is read as optional and the absence means "single LOD = the mesh itself".
- Backwards-compat: a `MeshRender` referencing a plain `Mesh` keeps its current draw path. The new LOD path is opt-in (set via the new `LodGroup` reference on `MeshRender`).
- Test surface: glTF sample assets under `glTF-Sample-Assets/Models/` provide cover for both the extension path (if any MSFT_lod sample is present) and the name-prefix fallback.
