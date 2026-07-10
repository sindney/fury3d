## Why

Mesh assets in the Content Browser today render as a flat dark-gray rectangle with a yellow "M" badge (`EditorWindows.cpp:1248-1264` calls `GetMeshThumbnail`, which returns `0` at `EditorAssetWindows.cpp:720-723`), and the Mesh editor's preview pane shows a gray rect labeled "(3D preview — render pending)" (`EditorAssetWindows.cpp:607-620`). Both are placeholder stubs that bypass already-written infrastructure (`RenderMeshToThumbnail` at `EditorAssetWindows.cpp:144-208`, an inline flat-shaded Lambert shader at `EditorAssetWindows.cpp:113-139`, the `g_MeshThumbnails` FBO cache at `EditorAssetWindows.cpp:104-110`, the `OrbitState` map at `EditorAssetWindows.cpp:54-96`, and the `g_PreviewRTs` map at `EditorAssetWindows.cpp:590-597`).

Users can't visually identify meshes at a glance, and the Mesh editor has no interactive 3D viewer. This change closes that gap by rendering real mesh previews (white-box Lambert + directional light), persisting them to a disk cache so thumbnails survive across editor sessions and warm up the in-memory cache asynchronously, and turning the Mesh editor into a proper 3D viewer with orbit/zoom and a right-side metadata panel.

## What Changes

- Implement `GetMeshThumbnail(mesh)` so Content Browser mesh tiles render a 128×128 snapshot of the mesh rendered in a white box with basic Lambert shading from a fixed directional light. Reuse the existing `RenderMeshToThumbnail` helper and `ThumbnailCacheEntry` map.
- Add a disk-backed PNG thumbnail cache: each rendered snapshot is also written to a temp folder as `furye_<hash_of_meshdata>.png`, where the hash is a content fingerprint derived from vertex positions + indices + submesh indices (NOT the `BufferId`, which only changes on object identity, not on content edits). Existing files are reused on cache hit.
- Generate thumbnails asynchronously: CPU-side content hashing + PNG encoding run on the `ThreadUtil` worker pool; the GL render + `glReadPixels` + `stbi_write_png` write happen on the main thread (GL context is main-thread only, per `ThreadUtil::IsMainThread`).
- Add a periodic cache-refresh poll in `Editor::Tick` (e.g. every ~30 frames, rate-limited) that re-checks live mesh tiles against the disk cache, regenerating snapshots whose content hash has changed or whose cache file is missing.
- Re-render cached thumbnails when mesh data changes (key the cache on a content hash; `mesh->GetDirty()` transitions trigger a re-render on the next clean frame, since `BufferId` alone doesn't catch in-place edits — see `Buffer.h:18-23`).
- Implement `RenderMeshPreview(mesh, popup_id, size, display_mesh)` to render the currently-selected LOD mesh into a `PreviewRT` with the same white-box Lambert + directional light look, framed by `ComputeInitialDistance` so the bounding sphere fills ~60% of the shorter axis (per `asset-editor-windows/spec.md:73-74`).
- Add orbit/zoom/pan camera controls to the Mesh editor preview, mirroring the main 3D scene UX: drag to orbit (azimuth/elevation), wheel to dolly (clamped to `[0.1, 10] × initialDistance`), and the `ImGuizmo::ViewManipulate` axis cube in the top-right corner for orientation feedback. The camera starts focused on the mesh's AABB center.
- Restructure the Mesh editor window into a two-pane layout: the 3D viewer fills the left/main region; the right side hosts a metadata panel (existing `RenderMeshMetadata` contents: name, LOD dropdown, LOD thresholds, Generate LODs modal, stats, AABB, cast shadows, joint summary).
- The disk cache lives under `FileUtil::GetAbsPath("Resource/.thumbcache/")` (created on first use) and is best-effort — if disk write fails or the folder is missing, the in-memory FBO cache still serves thumbnails for the current session.

## Capabilities

### New Capabilities
- `mesh-thumbnail-disk-cache`: Persistent on-disk PNG cache of mesh preview snapshots, keyed by a content fingerprint (hash of vertex positions + indices + submesh indices), with async generation via `ThreadUtil` (CPU hash + PNG encode off-thread, GL render + readback on main thread) and a periodic refresh poll that re-renders stale entries.
- `render-mesh-cli`: A `fury render-mesh <scene> <mesh> <output.png>` CLI subcommand that loads a scene and renders a specific mesh to a PNG for visual debugging of the mesh-rendering pipeline without launching the editor.

### Modified Capabilities
- `editor-shell`: Content Browser mesh tiles now display a rendered preview snapshot instead of the gray fallback rect, and the in-memory FBO cache (`g_MeshThumbnails`) is warmed from the disk cache when a `furye_<hash>.png` file already exists.
- `asset-editor-windows`: The Mesh editor's placeholder preview pane is replaced with an interactive 3D viewer (orbit/dolly/pan, axis cube, AABB-framed camera, simple Lambert shading) and the window layout changes from a stacked single-column to a two-pane layout (viewer left, metadata right).

## Impact

- **Code**:
  - `engine/Fury/Editor/EditorAssetWindows.cpp` — implement the two placeholder stubs (`GetMeshThumbnail` at line 720, `RenderMeshPreview` at line 607); extend `ThumbnailCacheEntry` to track last-rendered content hash; add disk-cache helpers (read/write `furye_<hash>.png`, ensure cache dir exists); add async hashing + PNG encode via `ThreadUtil`; wire the periodic refresh poll into `Editor::Tick` or `RenderAllOpenAssetEditors`.
  - `engine/Fury/Editor/EditorAssetWindows.h` — public API additions for the disk cache + refresh poll.
  - `engine/Fury/Editor/Editor.cpp` — call the periodic refresh poll from `Editor::Tick`.
  - Mesh hashing — a new free function (or `Mesh`/`Buffer` method) that computes a stable content fingerprint; no existing hashing utility exists in the engine.
- **Dependencies**:
  - `stb_image_write` (already linked for `WriteBackBufferAsPng` at `Engine.cpp:22-23`) — reused for `stbi_write_png`.
  - `std::filesystem` for temp dir creation / file existence checks (no existing `std::filesystem::temp_directory_path` use in the engine).
- **APIs**: New internal editor-only helpers (no Lua/engine public API changes).
- **Systems**: Disk I/O on the `Resource/.thumbcache/` folder (created on demand). GL context pinned to main thread for the render + readback step. `ThreadUtil` worker pool used for hashing + PNG encoding only.
- **Specs**: `editor-shell` and `asset-editor-windows` get delta specs; new `mesh-thumbnail-disk-cache` spec is created.
- **Risk**: Content hashing cost on large meshes — mitigated by caching the hash in the `ThumbnailCacheEntry` and only recomputing when `mesh->GetDirty()` transitions from true to false. Disk cache is best-effort (failure doesn't break the editor).
