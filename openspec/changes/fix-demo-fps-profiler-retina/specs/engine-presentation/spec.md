## ADDED Requirements

### Requirement: Engine SHALL cap the main-loop frame rate

`Engine::Run` SHALL limit the rate at which it issues `window.display()` and emits `OnUpdate` to a configurable target FPS. The cap SHALL default to `144` frames per second. The cap SHALL be configurable via an `EngineCallbacks` companion options field (finalized in design as a single struct passed alongside the callbacks) and from Lua via the second argument of `Engine.run`. Setting the cap to `0` SHALL disable rate limiting entirely (uncapped, the previous behavior). The implementation SHALL use `sf::Window::setFramerateLimit` rather than busy-wait sleeps so that input latency stays bounded by the underlying SFML driver.

#### Scenario: Default cap is 144 FPS

- **WHEN** the demo binary is launched via `./fury Demo.lua` with no `max_fps` field in the options table
- **THEN** the profiler's FPS readout reports a value in the range `[130, 150]` once the scene is steady
- **AND** `Log.txt` records a `DBUG`-level line containing `framerate cap: 144`

#### Scenario: Lua can override the cap

- **WHEN** `Demo.lua` calls `Engine.run({...}, { max_fps = 60 })`
- **THEN** the profiler's FPS readout reports a value in the range `[55, 65]` once the scene is steady

#### Scenario: Lua can disable the cap

- **WHEN** `Demo.lua` calls `Engine.run({...}, { max_fps = 0 })`
- **THEN** no frame-rate limit is applied (the profiler readout is whatever the host can sustain)

### Requirement: RenderUtil SHALL expose previous-frame counter snapshots

`RenderUtil` SHALL accumulate `DrawCall`, `MeshCount`, `TriangleCount`, `SkinnedMeshCount`, and `LightCount` into per-frame working totals during the frame, and SHALL atomically swap the working totals into a "last-frame snapshot" at `EndFrame()`. The existing public getters `GetDrawCall()`, `GetMeshCount()`, `GetTriangleCount()`, `GetSkinnedMeshCount()`, `GetLightCount()` SHALL return the snapshot (i.e., the values as of the most-recently-completed frame), not the in-progress working totals. The reset of working totals SHALL happen at `EndFrame()` (after snapshot), not at `BeginFrame()`, so that any consumer reading the getters at any point during frame N observes the totals from frame N-1.

#### Scenario: Profiler reports non-zero counts for the deferred lighting demo

- **WHEN** the demo binary renders the bundled `scene.bin` for ≥2 frames
- **THEN** the ImGui Profiler panel reports `DrawCall > 0`, `Triangles > 0`, `Mesh > 0`, and `Light > 0`
- **AND** the reported values are stable across consecutive frames (variance ≤10%)

#### Scenario: GUI render order does not affect counter visibility

- **WHEN** `on_update` calls `Gui.ShowDefault → Gui.Render → Pipeline.Execute` (i.e., the GUI is drawn *before* the scene pipeline in the same frame)
- **THEN** the profiler still displays the scene's draw-call and triangle counts (because `Gui.ShowDefault` reads the previous frame's snapshot, not in-progress totals)

### Requirement: Gui SHALL not double-scale on SFML 3 macOS

`Gui::Initialize` SHALL default `m_GlobalScale` (the multiplier passed to `ImGuiStyle::ScaleAllSizes`) to `1.0f`, not the previous SFML-2-era assumption of `2.0f`. `Gui::Initialize` SHALL NOT hardcode `io.FontGlobalScale`. Both the style scale and the font scale SHALL be configurable; the font scale SHALL default to `1.0f`. Under SFML 3.1 the macOS backend hardcodes `highDpi = NO` (see `engine/ThirdParty/SFML/src/SFML/Window/macOS/SFOpenGLView.mm:128`), so the GL framebuffer dimensions are equal to the window dimensions in screen points; the previous 2× scaling produced a double-magnified UI on Retina screens.

#### Scenario: Default Gui scale is 1.0

- **WHEN** `Engine.run({...})` is invoked with no `gui_scale` or `gui_font_scale` in the options table
- **THEN** `Gui::Initialize` is called with `guiScale = 1.0f`
- **AND** `io.FontGlobalScale` is set to `1.0f`

#### Scenario: Lua can override the Gui scale

- **WHEN** `Engine.run({...}, { gui_scale = 1.5, gui_font_scale = 1.5 })` is called
- **THEN** `Gui::Initialize` is called with `guiScale = 1.5f`
- **AND** `io.FontGlobalScale = 1.5f`

### Requirement: Lua Engine.run SHALL accept an optional options table

The Lua binding `Engine.run` SHALL accept either one argument (the callbacks table, as currently shipped) or two arguments (the callbacks table followed by an options table). The options table SHALL support the keys `max_fps` (number, default `144`, `0`/`false` disables), `gui_scale` (number, default `1.0`), and `gui_font_scale` (number, default `1.0`). Unknown keys SHALL be silently ignored (so future options can be added without breaking older scripts). Missing keys SHALL fall back to the defaults defined above.

#### Scenario: Single-argument call is still valid

- **WHEN** `Demo.lua` calls `Engine.run({ on_init = ..., on_update = ..., on_shutdown = ... })`
- **THEN** the engine runs successfully with the documented defaults (144 FPS, 1.0× UI scale)

#### Scenario: Options table is parsed and applied

- **WHEN** `Engine.run({...}, { max_fps = 30, gui_font_scale = 2 })` is called
- **THEN** `Log.txt` records `framerate cap: 30` and `gui_font_scale: 2`
- **AND** the running profiler FPS stabilizes in `[25, 35]`
