# add-tracy-profiler — design

## Context

Frame execution is single-threaded: `Engine::Run` (engine/Fury/Engine.cpp:485) runs events, fixed update (25 Hz), Lua-driven render (`Pipeline:Execute` via `cb.OnUpdate`), ImGui flush, and `window.display()` (Engine.cpp:616) on one main thread. Off-main-thread work is a progschj-style pool (`ThreadUtil`, engine/Fury/ThreadUtil.h:28) and Jolt's `JobSystemThreadPool` (PhysicsWorld.cpp:137). There is no existing instrumentation beyond `RenderUtil`'s frame clock (BeginFrame/EndFrame) and the editor's bespoke Profiler window (fps + draw-call counters).

Build facts that shape the integration: C++17; no FetchContent — all third-party is vendored under `engine/ThirdParty` (submodules + copied code); Windows + macOS only; per-config defines `FURY_BUILD_DEBUG` / `FURY_BUILD_PROFILE` (RelWithDebInfo) / `FURY_BUILD_RELEASE`; a custom GLLoader already exports `glQueryCounter` / `GL_TIME_ELAPSED` symbols; single GL context on the main thread; RTTI + exceptions on; MSVC `/bigobj` already set (covers TracyClient.cpp).

## Goals / Non-Goals

**Goals:**
- Zero-cost instrumentation: with `FURY_WITH_TRACY=OFF` every macro is a no-op and no Tracy code is compiled or linked.
- CPU zones + frame marks covering the main loop, physics/animation ticks, render pipeline passes, worker pool, and Jolt jobs.
- Optional real GPU timings (GL timer queries) shown in Tracy's GPU timeline.
- On-demand capture: run normally, connect the profiler UI mid-session, no rebuild needed.
- Agent-friendly validation path (headless capture via Tracy's `tracy-capture` CLI).

**Non-Goals:**
- Memory-allocation tracking, lock profiling, Lua-source zone instrumentation (Tracy supports these; out of scope for v1).
- GPU zones for any API other than the existing OpenGL path.
- Shipping or auto-building the profiler UI with the engine (it is a standalone developer tool).
- CI/continuous profiling.

## Decisions

### D1: Vendor Tracy as a git submodule pinned to a release tag
`engine/ThirdParty/tracy` -> https://github.com/wolfpld/tracy.git, pinned to the latest stable tag at apply time. Rationale: matches the repo's existing pattern (tinygltf, SFML, Jolt are submodules) and makes version bumps a submodule bump. Alternative considered: copying the ~2 needed files (`TracyClient.cpp` + headers) into the tree — rejected: Tracy's header layout (`tracy/Tracy.hpp`, `tracy/TracyOpenGL.hpp`, client sources) is cleaner consumed as the upstream tree, and the missing-submodule guard pattern in engine/CMakeLists.txt:94-117 already handles absent submodules.

### D2: One static lib + one wrapper header
Follow the lua/meshoptimizer pattern: `add_library(tracy STATIC ThirdParty/tracy/public/TracyClient.cpp)` + `include_directories(SYSTEM ThirdParty/tracy/public)`, linked into the Fury targets only when enabled. All engine code includes a new thin header `engine/Fury/Profiler.h` that maps engine macros to Tracy or no-ops:

| Engine macro | Tracy (enabled) | Disabled |
|---|---|---|
| `FURY_ZONE` / `FURY_ZONE_NAMED(name)` | `ZoneScoped` / `ZoneScopedN` | nothing |
| `FURY_FRAME` | `FrameMark` | nothing |
| `FURY_SET_THREAD_NAME(name)` | `tracy::SetThreadName` | nothing |
| `FURY_GPU_ZONE(name)` | `TracyGpuZone` | nothing |

Rationale: keeps `#include <tracy/Tracy.hpp>` and `TRACY_ENABLE` ifdefs out of dozens of engine files; one place to evolve the mapping.

### D3: CMake gating — dev/editor default-on, shipping default-off
`option(FURY_WITH_TRACY ...)` defaults ON for Debug, RelWithDebInfo, and multi-config generators (where the editor is the daily driver — per project direction, profiling should just be there in dev), and OFF for Release/MinSizeRel (shipping builds of the `fury` runtime). Explicit `-DFURY_WITH_TRACY=ON/OFF` always wins. When ON: compile the tracy lib, define `TRACY_ENABLE` and `TRACY_ON_DEMAND` on the Fury targets, link the lib. `TRACY_ON_DEMAND` keeps an enabled build inert (no data collection) until a profiler connects — this is the "runtime toggle without rebuild" mechanism.

### D4: FrameMark after the swap; zones on the proven hot path
`FURY_FRAME` immediately after `window.display()` (Engine.cpp:616), so a Tracy frame = one presented frame. Initial zone set (all `FURY_ZONE*` at function scope):
- Main loop: fixed-update block, `Gui::NewFrame`, `Editor::Tick`, `cb.OnUpdate` (named `Lua+Render`), `Engine::Update`, `Editor::TickPostRender`, `Gui::Render`.
- `PhysicsWorld::TickFixed` (plus inner zones for Jolt `Update`, buoyancy, character ticks), `PhysicsWorld::TickUpdate`/`SyncNodes`.
- `Animator::TickFixed` / `Animator::TickUpdate`.
- `PrelightPipeline::Execute` as the top render zone; inner: `GetRenderQuery`+`Sort` (`Culling`), per-pass zones (`ShadowMaps`, `GBuffer`, `Lighting`, `DrawSky`, `DrawOcean`, transparent pass, `RunPostProcessChain`).
Rationale: coarse zones first — Tracy's sampling + statistics fill in detail; fine-grained per-draw zones would flood the capture.

### D5: GPU zones via TracyOpenGL, behind `FURY_WITH_TRACY_GPU`
Use `tracy/TracyOpenGL.hpp`: create the `TracyGpuContext` once after `gl::LoadGLFunctions()` succeeds, wrap render passes in `FURY_GPU_ZONE`. GL 3.3 core guarantees `ARB_timer_query`; the engine's GLLoader already exports the needed entry points, and the single GL context lives on the main thread where the zones are emitted. Separate CMake option (default ON when `FURY_WITH_TRACY` is ON) so platforms/drivers with flaky timer queries can disable GPU zones without losing CPU profiling. GPU zones are per-pass (same granularity as D4), not per-draw — query objects cost real driver time.

### D6: Thread naming
`FURY_SET_THREAD_NAME("main")` after `ThreadUtil::SetMainThread()` (Engine.cpp:201); `worker-N` inside the ThreadUtil worker loop. Jolt's pool names its own threads at the OS level; Tracy still captures them (unnamed) — naming Jolt threads requires patching Jolt's job system and is deferred (Open Question).

### D7: Tracy tools build with the engine, into `examples/`
Per project direction the profiler UI is a required companion: when `FURY_WITH_TRACY=ON`, a `tracy_tools` ALL target drives two `ExternalProject` builds (`tracy/capture` and `tracy/profiler`, both Release) and copies `tracy-profiler` + `tracy-capture` next to `fury`/`furye` in `examples/`. ExternalProject keeps Tracy's C++20 + CPM-fetched deps (capstone, glfw, freetype) isolated from our C++17 build and guarantees tool/client versions always match the vendored submodule. The tools target is deliberately NOT a dependency of `fury`/`furye`, so target-scoped builds skip it. Versioning note: a profiler UI must match the vendored client version (v0.14.1) or connection is refused — the auto-built tools satisfy this by construction; downloaded release binaries must match the pin.

### D8: Runtime control via an `armed` flag — NOT Tracy's manual lifetime
Tracy's `tracy::StartupProfiler`/`ShutdownProfiler` require `TRACY_DELAYED_INIT` + `TRACY_MANUAL_LIFETIME` and are unsafe for us: after shutdown, `s_profilerData` is freed and any later zone dereferences a null/ freed profiler (zones never stop executing in our loop). Instead, `Profiler.h` owns a process-wide `std::atomic<bool> armed` that feeds every zone macro's `active` parameter (`ZoneNamed`, `TracyGpuZoneTransient`, gated `FrameMark`). A disarmed zone short-circuits before touching Tracy or GL state, and `TRACY_ON_DEMAND` additionally gates everything on an actual client connection. This makes toggling safe at any moment, from any of: `FURY_TRACY=0` env at startup, the editor's persisted `Tracy=0|1` setting (imgui.ini handler, applied via `Engine::SetTracyEnabled`), and the Lua `tracy` option / `Engine.SetTracyEnabled`. Alternative rejected: manual lifetime startup/shutdown (crash-unsafe mid-run); connect-gating only (violates the spec's "connecting profiler receives no zone data" scenario).

## Risks / Trade-offs

- TracyClient.cpp is a heavy TU -> MSVC `/bigobj` already enabled; clang/gcc handle it; only compiled when the option is ON.
- `TRACY_ON_DEMAND` still installs signal handlers / a listening socket in enabled builds -> documented as dev-only config; Release builds keep the option OFF.
- GL timer queries add driver overhead per query -> coarse pass-level zones only (D5); option to disable.
- Zone macros on functions compiled without TRACY_ENABLE must still parse -> wrapper header guarantees syntactically valid no-ops; build both configs in the validation tasks.
- Submodule pin drift between Tracy client and profiler UI (version mismatch refuses connection) -> docs must state "use the profiler release matching the pinned tag"; `tracy-capture` is built from the same submodule so versions always match.
- Headless CLI paths (`Cli::Run`) never init the engine -> intentionally unprofiled; `render-mesh` and windowed paths go through `Engine::Initialize` and are covered.

## Open Questions

- Naming Jolt worker threads (needs a small Jolt-side patch or a custom JobSystem subclass) — defer unless captures show confusing Jolt activity.
- Whether `FURY_BUILD_PROFILE` should also enable `TRACY_CALLSTACK` sampling depth > 0 — start without callstack collection, revisit after first captures.
