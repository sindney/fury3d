# add-tracy-profiler — tasks

## 1. Vendor and build integration

- [x] 1.1 Add `engine/ThirdParty/tracy` as a git submodule (https://github.com/wolfpld/tracy.git) pinned to the latest stable tag; register in `.gitmodules`; add missing-submodule guard in `engine/CMakeLists.txt` following the existing pattern (lines ~94-117)
- [x] 1.2 Add `option(FURY_WITH_TRACY ...)` — default ON for Debug/RelWithDebInfo/multi-config (dev+editor), OFF for Release/MinSizeRel (shipping), explicit flag always wins — and `option(FURY_WITH_TRACY_GPU ... ON)` gated on `FURY_WITH_TRACY` (optional per-machine; unreliable on Apple/TBDR drivers)
- [x] 1.3 Build Tracy as a static lib from `ThirdParty/tracy/public/TracyClient.cpp` (lua/meshoptimizer pattern), `SYSTEM` include `ThirdParty/tracy/public`, define `TRACY_ENABLE` + `TRACY_ON_DEMAND` on Fury targets, link when enabled
- [x] 1.4 Verify all four configs build: Debug/Release without Tracy, RelWithDebInfo with Tracy, `FURY_WITH_TRACY=ON -DFURY_WITH_TRACY_GPU=OFF` (all four built clean on macOS; real exit codes checked)

## 2. Wrapper macros

- [x] 2.1 Create `engine/Fury/Profiler.h` defining `FURY_ZONE`, `FURY_ZONE_NAMED(name)`, `FURY_FRAME`, `FURY_SET_THREAD_NAME(name)`, `FURY_GPU_ZONE(name)` mapping to Tracy macros when `TRACY_ENABLE`, no-ops otherwise
- [x] 2.2 Confirm no `#include <tracy/...>` appears in engine sources outside `Profiler.h`

## 3. CPU instrumentation

- [x] 3.1 Startup/teardown: name thread `main` after `ThreadUtil::SetMainThread()` (Engine.cpp:201); Tracy startup/shutdown hooks in `Engine::Initialize`/`Engine::Shutdown` honoring `FURY_TRACY=0` env
- [x] 3.2 `FURY_FRAME` after `window.display()` (Engine.cpp:616)
- [x] 3.3 Name ThreadUtil workers `worker-N` inside the worker loop (ThreadUtil.cpp)
- [x] 3.4 Zones on main loop phases: fixed-update block, `Gui::NewFrame`, `Editor::Tick`, `cb.OnUpdate` (named `Lua+Render`), `Engine::Update`, `Editor::TickPostRender`, `Gui::Render`
- [x] 3.5 Zones on `PhysicsWorld::TickFixed` (+ inner Jolt-step zone), `TickUpdate`/`SyncNodes`, `Animator::TickFixed`/`TickUpdate`
- [x] 3.6 Zones in `PrelightPipeline::Execute`: culling (`GetRenderQuery`+`Sort`), shadow passes, gbuffer/opaque, transparent, `DrawSky`, `DrawOcean`, `RunPostProcessChain`

## 4. GPU zones

- [x] 4.1 Create the Tracy GL GPU context (`TracyGpuContext`) once after `gl::LoadGLFunctions()` succeeds, guarded by `FURY_WITH_TRACY_GPU`
- [x] 4.2 Wrap the major render passes (same granularity as 3.6) in `FURY_GPU_ZONE`
- [x] 4.3 Verify a build with `FURY_WITH_TRACY_GPU=OFF` still profiles CPU correctly (capture of the GPU-off binary: 903 frames, 21,024 zones, full CPU zone set)

## 5. Runtime control and editor surface

- [x] 5.1 `FURY_TRACY=0` env check at `Engine::Initialize` (follows `FURY_COMPUTE_SHADER` precedent)
- [x] 5.2 Editor settings-registry entry `Tracy=0|1` in the imgui.ini handler (Editor.cpp:200-274 pattern), mirroring the env override
- [x] 5.3 Optional: `tracy` option in the Lua `Engine.run` options table

## 6. Docs and validation

- [x] 6.1 Write `docs/TRACY.md`: enabling the options, matching-version profiler UI (prebuilt release or building `profiler/`), connecting, headless `tracy-capture` workflow
- [x] 6.2 Build Tracy's `tracy-capture` CLI from the submodule — superseded by project direction: tools now build automatically via ExternalProject (`tracy_tools` target) and land in `examples/` as `tracy-profiler` + `tracy-capture`
- [x] 6.3 Validation capture: `ocean_island` recorded via `tracy-capture` (1003 frames, 23,382 zones over 8.17s), contents verified via `tracy-csvexport` (frame marks, `Culling`/`Execute`/per-pass dynamic zones, `Jolt::Update`, `DrawSky`/`DrawOcean`, shadow zones); trace + zone CSV + run screenshot saved to `screenshots/tracy/`. FURY_TRACY=0 disarmed run verified: 0 zones recorded. GPU zones: compiled in and armed, but produce no events on this Mac (Apple/TBDR GL_TIMESTAMP zero precision - Tracy's documented limitation, see docs/TRACY.md); GPU timeline confirmation needs a Windows GL 4.3+ run
- [x] 6.4 Verify zero overhead claim: Release Tracy-OFF objects contain zero tracy symbols (`nm` check on Engine.cpp.o / PrelightPipeline.cpp.o); Tracy-enabled binary carries 514 tracy symbols; armed capture ran at ~123 fps against the 144 fps cap with ~23 zones/frame (sub-ms overhead)
