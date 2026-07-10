## 1. Setup and content-hash utility

- [x] 1.1 Add a `MeshContentHash(mesh)` free function (or `Mesh::ComputeContentHash()` method) in `engine/Fury/Mesh.{h,cpp}` that computes a 64-bit FNV-1a hash over `Positions.Data` bytes (raw float bytes) + top-level `Indices.Data` bytes + each submesh's `Indices.Data` bytes (in submesh order). Return as `uint64_t`. No allocations beyond the hash state.
- [x] 1.2 Add a `FormatHashHex(uint64_t)` helper (16-char lowercase hex string, e.g. `1a2b3c4d5e6f7a8b`) — place next to `MeshContentHash` or in `FileUtil` if it makes sense to share.
- [x] 1.3 Add a `ThumbnailDiskCache` namespace (or static helpers in `EditorAssetWindows.cpp`) with: `GetCacheDir()` returning `FileUtil::GetAbsPath("Resource/.thumbcache/")` + ensuring the directory exists via `std::filesystem::create_directories`; `GetCachePath(uint64_t hash)` returning `<cache_dir>/furye_<hex>.png`; `IsCached(uint64_t hash)` consulting the in-memory index; `WritePngAsync(path, pixels, w, h)` enqueuing a `ThreadUtil` worker that calls `stbi_write_png`.
- [x] 1.4 Add an in-memory disk-cache index: `std::unordered_set<std::string> g_DiskCacheIndex` populated on startup via `std::filesystem::directory_iterator` scan of `GetCacheDir()`. Add a helper `IndexCacheFile(filename)` that inserts a name on successful write.

## 2. Extend ThumbnailCacheEntry and wire the disk-cache fields

- [x] 2.1 Extend `ThumbnailCacheEntry` (`EditorAssetWindows.cpp:104-110`) with: `uint64_t contentHash = 0;` (last computed hash), `bool hashInFlight = false;` (an async hash is running), `bool wasDirtyLastFrame = false;` (for dirty-transition detection), `bool diskLoaded = false;` (the disk PNG has been loaded into the FBO this session).
- [x] 2.2 Update `GetThumbnailShader` (`EditorAssetWindows.cpp:113-139`) if needed to confirm the white-box Lambert look: 0.7 grey albedo, fixed light dir `(0.4, 0.8, 0.3)`, half-lambert floor 0.2. Also confirm `glClearColor(1.0, 1.0, 1.0, 1.0)` (opaque white) is set inside `RenderMeshToThumbnail` — change it if currently grey/black.
- [x] 2.3 Update `EditorAssetWindows.h` to declare the new public/internal helpers: `RefreshMeshThumbnailCache()`, `RefreshMeshThumbnailNow(mesh)` (for the right-click → Refresh action), `WarmDiskCacheIndex()`. Add forward declarations as needed.

## 3. Implement GetMeshThumbnail (cache lookup + async hash enqueue)

- [x] 3.1 Implement the body of `GetMeshThumbnail(mesh)` at `EditorAssetWindows.cpp:720-723`. Fast path: look up `g_MeshThumbnails[BufferId]`; if entry exists, `entry.contentHash != 0`, `mesh->GetDirty() == false`, and `entry.hashInFlight == false`, return `entry.colorRT->GetID()` immediately.
- [x] 3.2 Slow path A (dirty-transition): if `entry.wasDirtyLastFrame == true` and `mesh->GetDirty() == false` (transition just happened) and `entry.hashInFlight == false`, enqueue an async hash task via `ThreadUtil::Enqueue<uint64_t>([mesh]{ return MeshContentHash(mesh); }, [mesh, bufferId](std::shared_ptr<uint64_t> result){ ... })`. Set `entry.hashInFlight = true`. The main-thread callback compares `*result` to `entry.contentHash`: if different, set `entry.contentHash = *result`, clear `entry.diskLoaded = false` (force a re-load or re-render). If identical, no change. Clear `entry.hashInFlight = false`.
- [x] 3.3 Slow path B (cache miss): if `entry.contentHash == 0` and not in-flight, enqueue the hash task as above (first time seeing this mesh). Return `0` (or the gray fallback) for this frame so the worker has time to run.
- [x] 3.4 Slow path C (load or render): once `entry.contentHash != 0` and `entry.diskLoaded == false`: consult `IsCached(contentHash)`. If cached, load via `Texture::CreateFromImage(GetCachePath(contentHash), false, false)` and blit into `entry.colorRT` (use `RenderUtil::Blit` or a simple fullscreen-quad blit). Set `entry.diskLoaded = true`. If not cached, run `RenderMeshToThumbnail(mesh, entry)` (existing helper at `EditorAssetWindows.cpp:144-208`), then `glReadPixels` into a `std::vector<unsigned char>` (128×128×4), enqueue `WritePngAsync(GetCachePath(contentHash), pixels, 128, 128)` and call `IndexCacheFile(filename)`. Set `entry.diskLoaded = true`.
- [x] 3.5 Update `wasDirtyLastFrame` at the end of `GetMeshThumbnail`: `entry.wasDirtyLastFrame = mesh->GetDirty();`. Return `entry.colorRT->GetID()` once the FBO is populated; before that, return `0` so the Content Browser shows the gray fallback.
- [x] 3.6 Make sure `EvictStaleMeshThumbnails` (called every frame from the Content Browser) still clears `g_MeshThumbnails` entries whose `BufferId` left the live set — unchanged behavior, but verify it doesn't crash with the new fields.

## 4. Periodic refresh poll (calls into the dirty-transition detection)

- [x] 4.1 Implement `RefreshMeshThumbnailCache()` that iterates the currently-visible Content Browser mesh tiles and, for each mesh, calls `GetMeshThumbnail(mesh)` (which already runs the dirty-transition check). Rate-limit to once every 30 frames via a static frame counter. Skip meshes whose `hashInFlight == true`.
- [x] 4.2 Wire `RefreshMeshThumbnailCache()` into `Editor::Tick` (`engine/Fury/Editor/Editor.cpp` near line 575, after `RenderAllOpenAssetEditors`). Use a frame counter modulo 30 to gate the call.
- [x] 4.3 On `Editor::Initialize` (or first `Editor::Tick`), call `WarmDiskCacheIndex()` to scan `Resource/.thumbcache/` and populate `g_DiskCacheIndex`. Wrap in try/catch for the case where the directory doesn't exist yet (no error — just an empty index).
- [x] 4.4 Add a `Refresh` item to the mesh tile's right-click context menu in `RenderContentBrowserWindow` (`EditorWindows.cpp` near the existing `Duplicate`/`Rename`/`Delete` items). On click, look up `g_MeshThumbnails[BufferId]`, set `entry.contentHash = 0`, `entry.diskLoaded = false`, `entry.hashInFlight = false` to force a re-hash + re-render on the next poll.

## 5. Implement RenderMeshPreview (the Mesh editor 3D viewer)

- [x] 5.1 Implement the body of `RenderMeshPreview(mesh, popup_id, size, display_mesh)` at `EditorAssetWindows.cpp:607-620`. Allocate/resize a `PreviewRT` for `popup_id` from the existing `g_PreviewRTs` map (`EditorAssetWindows.cpp:590-597`). Bind the FBO, set the viewport, clear to opaque white `glClearColor(1,1,1,1)`.
- [x] 5.2 Build the camera matrices from `g_OrbitState[popup_id]` (existing struct at `EditorAssetWindows.cpp:54-70`). On first frame for this `popup_id`, call `ComputeInitialDistance(display_mesh, size)` to set `initialDistance` and `distance`. Compute the camera position from azimuth + elevation + distance around the AABB center. Build view via `Matrix4::LookAt(eye, target, up)` and projection via `Matrix4::PerspectiveFov(fov, aspect, near, far)`.
- [x] 5.3 Bind the white-box Lambert shader (reuse `GetThumbnailShader`), call `shader->BindMatrix("_ViewMatrix", view)`, `shader->BindMatrix("_ProjectionMatrix", proj)`, `shader->BindMesh(display_mesh)`, then iterate submeshes with `shader->BindSubMesh(display_mesh, i)` + `glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0)`. Unbind the FBO.
- [x] 5.4 Present the rendered texture via `ImGui::Image((ImTextureID)(intptr_t)rt->GetColorTexture()->GetID(), size, ImVec2(0, 1), ImVec2(1, 0))` inside an `ImGui::BeginChild("viewer", size, true, ImGuiWindowFlags_NoScrollbar)` block. Capture mouse position / drag state relative to this child window for camera input.
- [x] 5.5 Orbit input: on `ImGui::IsItemHovered()` and `ImGui::IsMouseDragging(ImGuiMouseButton_Left)`, update `g_OrbitState[popup_id].azimuth -= dx * sensitivity;` and `elevation -= dy * sensitivity;` (clamp elevation to `[-π/2 + 0.01, π/2 - 0.01]`).
- [x] 5.6 Dolly input: on hover and `ImGui::GetIO().MouseWheel != 0`, update `g_OrbitState[popup_id].distance *= (1.0f - wheel * 0.1f);` and clamp to `[0.1f * initialDistance, 10.0f * initialDistance]`.
- [x] 5.7 Pan input: on `ImGui::IsMouseDragging(ImGuiMouseButton_Right)` OR `IsMouseDragging(ImGuiMouseButton_Middle)`, translate the orbit target point in screen space. Project the drag delta onto the camera's right and up vectors, scale by `(distance / preview_height)`, subtract from the target. Keep `azimuth` and `elevation` unchanged.
- [x] 5.8 Render the `ImGuizmo::ViewManipulate` axis cube in the top-right corner of the viewer pane via `ImGuizmo::ViewManipulate(view, length, position, size, backgroundColor)` (use `ImGui::GetWindowDrawList()`-style absolute coordinates or `ImVec2(pos.x + size.x - 48, pos.y + 8)` for the cube position). On click, the cube returns a modified view matrix — extract the new azimuth/elevation/distance from it (or just feed the matrix directly into the next frame's view computation).
- [x] 5.9 Pass through `display_mesh` (the LOD-selected mesh) instead of the base `mesh` so the preview reflects the user's LOD dropdown selection (already plumbed in `RenderMeshEditorWindow` at `EditorAssetWindows.cpp:650-660`).

## 6. Mesh editor two-pane layout

- [x] 6.1 Restructure `RenderMeshEditorWindow` (`EditorAssetWindows.cpp:627-673`) to compute `avail = ImGui::GetContentRegionAvail()`, then `sidebar_width = std::max(240.0f, avail.x * 0.30f)` and `viewer_width = avail.x - sidebar_width - 8.0f`.
- [x] 6.2 Call `RenderMeshPreview(mesh, popup_id, ImVec2(viewer_width, avail.y), display_mesh)` first.
- [x] 6.3 Immediately after, `ImGui::SameLine();` then `ImGui::BeginChild("metadata", ImVec2(sidebar_width, avail.y), true);` then `RenderMeshMetadata(mesh, popup_id);` then `ImGui::EndChild();`.
- [x] 6.4 Verify the existing LOD dropdown lookup (`g_LodSelection[popup_id]`) still flows into `display_mesh` correctly within the new layout.
- [x] 6.5 Confirm `ImGuiWindowFlags_AlwaysAutoResize` is NOT set on the Mesh editor window (it should already not be — the existing code at `EditorAssetWindows.cpp:629` uses `ImGuiCond_FirstUseEver` size only).

## 7. Validation and manual testing

- [x] 7.1 Build the engine with `WITH_EDITOR=ON` (default). Fix any compile errors / warnings.
- [ ] 7.2 Launch the editor and open `Resource/Scene/scene.bin` (or any scene with meshes). Verify mesh tiles in the Content Browser show actual rendered previews (simple Lambert on black background) instead of gray placeholder rects.
- [ ] 7.3 Double-click a mesh tile. Verify the Mesh editor opens with a two-pane layout (viewer left, metadata right). Verify the viewer renders the mesh centered with the simple Lambert look.
- [ ] 7.4 In the Mesh editor, verify LMB drag orbits, mouse wheel zooms (clamped), RMB or MMB drag pans, and the axis cube in the top-right reflects the current orientation. Click an axis cube face to snap the camera.
- [ ] 7.5 Verify the LOD dropdown (if the mesh has LODs) switches the previewed mesh.
- [ ] 7.6 Right-click a mesh tile → Refresh. Verify the thumbnail re-renders within ~0.5s.
- [ ] 7.7 Quit the editor. Verify `Resource/.thumbcache/` contains `furye_<hash>.png` files for the meshes that were rendered.
- [ ] 7.8 Relaunch the editor and open the same scene. Verify thumbnails appear quickly (warm from disk) without re-rendering. Add a `FURYD` log line in the cache-hit path to confirm.
- [ ] 7.9 Edit a mesh's vertices (via an external test script or by modifying the mesh in code and calling `SetDirty()`). Verify the thumbnail refreshes on the next periodic poll after the dirty-transition.
- [ ] 7.10 Test disk-write failure: temporarily make `Resource/.thumbcache/` read-only (`chmod -w`). Verify the editor emits one `FURYW` log line per failed write and continues serving thumbnails from the in-memory FBO. Restore permissions after.
- [ ] 7.11 Verify the existing `EvictStaleMeshThumbnails` still works: clear the scene (`File → New`) and confirm the in-memory FBO entries for the old meshes are dropped (disk files remain).
- [x] 7.12 Run the existing test suite (`ctest` or the project's test command) and confirm no regressions.

## 8. White-output fix and visual-debug CLI

- [x] 8.1 Make the simple Lambert shader robust against missing/zero vertex normals: when `length(v_normal) < 0.0001`, fall back to `vec3(0, 1, 0)` so glTF meshes without a normal attribute produce a non-white silhouette.
- [x] 8.2 Change the FBO clear color from opaque white to opaque black in both `RenderMeshToThumbnail` and `RenderMeshPreview` so the editor preview / Content Browser tile match the 3D scene's viewport background.
- [x] 8.3 Add a `fury render-mesh <scene> <mesh_name> <output.png>` CLI subcommand in `examples/main.cpp` (alongside the existing `--screenshot` flow) that loads a scene, finds a mesh, and renders it to a 256×256 offscreen FBO with the same simple Lambert shader. Writes a PNG via `stbi_write_png`. Reuses `Cli::LoadSceneForExec` for the scene-loading dispatch.
- [x] 8.4 Expose `Cli::LoadSceneForExec` as a public static method on the `Cli` class so the launcher-side `RenderMeshCli` can reuse it. Renamed the file-local helper to `LoadSceneForExecImpl` to avoid name collision.
- [x] 8.5 Add a `render-mesh` line to the `fury help` top-level help text.
- [x] 8.6 Build `fury` and `furye` and verify `fury render-mesh Resource/Scene/scene.json T90 /tmp/t90.png` produces a non-white PNG (black background, grey Lambert-shaded mesh).
- [x] 8.7 Update the `mesh-thumbnail-disk-cache` spec to specify the simple Lambert look on black background, and add a "Mesh with missing normals still renders" scenario.
- [x] 8.8 Update the `editor-shell` and `asset-editor-windows` spec deltas to reference the simple Lambert look on black background (replacing the original "white-box" wording) and the shader's robustness against missing normals.
- [x] 8.9 Add a new `render-mesh-cli` capability spec describing the new subcommand and its motivation.
- [x] 8.10 Update `proposal.md` to add `render-mesh-cli` to the New Capabilities list and replace "white-box Lambert" with "simple Lambert" in Modified Capabilities.
- [x] 8.11 Update `design.md` Decision 5 to describe the simple Lambert look, the black background, and the shader's robustness against missing normals. Add Decision 10 describing the `fury render-mesh` subcommand and why it's the right shape.
