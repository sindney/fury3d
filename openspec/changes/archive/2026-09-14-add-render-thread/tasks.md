# Tasks: add-render-thread

## 1. RenderThread plumbing + context handoff

- [x] 1.1 Add `RenderThread` class: bounded SPSC frame queue (depth 2), deferred GL job queue, frame-result mailbox, `Start`/`Stop` with `window.setActive(false/true)` context handoff, `Flush()`, thread named `render` via `FURY_SET_THREAD_NAME`; TracyGpuContext creation moves to the render thread after handoff
- [x] 1.2 Toggle plumbing: `--render-thread=0|1` CLI flag, `FURY_RENDER_THREAD` env, Lua `Engine.run` `render_thread` option, editor ini `RenderThread` setting; resolution order CLI > env > lua > ini > default ON; headless CLI tools (`exec`, `render-mesh`, `info`, `convert`) force OFF
- [x] 1.3 Smoke test (fury player): render thread clears backbuffer, swaps, presents; `FURY_FRAME` executes post-swap on the GL-owning thread; Tracy capture shows `render` thread timeline
- [x] 1.4 Debug-only GL thread-ownership assert (wrap GL entry points or key call sites; assert "on GL-owning thread" after handoff) to catch stray main-thread GL during later stages

## 2. FramePacket + gather (fury player threaded end-to-end)

- [x] 2.1 `FramePacket` struct + double-buffered pool + `FrameResult` mailbox (counters, shadow tex ids, readback pixels, pool slot return)
- [x] 2.2 Extend `RenderUnit` with copied world matrix / world bounds / resolved LOD index; gather phase on game thread: octree `GetRenderQuery` + `Sort` + per-light shadow-caster queries + copies of camera, light params/transforms, RenderSettings, FURY_* debug flags
- [x] 2.3 Instanced packetization: version-gated copy of instance world-matrix/AABB arrays; `BuildVisibleBatches` (per-instance cull + LOD bucketing) runs on the render thread over packet data
- [x] 2.4 Dynamic component packetization: particle billboard vertex/index copies; ocean/sky/terrain parameter copies; their GL resources (LUTs, depth-blit FBOs) become render-thread-owned
- [x] 2.5 Intercept `Pipeline::Execute`: threaded mode = gather + `SubmitFrame` (blocking when full) + execute on render thread; OFF mode = gather + execute inline on main thread (identical code path); pass orchestration reads only packet data, never SceneNode/Component

## 3. GL resource marshaling

- [x] 3.1 Split CPU decode from GL upload for Texture/Mesh/Shader: upload immediately on the GL-owning thread, else enqueue job with payload copy; enqueue GL handle destruction from non-GL threads; pending resources bind engine dummies
- [x] 3.2 `Flush()` at scene/pipeline load completion (editor open/import, Lua scene load), window/viewport resize, and shutdown; `Engine::Shutdown` joins the render thread and re-activates the context on main before GL teardown
- [x] 3.3 Singleton audit: Texture temp pool, InstancedMeshStreamer, RenderUtil dummies, shared shadow pass FBO, DrawOcean depth-blit FBOs, sky LUT FBOs - assert render-thread-only access after handoff

## 4. Static draw-command cache

- [x] 4.1 DrawCommand cache on render thread keyed by (node id, submesh, pass index, LOD index): cached program/VAO/texture binds/index range/state; hit patches per-draw uniforms only; component setters bump render version for invalidation
- [x] 4.2 Instanced units cached per (component id, LOD tier, submesh); per-frame instance stream upload unchanged; Tracy plots for cache hits/rebuilds/draw count

## 5. Editor (furye) threaded support

- [x] 5.1 ImGui split: `ImGui::NewFrame`/window building/`ImGui::Render` on game thread; ImDrawData snapshot (ImDrawDataSnapshot or manual ImDrawList clone) handed to render thread; `ImGui_ImplOpenGL3_NewFrame`/`RenderDrawData` on render thread
- [x] 5.2 Viewport RenderTarget handoff: resize as render-thread job, scene RT produced and sampled by ImGui within the same rendered frame
- [x] 5.3 Picking id-pass + `glReadPixels` on render thread with pixel mailbox (keeps 2-frame contract); screenshots round-trip through the mailbox
- [x] 5.4 Thumbnails + mesh/material 3D previews as render-thread offscreen jobs; results sampled at most one frame later

## 6. Instrumentation, benchmarks, sanitizers

- [x] 6.1 Tracy zones per the tracy-profiling delta (gather zones on `main`, execution zones on `render`) + plots (queue depth, cache hit/rebuild, draw count); capture confirms the threaded split visually
- [x] 6.2 Build stress scene: `fury convert scene` ocean_island.bin -> json, splice multiple fire/smoke particle clusters from outdoor.bin, convert back to .bin under examples/Projects
- [x] 6.3 Benchmark A/B (uncapped `max_fps=false`): island + stress scene, `--render-thread=0|1`, record avg frame times in change notes; confirm no regression on outdoor.bin; capture Tracy timelines for both modes
- [x] 6.4 Add `FURY_WITH_ASAN` CMake option; run island + stress scenes threaded under ASAN; fix all new engine reports
- [x] 6.5 USER visual verify in furye threaded (viewport live, picking correct, gizmos/overlay render, thumbnails populate); then sync specs + archive
