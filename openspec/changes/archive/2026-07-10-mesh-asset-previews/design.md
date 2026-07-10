## Context

The Content Browser's mesh tiles and the Mesh editor's preview pane are both placeholder stubs today (`EditorAssetWindows.cpp:720-723` `GetMeshThumbnail` returns 0; `EditorAssetWindows.cpp:607-620` `RenderMeshPreview` draws a gray rect labeled "render pending"). The surrounding infrastructure is already written but inert:

- `ThumbnailCacheEntry` struct + `g_MeshThumbnails` map keyed on `BufferId` (`EditorAssetWindows.cpp:104-110`)
- Inline flat-shaded Lambert shader compiled lazily by `GetThumbnailShader` (`EditorAssetWindows.cpp:113-139`) — fixed light dir `(0.4, 0.8, 0.3)`, `0.7` grey albedo, half-lambert floor of 0.2
- `RenderMeshToThumbnail(mesh, entry)` — full FBO bind/clear/draw/readback helper (`EditorAssetWindows.cpp:144-208`)
- `EvictStaleMeshThumbnails(liveBufferIds)` called every frame from `RenderContentBrowserWindow` (`EditorWindows.cpp:1570-1579`)
- `OrbitState` map + `ComputeInitialDistance(mesh, previewSize)` for AABB-framed camera (`EditorAssetWindows.cpp:54-96`)
- `PreviewRT` struct + `g_PreviewRTs` map declared but unused (`EditorAssetWindows.cpp:590-597`)
- `RenderMeshMetadata` — full metadata block already rendered (LOD dropdown, thresholds, Generate LODs modal, stats, AABB, cast shadows, joints) at `EditorAssetWindows.cpp:357-581`
- `RenderMeshEditorWindow` — window shell + stacked layout at `EditorAssetWindows.cpp:627-673`

Constraints:

- **GL context is main-thread only** (`ThreadUtil::IsMainThread` is asserted in GL calls). The render + `glReadPixels` + `stbi_write_png` write MUST happen on the main thread.
- **`BufferId` is identity, not content** (`Buffer.h:18-23`) — it increments at Mesh construction and never changes when the mesh's vertex data is edited in place. The existing comment at `editor-shell/spec.md:430-435` says "FBO is regenerated only when the mesh's `BufferId` changes (i.e. the mesh was re-uploaded to the GPU)" — but that's wrong about in-place edits. `Buffer::m_Dirty` flips on edit and clears after `UpdateBuffer()`, so a dirty-transition is the correct trigger.
- **`stb_image_write` is already linked** (used by `Engine::WriteBackBufferAsPng` at `Engine.cpp:22-23, 32-65`).
- **No existing disk cache utility** — `FileUtil` has no temp-dir / cache-dir helpers; we add a small one.
- **No existing content-hash utility** — we add a small FNV-1a or 64-bit hash over `Positions.Data + Indices.Data + submesh Indices.Data`. No need for cryptographic strength; collisions just cause a stale thumbnail until the next dirty-transition.
- **Per-frame update loop** (`Engine.cpp:239-328`): `Editor::Tick` → `RenderAllOpenAssetEditors` → `RenderMeshEditorWindow` → `RenderMeshPreview`. `ThreadUtil::Instance()->Update()` runs on line 203 and marshals finished-task callbacks to the main thread — this is where async-hashing results land.
- **The existing spec wording** at `editor-shell/spec.md:364` says "drawn with the mesh's first material or a fallback flat shader" and `asset-editor-windows/spec.md:81` says "first bound material if the mesh has one, else a fallback flat-shaded material". The user wants the previews to ALWAYS use a simple Lambert look (so the icon and the editor preview match). This is a deliberate spec-level change.

Stakeholders: editor users (visual identification of meshes), the existing `asset-editor-windows` and `editor-shell` specs (modified deltas), the new `mesh-thumbnail-disk-cache` capability.

## Goals / Non-Goals

**Goals:**

- Replace both placeholder stubs with real renders so mesh tiles and the Mesh editor show actual mesh previews.
- Persist thumbnails to disk (`Resource/.thumbcache/furye_<hash>.png`) so they survive across editor sessions and warm the in-memory FBO cache on startup.
- Generate thumbnails asynchronously: CPU-only work (content hashing, PNG encoding) on `ThreadUtil` workers; GL work (render + readback) on the main thread.
- Re-render thumbnails when mesh content actually changes (dirty-transition), not just when `BufferId` changes.
- Make the Mesh editor a proper 3D viewer: orbit + dolly + pan, AABB-framed default camera, axis cube, two-pane layout (viewer left, metadata right).
- Use a consistent visual look across thumbnails and the editor preview: simple Lambert (flat 0.7 grey albedo + fixed directional light on an opaque black background) — matches the 3D scene's viewport look.

**Non-Goals:**

- No thumbnail generation for non-mesh assets (materials already work; textures are direct samples).
- No persistent thumbnail storage keyed on anything other than content hash (no per-project / per-user cache partitioning).
- No GPU-driven thumbnail batching (rendering one mesh per frame is sub-millisecond; optimization is unnecessary).
- No streaming / async GL upload — the GL render is synchronous on the main thread; only the hash and PNG encode are off-thread.
- No changes to the mesh's first-material rendering in the main scene pipeline — only the editor preview switches to the simple Lambert look.
- No pan in the existing spec's "Pan is NOT supported" wording is reversed — but only for the mesh editor preview, not the broader spec.
- No automatic cache GC / size cap beyond the existing `EvictStaleMeshThumbnails` (which drops entries whose `BufferId` left the live set). Disk cache may grow unbounded; manual clear is the escape valve.

## Decisions

### Decision 1: Content hash key — FNV-1a 64-bit over vertex positions + indices

**Choice:** Compute a 64-bit FNV-1a hash over `Positions.Data` (raw float bytes) + the top-level `Indices.Data` (raw uint bytes) + each submesh's `Indices.Data`. Format as 16-char lowercase hex for the filename: `furye_<hex>.png`.

**Why FNV-1a 64-bit:**
- Fast (one pass over contiguous memory, no allocations).
- Good enough distribution for the small input sizes (vertex buffers are KB to a few MB).
- No external dependency (no MD5/SHA lib to wire up).
- 64-bit gives collision probability below 1 in 10^9 for any realistic mesh library size (birthday bound at ~4 billion meshes before 50% collision).
- Filename is short (`furye_1a2b3c4d5e6f7a8b.png`) and filesystem-safe.

**Alternatives considered:**
- `BufferId` (existing key) — rejected: identity, not content. In-place edits (the most common reason to re-render a thumbnail) don't change `BufferId`.
- `Entity::GetHashCode()` (UUID-derived) — rejected: persistent identity, not content. Same problem.
- MD5/SHA — rejected: cryptographic strength is unnecessary; linking a hash lib for one feature is overkill.
- `std::hash<std::vector<float>>` — rejected: not stable across runs / platforms (implementation-defined seed).
- Hash over the rapidjson-serialized `Mesh::Save` output — rejected: too slow for a per-frame cache check; JSON serialization is ~100x slower than a byte hash.

**Why positions + indices (and not normals / UVs / joints):**
- Positions + indices define the silhouette — what the user is trying to visually identify.
- Including normals would cause re-renders on normal-only edits that the user can't visually distinguish in a Lambert preview anyway.
- UV changes don't affect the silhouette.
- Joint changes are rare and would just bump the cache cost; users editing a skinned mesh typically care about the bind pose, not the rig.
- Trade-off: a mesh that has only its normals edited will show a stale thumbnail. This is acceptable for v1; if it becomes a problem, add normals to the hash input.

### Decision 2: Disk cache location — `Resource/.thumbcache/`

**Choice:** `FileUtil::GetAbsPath("Resource/.thumbcache/")`. Created on first use with `std::filesystem::create_directories`. Files written as `furye_<hex>.png` via `stbi_write_png`.

**Why under `Resource/`:**
- Already resolved by `FileUtil::GetAbsPath()` against the engine's working directory (no new path-resolution code).
- Sits alongside other engine-managed cache directories (the engine already writes to `Resource/Scene/` and `Resource/Shader/`).
- Survives across sessions (the user's working directory is stable).
- Easy to inspect / clear manually (just delete the folder).
- The `.thumbcache` name mirrors the OS thumbnail-cache convention (Windows Explorer uses `.thumbcache_*`).

**Alternatives considered:**
- `std::filesystem::temp_directory_path()` (OS temp dir) — rejected: temp dir is periodically cleared by the OS, defeating the "warm cache across sessions" goal. Also platform-variable (`/tmp` vs `~/Library/Caches/...` vs `%LOCALAPPDATA%\Temp`), adding testing complexity.
- `~/Library/Caches/Fury/` (platform-specific user cache) — rejected: doesn't travel with the project; user opening the project from a different machine wouldn't see the cache.
- Inside the user's home `~/.fury/thumbcache/` — rejected: same problem.
- A user-configurable path — rejected: not requested; adds a config surface for v1 that we don't need.

**Risk:** The folder may be write-protected (rare for `Resource/`). The write path is wrapped in try/catch and silently degrades to in-memory-only caching — the editor still works, just no disk warm-up next session.

### Decision 3: Async split — hash + PNG encode off-thread, GL on main

**Choice:**
- **Worker thread (`ThreadUtil::Enqueue`)**: compute the 64-bit FNV-1a content hash over `mesh->Positions.Data + mesh->Indices.Data + submesh indices`. This is a CPU-only read-only scan over `std::vector` bytes — safe to do off-thread because the data is not being mutated while the worker runs (the mesh is in its clean state — we only enqueue the hash when `mesh->GetDirty()` transitioned to false last frame).
- **Main thread, when worker callback fires**: look up the disk path `furye_<hex>.png`. If it exists, load it via `Texture::CreateFromImage(path, srgb=false, mipMap=false)` and blit into the thumbnail FBO. If it doesn't exist, run `RenderMeshToThumbnail` (the existing helper) to render into the FBO, then `glReadPixels` + `stbi_write_png` to write the PNG.
- **PNG encode off-thread**: `stbi_write_png` does both encoding and file I/O. To move it off the main thread, we'd need to capture the pixel buffer into a `std::vector<unsigned char>` (allocated on the main thread after `glReadPixels`), enqueue a worker task that calls `stbi_write_png` on the captured buffer, then drop the buffer. This is worth doing because PNG encode for a 128×128 image is ~5-10ms — noticeable as a frame hitch if done inline.

**Final flow per mesh thumbnail request:**
1. `GetMeshThumbnail(mesh)` called from Content Browser every frame.
2. Look up `g_MeshThumbnails[BufferId]`. If entry exists and `entry.contentHash` matches the last computed hash and `mesh->GetDirty() == false`, return `entry.colorRT->GetID()` immediately (fast path — 99% of frames).
3. If the mesh was dirty last frame and is clean this frame (dirty-transition): enqueue a hash task on `ThreadUtil`. Mark `entry.hashInFlight = true` so we don't enqueue duplicates. Return the existing (or gray-fallback) texture for this frame.
4. When the hash task completes (main-thread callback): store `entry.contentHash`. Check disk for `furye_<hex>.png`. If exists, load and blit into `entry.colorRT`. If not, render into `entry.colorRT` via `RenderMeshToThumbnail`, then `glReadPixels` into a `std::vector<unsigned char>`, enqueue a second worker task that calls `stbi_write_png(path, w, h, 4, pixels.data(), row_stride)`. Clear `entry.hashInFlight`.
5. The next frame's `GetMeshThumbnail` call sees a matching hash + dirty=false and hits the fast path.

**Why this split:**
- GL work can't leave the main thread (context is thread-affine).
- Hashing is the most expensive CPU step (scans MB of vertex data) — must be off-thread to avoid frame hitches when many meshes change at once.
- PNG encode is the second-most expensive step — also off-thread.
- The disk file existence check is fast (one `std::filesystem::exists` call) and stays on the main thread to avoid race conditions with the write step.

**Alternatives considered:**
- All on main thread (simpler, matches the existing dead code) — rejected: hashing 1MB of vertex data + PNG encode is 15-20ms; if 10 meshes change in the same frame, that's a 200ms hitch. The user explicitly asked for async.
- Render off-thread via shared GL context — rejected: SFML/GL context sharing is fragile and not currently used in the engine. Not worth it for thumbnails.
- Spread the render over multiple frames (like `EditorPicking`) — rejected: a 128×128 single-mesh render is sub-millisecond; the bottleneck is hashing + encode, not the render.
- Skip PNG entirely, cache raw RGBA bytes — rejected: 128×128×4 = 64KB per mesh; caching 1000 meshes = 64MB of disk for a feature the user wanted to be lightweight. PNG compresses that to ~5-15KB each.

### Decision 4: Dirty-transition trigger, not per-frame hash

**Choice:** Re-enqueue the hash task ONLY when `mesh->GetDirty()` transitions from `true` (last frame) to `false` (this frame). This indicates the mesh's vertex data was just uploaded to the GPU via `UpdateBuffer()` — the content may have changed.

**Why:** `Buffer::m_Dirty` is set by `SetDirty()` (called whenever vertex data changes) and cleared after `UpdateBuffer()` (called by `Shader::BindMesh` or explicitly). The transition is a single reliable signal that "the mesh was edited". Hashing every frame would be wasteful; hashing only on the transition catches every meaningful change.

**Edge case — mesh that is dirty every frame (e.g. animated):** An always-dirty mesh never hits the transition (it's dirty on both sides). Such meshes would never re-hash. This is acceptable for v1: animated meshes (skinned characters, vertex-animated water) have thumbnails that drift out of sync, but the user can right-click → Refresh to force a re-render. A future improvement could add a periodic re-hash (every N seconds) for always-dirty meshes.

**Alternatives considered:**
- Hash every N frames regardless of dirty — rejected: wasteful for static meshes (the common case).
- Hash on every `BufferId` change — rejected: doesn't catch in-place edits (the whole problem we're solving).
- Add an explicit `Mesh::InvalidateThumbnail()` API — rejected: pushes the burden onto every edit site; too easy to miss.
- Hash on every `mesh->SetDirty()` call (callback) — rejected: would re-hash mid-edit (before the user finished dragging a slider), causing rapid re-hash thrash.

### Decision 5: Simple Lambert look for both thumbnails and editor preview

**Choice:** Both the 128×128 thumbnail and the editor preview use the same inline flat-shaded Lambert shader (the existing `GetThumbnailShader` at `EditorAssetWindows.cpp:113-139`). The shader uses:
- Fixed light direction `(0.4, 0.8, 0.3)` normalized.
- Half-lambert floor of 0.2 (so back-faces aren't pure black).
- Flat `0.7` grey albedo.
- Opaque black background (`glClearColor(0.0, 0.0, 0.0, 1.0)`) — matches the 3D scene viewport so the editor preview / Content Browser tile look consistent with the main render.
- Robust against missing or zero vertex normals: when `length(v_normal) < 0.0001` (glTF primitives without a normal attribute), the shader falls back to `vec3(0, 1, 0)` so the fragment is shaded (mid-grey) instead of producing NaN/white.

**Why not the mesh's first material:**
- The user explicitly asked for a simple Lambert material in the previews — a consistent look across all mesh previews.
- The mesh's first material may be PBR-textured, animated, or transparent — rendering it in a tiny preview window is unreliable (textures may not be loaded, transparency may produce artifacts on a black background, etc.).
- A consistent look makes visual identification faster (the user's mental model is "what shape is this mesh?", not "what does this mesh look like with its current material?").
- The mesh's material is still rendered in the main scene viewport — that's the place for material-faithful rendering.

**Why 0.7 grey instead of pure white:**
- Pure white would clip on lit faces (N·L = 1 → 1.0 white) and vanish against a white background on back-faces.
- 0.7 grey keeps lit faces at 0.7 (clearly grey) and back-faces at 0.14 (clearly dark grey), giving a wide tonal range against the black background that reads cleanly at 64×64 thumbnail size.

**Why black background (not white):**
- The user asked for the preview to match the 3D scene's viewport (which is black) so the user can see the same look in the editor as in the main render.
- A black background makes the silhouette pop (mid-grey mesh on black is high contrast).
- Original Decision 5 spec'd a "white-box" look; the user revised this in feedback after seeing the white preview, asking for black to match the 3D scene.

**Why robustness against missing normals:**
- Some glTF primitives arrive without vertex normals (e.g. meshes exported without normal data, or primitives that share a vertex but not a normal buffer).
- `normalize(vec3(0))` is undefined on most GPUs (returns NaN).
- A naive shader would write NaN to the FBO, which clamps to white in 8-bit color — making the entire mesh invisible against any background.
- The `length(v_normal) < 0.0001` check catches this and substitutes a fixed up vector, which gives a uniform mid-grey shading (lit faces = 0.7 * 0.65 ≈ 0.45, back-faces = 0.7 * 0.2 = 0.14) — still a readable silhouette.

**Alternatives considered:**
- Use `GBufferNoTexture.glsl` (the engine's existing no-texture GBuffer pass) — rejected: it's tied to the deferred pipeline's MRT layout (outputs gbuffer_diffuse + gbuffer_light), incompatible with a simple forward FBO render.
- Use the mesh's first material — rejected (see above).
- Synthesize normals from cross products in the vertex shader (recompute face normals) — rejected: needs a uniform buffer of triangle vertex indices, complexity not justified for thumbnails.
- Render at a higher resolution and downsample — rejected: 128×128 is sufficient for a 64×64 tile (2x supersampling for downscale AA).

### Decision 6: Two-pane Mesh editor layout via `ImGui::BeginChild` + `SameLine`

**Choice:** Restructure `RenderMeshEditorWindow` (`EditorAssetWindows.cpp:627-673`) into:
- Left pane (~70% width): the 3D viewer (`RenderMeshPreview`), filling the available height.
- Right pane (~30% width, min 240px): a `ImGui::BeginChild("metadata", ...)` containing the existing `RenderMeshMetadata` block.

Implemented via:
```cpp
float sidebar_width = std::max(240.0f, avail.x * 0.30f);
float viewer_width = avail.x - sidebar_width - 8.0f;
RenderMeshPreview(mesh, popup_id, ImVec2(viewer_width, avail.y), display_mesh);
ImGui::SameLine();
ImGui::BeginChild("metadata", ImVec2(sidebar_width, avail.y), true);
RenderMeshMetadata(mesh, popup_id);
ImGui::EndChild();
```

**Why `BeginChild` + `SameLine` (not a second dockable window):**
- A second dockable window (`"Mesh Metadata: <name>"`) would require tracking open-state, lifetime, and docking separately per mesh — complexity the existing one-window-per-asset model doesn't need.
- `BeginChild` keeps the metadata visually attached to the editor window — dragging the editor moves both panes together, which matches user expectations for a single asset editor.
- The existing `RenderMeshMetadata` is already a self-contained function that emits ImGui widgets — wrapping it in `BeginChild` is a one-line change.
- The pane split is resizable at runtime by dragging the divider if we add an `ImGui::Splitter`-style helper, but for v1 a fixed 70/30 split is fine.

**Alternatives considered:**
- Two separate dockable windows — rejected (see above).
- `ImGui::Columns(2)` — rejected: columns are finicky with mixed-height children (the metadata block has collapsible headers that change height, which would reflow the viewer and cause flicker).
- Tabs (`BeginTabBar` with "Viewer" / "Metadata" tabs) — rejected: the user explicitly asked for viewer-left / options-right, not a tabbed switch.

### Decision 7: Camera controls — orbit + dolly + pan (NOT free-fly)

**Choice:** The Mesh editor preview uses orbit-style controls (NOT the main scene's free-fly):
- **LMB drag**: orbit (azimuth + elevation around AABB center).
- **Mouse wheel**: dolly (clamp distance to `[0.1 × initial, 10 × initial]`).
- **RMB drag or MMB drag**: pan (translate the orbit target point in screen-space).

The default camera frames the mesh's AABB to fill ~60% of the shorter axis (using the existing `ComputeInitialDistance`).

**Why orbit, not free-fly (despite user saying "like main scene"):**
- "Like main scene" describes the UX quality (responsive, real-time, with an axis cube), not literally the same control scheme.
- A free-fly camera makes no sense for a 64-unit cube preview — the user can't meaningfully "fly" through a single mesh. Asset viewers (Blender, Unity, Unreal, Godot) universally use orbit/dolly/pan for asset previews.
- The existing `OrbitState` (`EditorAssetWindows.cpp:54-96`) and `ComputeInitialDistance` are already orbit-style — reusing them is zero new code.
- The existing `asset-editor-windows` spec already specifies orbit + zoom + axis-cube; this decision only adds pan (which the existing spec explicitly disallows at line 77). The user's "move around the cam" wording is the basis for reversing that decision.

**Why reverse the spec's "no pan" decision:**
- The user explicitly asked for "move around the cam like main scene", which we interpret as including pan.
- Pan is useful for inspecting off-center submeshes or details that don't fit the AABB center framing.
- Pan is a standard asset-viewer control; users will expect it.

**Alternatives considered:**
- Free-fly (matching `Editor.lua:443-531`'s WASD + drag-look) — rejected (see above).
- Orbit + dolly only (existing spec) — rejected: doesn't satisfy "move around the cam like main scene".
- Orbit + dolly + pan + zoom-to-region (box-select zoom) — rejected: scope creep; can be added later.

### Decision 8: Periodic refresh poll — every 30 frames, scan live tiles

**Choice:** Add a `RefreshMeshThumbnailCache()` function called from `Editor::Tick` every 30 frames (≈0.5s at 60fps). It iterates the currently-visible mesh tiles (reusing `CollectTiles`'s output, cached on the Content Browser), and for each mesh:
- Checks if `mesh->GetDirty()` transitioned since last poll.
- If so, enqueues the async hash + re-render pipeline (Decision 3).

**Why 30 frames:**
- Fast enough to feel responsive (user edits a mesh, sees the thumbnail update within ~0.5s).
- Slow enough to avoid thrash (hashing is off-thread, but the main-thread lookup + disk check still costs ~0.1ms per mesh — 100 meshes × 0.1ms = 10ms per poll, acceptable at 0.5s intervals).
- The Content Browser already runs `EvictStaleMeshThumbnails` every frame (cheap), so eviction is not rate-limited — only the dirty-check + hash-enqueue is.

**Alternatives considered:**
- Every frame — rejected: wasteful; the dirty-transition signal means we don't need per-frame polling.
- On-demand only (when `GetMeshThumbnail` is called) — rejected: meshes that scroll off-screen and back on wouldn't get re-checked; the user would see a stale thumbnail until they interact.
- On `Editor::FrameSelection`-style event — rejected: doesn't cover edits made via the Node Properties panel or external scripts.
- Subscribe to `Buffer::OnDirty` signal — rejected: no such signal exists in the engine; adding one is more invasive than polling.

### Decision 9: Disk-cache warm-up on startup

**Choice:** On `Editor::Initialize` (or first `Editor::Tick`), kick off a one-shot scan of `Resource/.thumbcache/` to populate an in-memory `std::unordered_set<std::string> g_DiskCacheIndex` of filenames present. This avoids a `std::filesystem::exists` call per mesh per session (which would be ~0.5ms × N meshes on first hit).

**Why index instead of stat-on-demand:**
- `std::filesystem::exists` is ~0.1-0.5ms per call on macOS APFS — acceptable for a single call but adds up across many meshes.
- The index is built once (one directory iteration, ~1ms for a typical cache folder) and consulted in O(1) per mesh.
- The index can be invalidated / refreshed lazily — when a write succeeds, add the filename to the index; when eviction drops an entry, optionally remove the file from disk and the index.

**Alternatives considered:**
- Stat-on-demand only — rejected: slower for large asset libraries.
- Persist the index to a manifest file — rejected: over-engineering; the directory scan is fast enough.
- No warm-up, build the index lazily as meshes are requested — rejected: causes a stutter on first session frame as the first batch of meshes all stat the disk simultaneously.

### Decision 10: `fury render-mesh` CLI subcommand for visual debugging

**Choice:** Add a new CLI subcommand `fury render-mesh <scene> <mesh_name> <output.png>` that loads a scene, finds the named mesh, renders it to a 256×256 offscreen FBO using the same simple Lambert shader the editor uses, and writes a PNG.

**Why:**
- The user reported that both the editor preview and the Content Browser tile were pure white (a shader bug from missing/zero vertex normals). They asked for a way to test the mesh-rendering pipeline without launching the editor, so they can iterate on shader / camera math / FBO setup and verify the result as a static image.
- The CLI path bypasses the editor's dockspace, Lua VM, and ImGui, but still needs a GL context for the FBO render. SFML gives us a GL context for free if we create a window — even a hidden one.
- Reuses the existing scene-loading dispatch (`Cli::LoadSceneForExec`) so the same `.json` / `.bin` / `.gltf` / `.glb` / `.fbx` support works.
- Reuses the same camera math (orbit at az=30°, el=20°, distance from AABB via `ComputeInitialDistance`) as the editor's preview, so the static PNG matches what the user sees in the editor's Mesh window.

**Why not a Lua script (`fury exec scene.json render_mesh.lua T90 /tmp/out.png`):**
- The user asked for a "cli option", implying a built-in subcommand, not a script.
- A Lua script would require a Lua binding for the offscreen FBO render (currently editor-only).
- The C++ implementation is ~100 lines of straightforward code; a Lua binding would be more code for the same capability.

**Why a hidden 256×256 window (not a larger one):**
- The user doesn't see the window; the FBO is the output.
- 256×256 is enough to verify the silhouette and shading without using excess VRAM.
- A smaller window opens faster and tears down faster.

**Alternatives considered:**
- Add a new launcher flag `--render-mesh` (mirroring `--screenshot`) — rejected: launcher flags are stripped before Lua's `arg` table, which is the convention for runtime options. `render-mesh` is a full subcommand, not a runtime option.
- Have the user manually launch the editor and use a screenshot — rejected: that's what they were trying to avoid.
- Skip the FBO and render directly to the back buffer, then `glReadPixels` + `stbi_write_png` (same pattern as `Engine::WriteBackBufferAsPng`) — rejected: an offscreen FBO is more flexible (can choose any size, doesn't depend on the visible window's size, can be regenerated without resizing the window).

## Risks / Trade-offs

- **[Risk] Hash collisions cause stale thumbnails.** → Mitigation: 64-bit FNV-1a has ~10^-9 collision probability for realistic mesh counts. If a collision is ever suspected, the user can right-click → Refresh in the Content Browser to force a re-render, or delete the `.thumbcache` folder.
- **[Risk] Disk write fails (read-only `Resource/`, disk full, permissions).** → Mitigation: `stbi_write_png`'s return code is checked; on failure, the in-memory FBO cache still serves the thumbnail for the current session. A `FURYW` log line is emitted once per failed write (rate-limited to avoid spam).
- **[Risk] Race between async hash worker and main-thread mesh edit.** → Mitigation: the worker reads `mesh->Positions.Data` and `mesh->Indices.Data` (read-only scan). If the user edits the mesh while the hash is in flight, the next dirty-transition will enqueue a fresh hash that overwrites the stale result. The worst case is one frame of inconsistency, which is invisible at 60fps.
- **[Risk] Always-dirty meshes never re-hash.** → Mitigation: documented as a known limitation; the user can right-click → Refresh. A future improvement could add a periodic re-hash for always-dirty meshes.
- **[Risk] Disk cache grows unbounded.** → Mitigation: `EvictStaleMeshThumbnails` already drops in-memory entries when the mesh leaves the scene. We extend it to optionally delete the disk file too — but only if the content hash is also absent from any other live mesh (to avoid deleting a file that another mesh is about to reuse). For v1, we do NOT delete disk files on eviction; the user can manually clear the folder. This is a deliberate trade-off: unbounded disk growth is preferable to accidentally deleting a cache entry that's about to be needed.
- **[Risk] Reversing the "no pan" spec decision may break existing user muscle memory.** → Mitigation: the existing spec is at v1 (the comment at `EditorAssetWindows.cpp:623-626` notes the spec's `BeginPopupModal` wording was already overridden to use regular dockable windows). The user's request explicitly asks for pan; we follow the user's request.
- **[Trade-off] 0.7 grey albedo instead of pure white.** → Pure white would clip and lose silhouette readability against a white background. 0.7 grey is a deliberate visual-quality trade-off; users wanting material-faithful rendering can use the main viewport.
- **[Trade-off] No GPU context sharing for off-thread rendering.** → Keeps the GL threading model simple (matches existing engine convention). Thumbnails are small enough that off-thread rendering isn't worth the complexity.
- **[Trade-off] Pan reverses the existing spec's explicit decision.** → We accept this as a deliberate spec modification (filed as a MODIFIED requirement in the delta spec).
- **[Risk] The `BeginChild` + `SameLine` layout may interact poorly with `ImGuiWindowFlags_AlwaysAutoResize` (which the Material editor uses).** → Mitigation: the Mesh editor does NOT use `AlwaysAutoResize` (the spec at `asset-editor-windows/spec.md:14` says "fixed reasonable size ... for the Mesh editor"). The two-pane layout is only used in the Mesh editor, not the Material editor.

## Migration Plan

This is an additive change — no existing user-facing API is removed. The two placeholder stubs are filled in; existing callers see the new behavior automatically. The disk cache folder is created lazily on first write; no setup step is required.

**Rollback strategy:** If the disk cache causes issues (permissions, disk space, performance), set a future `EditorOptions::disable_thumbnail_disk_cache` flag (not in scope for v1) to fall back to in-memory-only caching. For now, manually deleting the `.thumbcache` folder disables disk caching for the next session.

**Spec changes:**
- `editor-shell` spec's "Mesh thumbnail FBO is cached on BufferId" scenario is MODIFIED to add disk-cache warm-up.
- `editor-shell` spec's mesh-thumbnail rendering description is MODIFIED to specify the simple Lambert look.
- `asset-editor-windows` spec's "centered 3D preview with rotate/zoom" requirement is MODIFIED to add pan, the simple Lambert look, the two-pane layout, and the disk-cache hookup.
- New `mesh-thumbnail-disk-cache` capability is ADDED.

## Open Questions

- **Should the disk cache be per-project or global?** Current decision: per-project (under `Resource/.thumbcache/`). If the user works on multiple projects that share meshes (e.g. imported from a common asset library), each project maintains its own cache. This is acceptable for v1; revisit if disk usage becomes a concern.
- **Should we cap the disk cache size?** Current decision: no cap; manual clear is the escape valve. Revisit if users report disk pressure.
- **Should the periodic refresh poll also re-render meshes whose `BufferId` changed but content didn't?** Current decision: no — content hash is the source of truth; `BufferId` change with identical content is a no-op. This avoids unnecessary re-renders when meshes are re-imported from the same source.
- **Should the mesh editor's right-side panel be collapsible / resizable?** Current decision: fixed 30% width, min 240px. A future improvement could add a draggable splitter; out of scope for v1.
- **Should the disk cache index be persisted to disk?** Current decision: no — rebuilt on each startup. Fast enough.
