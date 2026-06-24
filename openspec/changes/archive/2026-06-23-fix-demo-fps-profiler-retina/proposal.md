## Why

The first Lua-driven run of the demo surfaced three regressions versus the historical C++ demo: the frame rate is uncapped (~1300+ FPS), the ImGui profiler reports `DrawCall=0, Triangles=0, Mesh=0, SkinnedMesh=0, Light=0` even though the scene is clearly rendering, and the UI plus scene appear oversized on a macOS Retina display. The first two are user-visible "this is broken" issues for anyone trying the demo; the third changed silently when we migrated SFML 2 → SFML 3 because SFML 3's macOS backend hardcodes `highDpi=NO` (see `src/SFML/Window/macOS/SFOpenGLView.mm:128`) and our Gui code still applies the SFML-2-era 2× compensation. We fix all three together because they all live in the same handful of files and the third one bears on what defaults the other two should expose.

## What Changes

- Add a frame-rate cap to `Engine::Run`, default `144` FPS. Configurable from Lua via an options table on `Engine.run`. Passing `max_fps = 0` (or `false`) disables the cap.
- Extend the Lua `Engine.run` binding to accept an optional second argument — a table of options — without breaking existing callers that pass only the callbacks table. Supported keys: `max_fps`, `gui_scale`, `gui_font_scale`.
- Fix the zero-counters bug in the deferred profiler. `RenderUtil::BeginFrame()` currently zeroes the counters at the *start* of the frame, before the Lua `on_update` runs the GUI then the scene pipeline — so the GUI always reads zero. Switch to a snapshot model: counters accumulate during the frame, `EndFrame()` snapshots them, and `Gui::ShowDefault` reads the previous-frame snapshot. This decouples GUI render order from counter visibility.
- Stop hardcoding `io.FontGlobalScale = 2.0f` in `Gui::Initialize`. Default the Gui's `guiScale` (which drives `style.ScaleAllSizes`) to `1.0f` instead of `2.0f`. Both values become tunable via the new `Engine.run` options. The SFML-2-era 2× compensation no longer makes sense under SFML 3's non-Retina GL surface and is the root cause of the "UI too big" report.
- Reduce the default demo window from `1920×1080` to a more conservative `1280×720` so the launcher fits on common Retina laptop screens (13–15"). The Lua side still owns the window so a user can override.
- Document the new Lua surface in `docs/LUA.md` (Engine.run options table) and add a §16 note in `docs/ARCHITECTURE.md` covering the SFML 3 macOS HiDPI limitation.

## Capabilities

### New Capabilities
- `engine-presentation`: Defines how `Engine::Run` paces the main loop (frame-rate cap with Lua configurability), how the render profiler counters surface to UI (snapshot model so display order is decoupled from increment order), and the ImGui scaling defaults under SFML 3's non-Retina macOS GL surface.

### Modified Capabilities
<!-- None — lua-scripting is still pending in changes/integrate-sol2-lua-scripting and not yet archived to openspec/specs/. -->

## Impact

- Code: `engine/Fury/Engine.{h,cpp}`, `engine/Fury/RenderUtil.{h,cpp}`, `engine/Fury/Gui.{h,cpp}`, `engine/Fury/LuaBindings.cpp`, `examples/main.cpp`, `examples/Demo.lua`.
- Public Lua API: `Engine.run(callbacks, options?)` gains an optional second argument. Existing call sites that pass only callbacks remain valid.
- Public C++ API: `Engine::Run` gains optional fields on its `EngineCallbacks` struct (or a small `EngineOptions` companion struct — finalize in design). `RenderUtil` gains snapshot getters; the existing `GetDrawCall()`-style getters are repointed at the snapshot, so call sites in `Gui.cpp` need no rename.
- Behavior: A fresh `cmake --build build && ./fury Demo.lua` from `examples/bin/` should report a steady ~144 FPS, non-zero profiler counters that reflect the deferred-lighting passes, and a sensibly-sized window and UI on a 14" MBP.
- No change to scene serialization, shader paths, or asset formats. No new submodules.
- Out of scope: actually enabling SFML 3 Retina rendering (would require patching SFML or a separate windowing layer). Documented as a known limitation.
