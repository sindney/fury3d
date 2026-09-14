# render-thread

## ADDED Requirements

### Requirement: The engine SHALL support a two-thread rendering pipeline where the render thread owns the GL context

When render threading is enabled, the engine SHALL run game-thread work (events, fixed update/physics, Lua logic, particle/animation/physics writebacks, ImGui window building) on the main thread and all rendering work (instanced batching, sorting, all pipeline passes, postprocess, ImGui GL submission, present) on a dedicated render thread. The GL context SHALL be deactivated on the main thread and activated on the render thread at render-thread start, and returned to the main thread before shutdown GL teardown. After the handoff, the game thread SHALL NOT issue GL calls; all GL object creation, upload, use, and destruction SHALL execute on the render thread.

#### Scenario: Threaded run renders correctly

- **WHEN** `fury` runs a scene with render threading enabled (default)
- **THEN** frames render identically to single-threaded mode
- **AND** Tracy capture shows pipeline pass zones executing on a thread named `render`, separate from `main`

#### Scenario: Context handoff on start and stop

- **WHEN** the render thread starts and stops (including app shutdown)
- **THEN** the GL context is current on exactly one thread at a time
- **AND** GL teardown in `Engine::Shutdown` completes without errors after the render thread is joined

### Requirement: Frame data SHALL cross threads as a copied snapshot (FramePacket)

Per frame the game thread SHALL gather a FramePacket containing, at minimum: camera view/proj/frustum/position; render units with copied world matrices, world bounds, and resolved LOD indices; per-light copied parameters, world transforms, and shadow-caster unit lists; instanced component mesh/material references plus instance transform arrays copied on version change; particle billboard vertex/index data copies; ocean/sky/terrain parameter copies; a RenderSettings copy; and a snapshot of debug flags. The render thread SHALL NOT dereference SceneNode or Component objects; node/component pointers may travel only as opaque keys. Packets SHALL come from a small pool (double-buffered) whose slots are recycled through the frame-result mailbox.

#### Scenario: Game thread mutates while render thread draws

- **WHEN** the game thread moves nodes, dirties transforms, or edits Lua state while the render thread is mid-frame
- **THEN** the render thread's in-flight frame is unaffected (no tearing, no crash under ASAN)

#### Scenario: Packet pool recycles without leaks

- **WHEN** a scene runs threaded for thousands of frames
- **THEN** packet allocation reaches steady state (no per-frame heap growth)

### Requirement: Frame overlap SHALL be bounded to one frame

The frame queue SHALL hold at most one in-flight packet and one being built. `SubmitFrame` SHALL block the game thread when the render thread has not consumed the previous packet, so the render thread runs at most one frame behind. Window event pumping SHALL remain on the main thread; only swap/present moves to the render thread.

#### Scenario: Back-pressure under GPU-bound load

- **WHEN** a GPU-bound scene runs threaded with uncapped framerate
- **THEN** the game thread waits in SubmitFrame rather than queuing unbounded work
- **AND** memory stays flat

### Requirement: A single rendering code path SHALL serve threaded and single-threaded modes

The engine SHALL implement rendering once - packet build, then packet execution. With render threading disabled, the same packet path SHALL execute synchronously on the main thread. The toggle SHALL resolve in order: `--render-thread=0|1` CLI flag, then `FURY_RENDER_THREAD` env var, then the Lua `Engine.run` `render_thread` option, then the editor ini setting, defaulting to ON. Headless/no-window CLI tools (`exec`, `render-mesh`, `info`, `convert`) SHALL force single-threaded mode.

#### Scenario: Toggle off restores synchronous behavior

- **WHEN** a scene runs with `--render-thread=0` or `FURY_RENDER_THREAD=0`
- **THEN** all rendering executes on the main thread in the same call order as before this change
- **AND** no render thread is spawned

#### Scenario: Toggle on is the default

- **WHEN** `fury` or `furye` runs with no toggle overrides
- **THEN** the render thread is used

### Requirement: Static geometry draw submission SHALL use a cached draw-command path

The render thread SHALL cache per-pass draw commands for static units, keyed by (node id, submesh, pass index, LOD index), storing at minimum the resolved shader program, vertex array, texture bind list, index range, and static render state. A cache hit SHALL patch only per-draw uniforms (world matrix and other per-frame values) before submission. Component mutation (mesh, material, transform-affecting state) SHALL bump a render version that invalidates affected entries. Instanced units SHALL cache per (component id, LOD tier, submesh) with the per-frame instance stream upload unchanged. Transparent and particle units MAY bypass the cache.

#### Scenario: Cache hit rate on a static scene

- **WHEN** a camera flies through ocean_island.bin (static vegetation) with threading enabled
- **THEN** Tracy plots show draw-command cache hits dominating rebuilds after warmup
- **AND** rendered output matches the uncached path

#### Scenario: Material edit invalidates

- **WHEN** a material on a rendered mesh is changed at runtime (editor or Lua)
- **THEN** subsequent frames use the new material (stale cache entries are dropped)

### Requirement: GL resource uploads and destruction SHALL be marshaled to the GL-owning thread

Texture, mesh, and shader GL uploads SHALL execute immediately when requested on the GL-owning thread and SHALL be enqueued as render-thread jobs otherwise. GL handle destruction SHALL be enqueued when initiated from any non-GL thread. Scene/pipeline loads triggered from the game thread (editor open/import, Lua scene load) SHALL end with a render-thread flush so post-load behavior matches single-threaded mode. Draws referencing a not-yet-uploaded resource SHALL bind the engine dummy resources instead of crashing.

#### Scenario: Editor opens a scene while rendering

- **WHEN** furye loads a scene file mid-session with threading enabled
- **THEN** all texture/shader/FBO GL work happens on the render thread
- **AND** the first rendered frame after load is complete and correct

#### Scenario: Lua mesh edit while threaded

- **WHEN** a Lua script rewrites mesh vertex channels via `Mesh.SetPositions` during threaded rendering
- **THEN** the upload executes on the render thread and the new data appears without data races

### Requirement: Readbacks and per-frame render results SHALL return through a mailbox

Picking pixel readbacks, screenshot readbacks, draw counters, and editor-visible per-frame texture ids (e.g. shadow map previews) SHALL be posted from the render thread to a game-thread-consumed mailbox. Synchronous CLI-style consumers (screenshot flags) MAY round-trip with a wait. The editor picking flow SHALL keep its two-frame state machine contract.

#### Scenario: Picking works threaded

- **WHEN** the user clicks a node in the furye viewport with threading enabled
- **THEN** the correct node is selected within the same user-perceptible latency as single-threaded mode

#### Scenario: Screenshot works threaded

- **WHEN** fury runs with `--screenshot` (or `--screenshot-series`) and threading enabled
- **THEN** the written PNG matches single-threaded output

### Requirement: The editor SHALL render through the render thread

With threading enabled, furye SHALL build ImGui on the game thread and submit ImGui GL rendering on the render thread from a snapshotted ImDrawData. The scene viewport SHALL render into its RenderTarget on the render thread and be sampled by ImGui within the same rendered frame. Thumbnail and 3D-preview renders SHALL execute as render-thread offscreen jobs, with results sampled at most one frame later.

#### Scenario: Editor smoke

- **WHEN** furye runs ocean_island.bin threaded
- **THEN** the viewport updates live, gizmos/selection overlay render, thumbnails populate, and no GL errors occur

### Requirement: The threaded pipeline SHALL carry Tracy instrumentation on both threads

The render thread SHALL be named for the profiler. Zones SHALL cover, at minimum: game-side gather (octree query, shadow-caster gather, packet build, SubmitFrame wait), and render-side frame execution (packet consume, instanced batching, per-pass execution, ImGui GL, present). Tracy plots SHALL report frame-queue depth, draw-command cache hits and rebuilds, and submitted draw count. Frame-mark placement follows the modified tracy-profiling requirements.

#### Scenario: Threading is visible in a capture

- **WHEN** a Tracy capture is taken of a threaded run
- **THEN** `main` and `render` timelines both advance with the zones above
- **AND** overlap between game update and render submission is directly observable

### Requirement: Benchmarks SHALL demonstrate the change on representative scenes

The change SHALL ship a stress scene derived from ocean_island.bin with multiple fire/smoke particle clusters transplanted from outdoor.bin (scene files round-tripped via `fury convert scene`). The benchmark procedure SHALL use uncapped framerate (`max_fps = false`) and compare threading on vs off (`--render-thread=0|1`) on ocean_island.bin and the stress scene, recording average frame times (fps log or Tracy statistics) in the change notes. Threaded mode SHALL NOT regress frame time on outdoor.bin and SHALL improve or match it on the island/stress scenes.

#### Scenario: On/off A/B numbers recorded

- **WHEN** the benchmark runs are performed
- **THEN** the change's notes contain the measured on/off frame times for island and stress scenes

### Requirement: An ASAN build option SHALL exist and the threaded path SHALL pass it

The build SHALL provide an AddressSanitizer configuration (CMake option or preset) for the engine and examples targets. Running the island and stress scenes threaded under ASAN SHALL complete without reports attributable to the engine's threading changes.

#### Scenario: ASAN run is clean

- **WHEN** the stress scene runs threaded under the ASAN build for a fixed frame count
- **THEN** ASAN reports no new errors in engine code
