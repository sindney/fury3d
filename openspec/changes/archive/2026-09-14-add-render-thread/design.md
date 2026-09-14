# Design: add-render-thread

## Context

fury3d is immediate-mode all the way down: Lua `on_update` calls `Pipeline::Execute` (PrelightPipeline.cpp:106), which walks the octree, sorts, and issues GL inline (Pass::Bind -> DrawUnit -> glDrawElements). There is no renderer object, no command buffer, no submission boundary. The GL context is an SFML window made current on the main thread at startup (main.cpp:260). GL is also touched outside the pass loop: texture/shader upload during scene+pipeline load, lazy mesh upload on first draw, ImGui new-frame/render, editor thumbnails/3D previews, picking readback (2-frame state machine), screenshot readbacks, and several GL-owning singletons (Texture temp pool, InstancedMeshStreamer, RenderUtil dummies, shared shadow pass FBO).

Goal split (user direction): Unity's render-thread model (simple, one object model, no U/F split) + UE's lesson applied selectively (copy the data the render thread needs; cache static mesh draw commands).

## Goals / Non-Goals

**Goals:**
- Render thread owns the GL context and executes culling-adjacent per-frame render work (LOD select already gathered, instanced batching, sort), all pass execution, ImGui GL, and present.
- Game thread keeps logic/physics/animation/particle sim and gathers a per-frame FramePacket; the render thread never dereferences scene nodes or components.
- Static draw-command cache for opaque + instanced units (UE mesh-drawing-pipeline lite).
- One rendering code path, two schedulings: threading OFF runs the identical packet path synchronously on the main thread (debug switch, and trivially revertible).
- Tracy visibility on both sides of the split; threading-on/off A/B benchmark on ocean_island + a fire/smoke stress scene, uncapped fps.
- ASAN build option and a clean threaded run.

**Non-Goals:**
- No UObject/FScene proxy duplication of the object model.
- No parallel command recording across N worker threads, no DX12/Vulkan-style command lists.
- No jobified culling; the octree walk stays single-threaded.
- `fury exec` (headless, no GL) and `fury render-mesh` / `--screenshot*` CLI tools stay single-threaded (forced off).

## Architecture

```
game thread (main)                     render thread (new, "render")
-----------------                      -----------------------------
events / fixed update / Jolt
Lua on_update (logic)
Engine::Update (particles/anim/physics writeback)
Gui::NewFrame + editor windows
ImGui::Render -> ImDrawData snapshot
GATHER: octree cull -> RenderQuery
        + shadow-caster queries per light
        + copy transforms/params -> FramePacket
SubmitFrame(packet) ---- bounded ----> Drain deferred GL jobs
  (blocks if 1 frame behind)         LOD/batch/sort from packet
                                     all passes + postfx (unchanged code)
                                     offscreen jobs (thumbnail/pick/preview)
                                     ImGui_ImplOpenGL3_RenderDrawData
                                     window.display() -> FURY_FRAME
                       <--- mailbox --- FrameResult (counters, shadow tex ids,
                                     pick pixels, screenshot pixels, pool slot)
```

### FramePacket (plain data, pool-allocated, double-buffered)

- Camera: view/proj, frustum, position.
- RenderQuery extended: RenderUnit gains copied `worldMatrix`, `worldBounds`, resolved `lodIndex`; `node` kept only as an opaque cache key, never dereferenced by the render thread.
- Lights: per light, copied params + world transform + shadow settings + caster unit lists (same copied-unit form). All octree queries happen on the game thread.
- Instanced: per InstancedMeshRender, mesh/material ptrs + instance world-matrix arrays copied only when the component's version bumps (static vegetation: ~never). Per-instance frustum culling and LOD bucketing (BuildVisibleBatches) move to the render thread over the copied arrays.
- Particles: per ParticleRenderer, a copy of the frame's billboard vertex/index data (particles mutate every frame; copy is inherent and bounded).
- Ocean/Sky/Terrain: copied parameter structs; their GL resources (LUTs, FFT textures, FBOs) are render-thread-owned.
- RenderSettings copy + debug flags snapshot (FURY_* read once at gather).
- Editor block (furye only): viewport RT id+size, ImDrawData snapshot, offscreen job list, pending readback requests.

### Sync protocol

- Queue depth 2 (one in-flight, one building). `SubmitFrame` blocks when full -> render thread is at most 1 frame behind, and the game thread throttles naturally on GPU/CPU render-bound scenes.
- Events stay on the main thread (SFML/macOS requirement); only `window.display()` moves.
- Context handoff: init happens on main as today (font atlas, dummies), then `setActive(false)` on main, `setActive(true)` on render thread in `RenderThread::Start`; reversed in `Stop` before `Engine::Shutdown` GL teardown. TracyGpuContext creation moves to the render thread after handoff.
- `Flush()` (drain jobs + frames) at scene/pipeline load completion, window resize, and shutdown. Load-triggered uploads therefore behave exactly as today, at the cost of a stall only during load.
- Readbacks (picking, screenshots) and editor-visible per-frame results travel back through a mailbox consumed on the game thread; the existing 2-frame picking machine already tolerates the latency.

### GL resource marshaling

Rule: after handoff, only the render thread makes GL calls.
- Texture/Mesh/Shader: split CPU decode (any thread, unchanged) from GL upload (render thread). Upload happens immediately when called on the GL-owning thread (init, single-thread mode), otherwise it is enqueued with a copy of the pixel/vertex/source payload. Units referencing a pending resource are skipped (dummy textures already exist for the bind-every-sampler rule).
- Deletes: GL handle destruction enqueued from any thread.
- Lua mesh channel edits (Mesh.SetPositions): enqueue an upload job carrying a copy of the channel arrays.

### Static draw-command cache

Keyed by (nodeId, submesh, passIndex, lodIndex) -> { program, VAO, texture bind list, index range, static blend/depth state }. Built on first submission of a unit in a pass; per frame a hit patches only per-draw uniforms (world matrix, light indices) and submits. Instanced units cache per (componentId, lodTier, submesh) similarly; the per-frame instance stream upload is unchanged. Invalidation: component setters bump a render version; version mismatch drops the entry. Transparent/particle/debug units bypass the cache (dynamic anyway).

## Decisions

1. **Snapshot packet over proxy objects (Unity over UE).** One object model; the render thread reads only packet data. Rationale: fits an immediate-mode codebase - the packet is cut at the single existing choke point (`Pipeline::Execute`), no engine-wide U/F split. Alternative considered: recording raw GL calls into a command stream (true Unity gfx job) - rejected, too much interposition surface for this codebase.
2. **Culling stays on the game thread.** The octree is game-thread state; querying it from the render thread would race `SceneNode::Recompose` (which also re-inserts into the octree). The gather (octree walk + shadow-caster queries + copies) is the "command gathering" phase; it overlaps with the render thread drawing the previous frame, which is where the wall-clock win comes from. Alternative: snapshot bounds and cull on the render thread - more copies, no extra overlap, rejected.
3. **Single code path for on/off.** OFF mode = build packet + immediately execute it on the main thread. No divergent legacy path to rot; debugging a threading artifact is a flag flip. This also makes "migrate to disabled" trivial (flip the default).
4. **ImGui split at the backend boundary.** `ImGui::NewFrame`/window building/`ImGui::Render` on the game thread; `ImGui_ImplOpenGL3_NewFrame`/`RenderDrawData` on the render thread over a snapshotted ImDrawData (ImDrawDataSnapshot or manual ImDrawList clone, depending on vendored ImGui version). The editor viewport works unchanged in ordering: scene render -> RT ready -> ImGui samples it, all on the render thread within one frame.
5. **Editor offscreen renders become jobs.** Thumbnails, mesh/material 3D previews, and the pick id-pass are submitted with the packet and run on the render thread between scene render and ImGui; their textures/pixels are sampled a frame later. Thumbnails are already async (disk cache + ThreadUtil PNG encode) and picking is already a 2-frame machine, so latency is behavior-preserving.
6. **Toggle resolution order:** `--render-thread=0|1` > `FURY_RENDER_THREAD` > Lua `Engine.run` options `render_thread` > editor ini `RenderThread` > default ON. CLI no-window tools force OFF.
7. **Staging inside this change:** (A) fury player threaded end-to-end; (B) furye editor (ImGui snapshot, offscreen jobs, readback mailboxes); (C) draw-command cache; (D) benchmarks + ASAN. Editor remains usable at every stage via the toggle.

## Risks / Trade-offs

- [Hidden GL call sites outside the pass loop] -> audit list exists (texture/mesh/shader upload, ImGui, thumbnails, picking, screenshots, temp pool, streamer, dummies, shared shadow FBO, sky LUTs, ocean depth-blit FBOs); add a debug-only thread-ownership assert in the GL loader wrappers to catch stragglers.
- [Packet misses some per-frame mutable state -> visual lag or stale draws] -> gather checklist per component type; FURY_* debug flags snapshotted; version counters on anything cacheable.
- [macOS/SFML context handoff quirks] -> handoff is two setActive calls bracketing the thread's lifetime; validated first in stage A before any packet work lands (smoke: clear color from render thread).
- [Snapshot copy cost eats the win] -> copies are bounded (units ~2k, instance arrays only on version bump, particle billboards small); Tracy plots on gather/queue depth/draw count to measure; command cache reduces the submit side.
- [Editor 1-frame latency on picking/thumbnails] -> both flows are already frame-latent by design; verify interactively (user verify step).
- [ASAN false sense of security: it does not catch races] -> ASAN for lifetime/UB; queue/mailbox are SPSC with ownership transfer (no sharing), keeps the race surface small enough to review by hand. TSAN can be tried opportunistically but is not a gate (GL drivers trip it).
- [Perf regression in editor-bound light scenes from packet overhead] -> toggle defaults ON but is one flag away; benchmark gate in tasks requires no regression on outdoor.bin and a win on ocean_island.

## Migration Plan

No user/content migration. Behavior with the toggle OFF is bit-identical ordering to today (same call sequence, executed synchronously). Rollback = flip default to OFF; the packet path remains as the single implementation.

## Open Questions

- Vendored ImGui version: has ImDrawDataSnapshot? (apply-time check; fallback = manual ImDrawList buffer clone)
- SFML 3 `display()` from a non-window thread on macOS: expected fine (Cocoa swap is thread-agnostic once the context is current there); stage-A smoke test confirms.
- Editor play mode spawns a detached fury child; that child is just fury, so it inherits the toggle default - confirm no editor<->child coupling breaks.
