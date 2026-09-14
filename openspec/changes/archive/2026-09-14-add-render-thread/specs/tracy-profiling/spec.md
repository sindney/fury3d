# tracy-profiling

## MODIFIED Requirements

### Requirement: The engine SHALL emit exactly one frame mark per presented frame

`FURY_FRAME` SHALL execute exactly once per presented frame, immediately after the swap (`window.display()`) on the GL-owning thread: the main thread in single-threaded mode (`Engine::Run`), or the render thread when render threading is enabled, so a Tracy frame always corresponds to one presented frame.

#### Scenario: Frame timeline appears in captures

- **WHEN** a scene runs windowed with Tracy enabled and a profiler connects
- **THEN** the capture shows one frame marker per presented frame with frame-time spacing matching the observed frame rate

#### Scenario: Frame marks follow the render thread

- **WHEN** a capture is taken with render threading enabled
- **THEN** frame marks appear on the `render` thread timeline, one per presented frame

### Requirement: Threads SHALL be named for the profiler

The engine SHALL name the main thread (`main`) at startup and each ThreadUtil worker (`worker-N`) inside the worker loop. When render threading is enabled, the render thread SHALL be named (`render`) at thread start. Jolt job threads MAY remain unnamed in v1.

#### Scenario: Worker threads identifiable in capture

- **WHEN** a capture is taken while ThreadUtil tasks run
- **THEN** the profiler timeline shows threads labeled `main` and `worker-0..N`

#### Scenario: Render thread identifiable in capture

- **WHEN** a capture is taken with render threading enabled
- **THEN** the profiler timeline shows a thread labeled `render` carrying the pipeline pass zones

### Requirement: Heavy per-frame functions SHALL carry CPU zones

Zone macros SHALL cover at minimum: the fixed-update block and `PhysicsWorld::TickFixed` (with an inner zone around the Jolt step), `PhysicsWorld::TickUpdate`/`SyncNodes`, `Animator::TickFixed`/`TickUpdate`, the `cb.OnUpdate` Lua callback, the frame gather (octree render query, sort, shadow-caster gather, FramePacket build, SubmitFrame wait), pipeline execution (`PrelightPipeline::Execute` with inner zones for culling/batching, shadow passes, gbuffer/opaque pass, transparent pass, `DrawSky`, `DrawOcean`, and `RunPostProcessChain`), plus `Gui::NewFrame`/`Gui::Render` and `Editor::Tick` in the editor. When render threading is enabled, gather zones SHALL appear on the game thread and pipeline execution zones SHALL appear on the render thread.

#### Scenario: Capture shows the frame breakdown

- **WHEN** a capture is taken of a scene with physics, animation, and rendering active
- **THEN** the profiler shows nested zones for update, physics, animation, render pipeline passes, and GUI within each frame

#### Scenario: Capture shows the threaded split

- **WHEN** a capture is taken with render threading enabled
- **THEN** gather/update zones appear on `main` and pass/submission zones appear on `render`, with the two timelines overlapping across frame boundaries
