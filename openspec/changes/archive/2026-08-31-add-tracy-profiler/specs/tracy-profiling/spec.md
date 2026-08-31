# tracy-profiling

## Purpose

Optional Tracy profiler integration: vendored Tracy client behind a CMake option, no-op-when-disabled engine macros for CPU zones, frame marks, thread names, and optional OpenGL GPU zones, plus a documented capture workflow. Gives developers nanosecond CPU timing across the main thread, ThreadUtil workers, and Jolt job threads, with GPU pass timings alongside, at zero cost in default builds.

## ADDED Requirements

### Requirement: Tracy instrumentation SHALL be gated behind a CMake option with zero cost when disabled

The build SHALL provide `option(FURY_WITH_TRACY "Enable Tracy profiler instrumentation" ...)`. The default SHALL be ON for development/editor configurations (Debug, RelWithDebInfo, and multi-config generators) and OFF for shipping configurations (Release, MinSizeRel); an explicit `-DFURY_WITH_TRACY=ON/OFF` SHALL always override the default. When OFF, no Tracy translation unit SHALL be compiled, no Tracy library SHALL be linked, and every engine profiling macro SHALL expand to a syntactically valid no-op. When ON, the build SHALL compile Tracy's `TracyClient.cpp` from the vendored `engine/ThirdParty/tracy` submodule, define `TRACY_ENABLE` and `TRACY_ON_DEMAND` on the engine targets, and link the Tracy library.

#### Scenario: Shipping build contains no Tracy code

- **WHEN** the engine is configured with `-DCMAKE_BUILD_TYPE=Release` and no explicit Tracy option
- **THEN** the Fury targets compile and link with no Tracy objects
- **AND** all engine profiling macros compile as no-ops

#### Scenario: Dev/editor builds instrument by default

- **WHEN** the engine is configured with `-DCMAKE_BUILD_TYPE=Debug` (or RelWithDebInfo, or a multi-config generator) and no explicit Tracy option
- **THEN** `FURY_WITH_TRACY` resolves ON and the editor/runtime binaries carry instrumentation

#### Scenario: Explicit override always wins

- **WHEN** the engine is configured with `-DCMAKE_BUILD_TYPE=Debug -DFURY_WITH_TRACY=OFF`, or `-DCMAKE_BUILD_TYPE=Release -DFURY_WITH_TRACY=ON`
- **THEN** the explicit value is used regardless of the config default

#### Scenario: Enabled build compiles and links Tracy

- **WHEN** the engine is built with `FURY_WITH_TRACY=ON`
- **THEN** the build compiles `TracyClient.cpp` from the vendored submodule
- **AND** defines `TRACY_ENABLE` and `TRACY_ON_DEMAND` on the Fury targets
- **AND** the resulting `fury` and `furye` binaries link and run normally without a profiler connected

### Requirement: Engine code SHALL use wrapper macros, never Tracy headers directly

A single header `engine/Fury/Profiler.h` SHALL define the engine profiling macros (`FURY_ZONE`, `FURY_ZONE_NAMED`, `FURY_FRAME`, `FURY_SET_THREAD_NAME`, `FURY_GPU_ZONE`) mapping to Tracy macros when enabled and to no-ops when disabled. Engine sources SHALL include only `Profiler.h`; `#include <tracy/...>` SHALL NOT appear outside `Profiler.h` and the Tracy target itself.

#### Scenario: Macros compile in both configurations

- **WHEN** a source file uses `FURY_ZONE` at function scope
- **THEN** it compiles cleanly both with and without `FURY_WITH_TRACY=ON`

### Requirement: The engine SHALL emit exactly one frame mark per presented frame

`FURY_FRAME` SHALL execute once per main-loop iteration immediately after `window.display()` in `Engine::Run`, so a Tracy frame corresponds to one presented frame.

#### Scenario: Frame timeline appears in captures

- **WHEN** a scene runs windowed with Tracy enabled and a profiler connects
- **THEN** the capture shows one frame marker per presented frame with frame-time spacing matching the observed frame rate

### Requirement: Threads SHALL be named for the profiler

The engine SHALL name the main thread (`main`) at startup and each ThreadUtil worker (`worker-N`) inside the worker loop. Jolt job threads MAY remain unnamed in v1.

#### Scenario: Worker threads identifiable in capture

- **WHEN** a capture is taken while ThreadUtil tasks run
- **THEN** the profiler timeline shows threads labeled `main` and `worker-0..N`

### Requirement: Heavy per-frame functions SHALL carry CPU zones

Zone macros SHALL cover at minimum: the fixed-update block and `PhysicsWorld::TickFixed` (with an inner zone around the Jolt step), `PhysicsWorld::TickUpdate`/`SyncNodes`, `Animator::TickFixed`/`TickUpdate`, the `cb.OnUpdate` Lua+render callback, `PrelightPipeline::Execute` with inner zones for culling (`GetRenderQuery`+`Sort`), shadow passes, gbuffer/opaque pass, transparent pass, `DrawSky`, `DrawOcean`, and `RunPostProcessChain`, plus `Gui::NewFrame`/`Gui::Render` and `Editor::Tick` in the editor.

#### Scenario: Capture shows the frame breakdown

- **WHEN** a capture is taken of a scene with physics, animation, and rendering active
- **THEN** the profiler shows nested zones for update, physics, animation, render pipeline passes, and GUI within each frame

### Requirement: OpenGL GPU zones SHALL be available behind a separate option

When `FURY_WITH_TRACY_GPU` is ON (default ON when `FURY_WITH_TRACY` is ON), the engine SHALL create a Tracy GPU context after GL function loading and wrap the major render passes in `FURY_GPU_ZONE` so GPU pass timings appear in Tracy's GPU timeline. When OFF, GPU zones SHALL compile to no-ops while CPU profiling remains functional. GPU zones SHALL be per-pass, not per-draw-call.

#### Scenario: GPU timeline in capture

- **WHEN** a capture is taken with GPU zones enabled
- **THEN** the profiler shows a GPU context with per-pass GPU timings aligned with the CPU zones

#### Scenario: GPU zones disabled

- **WHEN** the engine is built with `FURY_WITH_TRACY=ON -DFURY_WITH_TRACY_GPU=OFF`
- **THEN** captures show CPU zones and frame marks with no GPU context

### Requirement: Collection SHALL be on-demand and runtime-controllable

Enabled builds SHALL define `TRACY_ON_DEMAND` so no profiling data is gathered until a profiler connects. Setting the environment variable `FURY_TRACY=0` before startup SHALL disable collection entirely for that run. The editor SHALL persist a `Tracy=0|1` entry in its imgui.ini settings registry, mirroring the env override.

#### Scenario: Connect mid-session

- **WHEN** the app is started with Tracy enabled and the profiler connects 30 seconds later
- **THEN** the capture contains data from the moment of connection onward

#### Scenario: Env override disables collection

- **WHEN** the app is started with `FURY_TRACY=0`
- **THEN** a connecting profiler receives no zone data for that run

### Requirement: The capture workflow SHALL be documented and validated end-to-end

`docs/` SHALL contain a page covering: enabling the CMake options, obtaining a profiler UI matching the vendored Tracy version (prebuilt release or building `profiler/`), connecting to a running app, and headless capture via the `tracy-capture` CLI built from the same submodule. The change SHALL be validated by an actual capture of a sample scene showing frame marks, named threads, CPU zones, and GPU zones.

#### Scenario: Headless capture produces a usable trace

- **WHEN** the engine runs a sample scene with Tracy enabled and `tracy-capture` connects and records to a file
- **THEN** the resulting `.tracy` file opens in the profiler UI with frame marks, named threads, CPU zones, and GPU zones present

### Requirement: Tracy tools SHALL build with the engine and land next to the executables

When `FURY_WITH_TRACY=ON` (and the engine builds executables, not shared libs), the default build SHALL also build `tracy-profiler` and `tracy-capture` from the vendored submodule via isolated ExternalProject builds (Tracy's tools are C++20 with their own dependency fetches) and copy both binaries next to `fury`/`furye` in `examples/`. Building only the `fury`/`furye` targets SHALL skip the tools.

#### Scenario: Full build produces the tools

- **WHEN** a Tracy-enabled build completes via the default target
- **THEN** `examples/tracy-profiler` and `examples/tracy-capture` exist and their versions match the vendored submodule

#### Scenario: Target-scoped build skips the tools

- **WHEN** only the `fury` target is built (e.g. `cmake --build build --target fury`)
- **THEN** the Tracy tools are not rebuilt
