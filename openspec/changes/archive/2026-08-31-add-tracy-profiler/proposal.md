# add-tracy-profiler

## Why

The engine has no real profiler — performance work (ocean, terrain, particles, shadows) has been guided by ad-hoc timers and guesswork. Code exploration confirmed the engine is **single-threaded at the frame level** (one main thread runs events, fixed update, Lua-driven rendering, and the swap; aux threads are a ThreadUtil worker pool and Jolt's job pool) — so we currently cannot see where frame time actually goes across the main thread, worker pool, and physics jobs, or whether off-main-thread work is healthy. Tracy (hybrid nanosecond zone + sampling profiler) gives exactly this visibility with near-zero overhead when disabled.

## What Changes

- Vendor Tracy into `engine/ThirdParty` behind a CMake option (`FURY_WITH_TRACY`, default OFF outside Profile config); when OFF every macro compiles to nothing.
- Instrument the CPU: `FrameMark` at the frame boundary (after `window.display()` in `Engine::Run`), named threads (`main`, `worker-N` for the ThreadUtil pool, Jolt job threads), and `ZoneScoped` zones on the heavy per-frame functions (fixed update + physics step, animation tick, scene/culling, `PrelightPipeline::Execute` and its passes, postfx chain, ImGui flush).
- Add optional GPU zones (`FURY_WITH_TRACY_GPU`) feeding real OpenGL timer-query timings (`glQueryCounter`/`GL_TIME_ELAPSED` — symbols already exported by the engine's GLLoader) into Tracy, so GPU rhythm is visible alongside CPU zones on the single GL context.
- Add a runtime/startup toggle so profiling can be enabled without a rebuild (Tracy on-demand mode + editor settings-registry entry), and document how to connect the `tracy-profiler` UI.
- Validate end-to-end: capture a session of a sample scene (ocean_island), confirm zones from main + worker + Jolt threads, frame marks, and GPU zones appear in the profiler UI.

## Capabilities

### New Capabilities

- `tracy-profiling`: Tracy client vendored and wired into the build; CPU zone macros on main/worker/physics threads, frame marks, thread naming; optional OpenGL GPU zones; on-demand capture; documented profiler-UI workflow.

### Modified Capabilities

(none — instrumentation only; no spec-level behavior change to existing capabilities)

## Impact

- **New code**: `engine/ThirdParty/tracy` (vendored), a thin `Profiler.h` wrapper header mapping engine macros (`FURY_ZONE`, `FURY_FRAME`, `FURY_GPU_ZONE`) to Tracy or no-ops, CMake option + target wiring (follows the lua/meshoptimizer vendoring pattern).
- **Existing code touched**: `Engine::Run` loop (FrameMark + top-level zones), `Engine::Initialize`/`Shutdown` (tracy startup/teardown, thread naming), `ThreadUtil` worker loop (thread names), `PhysicsWorld`, `Animator`, `PrelightPipeline` passes (zones + GPU zones), launcher `examples/main.cpp` (enable flag), editor settings registry (`Tracy=0|1`).
- **Dependencies**: Tracy (BSD-3, single `TracyClient.cpp` translation unit); profiler UI is a separate tool the developer runs (prebuilt release or built from `profiler/`), not linked into the engine.
- **Risk profile**: zero-cost when `FURY_WITH_TRACY=OFF` (all macros no-op); small overhead when enabled, acceptable for a dev-only config. C++17/RTTI/exceptions already match Tracy's needs; no new compiler flags required.
