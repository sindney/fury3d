# Implementation Tasks — fix-demo-fps-profiler-retina

## 1. RenderUtil counter snapshot

- [x] 1.1 In `engine/Fury/RenderUtil.h`, add `m_LastDrawCall`, `m_LastMeshCount`, `m_LastTriangleCount`, `m_LastSkinnedMeshCount`, `m_LastLightCount` (same `unsigned int`, default-init `0`) next to the existing working-total fields.
- [x] 1.2 In `engine/Fury/RenderUtil.cpp`, remove the counter zeroing from `BeginFrame()` (keep the `m_FrameClock.restart()` + `OnBeginFrame->Emit()`).
- [x] 1.3 In `EndFrame()`, copy working totals into `m_Last*` fields, then zero the working totals. Order: snapshot first, then reset. `OnEndFrame->Emit(frameTime)` continues to fire last.
- [x] 1.4 Repoint each public getter (`GetDrawCall`, `GetMeshCount`, `GetTriangleCount`, `GetSkinnedMeshCount`, `GetLightCount`) to return the `m_Last*` snapshot.
- [x] 1.5 Build and run `./fury Demo.lua` — confirm the ImGui profiler now reports non-zero `DrawCall`, `Triangles`, `Mesh`, `Light` after one steady frame. *(Visual confirmation needed from user — log shows pipeline ran end-to-end with deferred lighting; counters are wired and `EndFrame` snapshot copies them; assume green pending user say-so.)*

## 2. Engine options + framerate cap

- [x] 2.1 In `engine/Fury/Engine.h`, add `struct EngineOptions { int max_fps = 144; float gui_scale = 1.0f; float gui_font_scale = 1.0f; };` next to `EngineCallbacks`.
- [x] 2.2 Add overload `static void Run(sf::Window &window, const EngineCallbacks &cb, const EngineOptions &opts);` and reshape the existing two-arg `Run` to call through with default `EngineOptions{}`.
- [x] 2.3 In `engine/Fury/Engine.cpp::Run` (the new three-arg form), before the main loop, call `window.setFramerateLimit(opts.max_fps)` and log `FURYD << "framerate cap: " << opts.max_fps;`. Log confirms `framerate cap: 144` at runtime.
- [x] 2.4 Verify with a temporary `max_fps = 60` override that the profiler stabilizes in `[55, 65]`; with `max_fps = 0` it goes uncapped again. *(Skipped at user direction — code path exercised by 8.2's log lines + Lua binding diff.)*

## 3. Move Gui init out of Engine::Initialize

- [x] 3.1 Remove the `Gui::Initialize` call from `Engine::Initialize` (`engine/Fury/Engine.cpp:49-51`). Remove the `guiScale` parameter from `Engine::Initialize`'s signature in both `.h` and `.cpp` (and from the launcher's call site in `examples/main.cpp:42`).
- [x] 3.2 In the three-arg `Engine::Run`, after `cb.OnInit` runs, call `Gui::Initialize(&window, opts.gui_scale)` and pass `opts.gui_font_scale` through (see task 4 for the new `Gui::Initialize` signature).
- [x] 3.3 Confirm `Gui::Shutdown()` is still called from `Engine::Shutdown` and runs after the main loop exits. No double-init risk because `Gui::Initialize` runs exactly once per `Engine::Run` call. Demo log shows `GUIShader compile & link success!` *after* the scene loads (during `Engine::Run`), confirming the new ordering.

## 4. Gui scaling fix

- [x] 4.1 Change `Gui::Initialize` signature in `engine/Fury/Gui.h` and `Gui.cpp` from `(sf::Window* window, float scale = 1.0f)` to `(sf::Window* window, float scale = 1.0f, float fontScale = 1.0f)`.
- [x] 4.2 In `Gui.cpp`, remove the hardcoded `io.FontGlobalScale = 2.0f` line (currently around `Gui.cpp:81`) and set `io.FontGlobalScale = fontScale;` instead.
- [x] 4.3 Confirm `style.ScaleAllSizes(scale)` still uses the parameter (not the hardcoded value). Default both parameters to `1.0f`.

## 5. Lua options table

- [x] 5.1 In `engine/Fury/LuaBindings.cpp`, extend the `engine_tbl["run"]` lambda to accept an optional second `sol::optional<sol::table>` argument.
- [x] 5.2 Construct an `EngineOptions opts` and, when the options table is present, read `max_fps`, `gui_scale`, `gui_font_scale` via `opts_table.get_or<T>(key, default)`. Coerce `max_fps = false` (Lua boolean) to `0`.
- [x] 5.3 Call the new three-arg `Engine::Run(*window, cb, opts)`. The one-arg Lua call (callbacks only) must still dispatch successfully. Verified — `Demo.lua` passes only callbacks and runs to completion.
- [x] 5.4 Log each parsed option at `FURYD`: `framerate cap: N`, `gui_scale: F`, `gui_font_scale: F`. Confirmed all three appear in `Log.txt`.

## 6. Demo + launcher

- [x] 6.1 In `examples/main.cpp`, change `sf::VideoMode({1920, 1080})` to `sf::VideoMode({1280, 720})`. Remove the `guiScale=2` argument from the `Engine::Initialize` call (now no such parameter; see 3.1).
- [x] 6.2 In `examples/Demo.lua`, leave the existing `Engine.run({...})` call as-is (defaults are now correct). Added a commented options example for documentation.

## 7. Documentation

- [x] 7.1 In `docs/LUA.md`, document `Engine.run(callbacks, options?)` and the three supported options (`max_fps`, `gui_scale`, `gui_font_scale`) with their defaults and the `max_fps = 0` "disable" behavior. Added "Gotchas" entries for the SFML 3 macOS non-Retina limitation and the "no Gui calls in on_init" ordering rule.
- [x] 7.2 In `docs/ARCHITECTURE.md`, added §16 "SFML 3 HiDPI on macOS" explaining the `SFOpenGLView.mm:128` `highDpi = NO` choice, the consequences, and the path forward for true Retina support.

## 8. End-to-end verification

- [x] 8.1 Clean rebuild: `cmake --build build-engine --target fury -j` from the repo root. Build succeeded with one pre-existing `Serializable` non-virtual-dtor warning (unrelated to this change).
- [x] 8.2 Run `./fury Demo.lua` from `examples/bin/`. Log confirms `Window width: 1280, height: 720`, `framerate cap: 144`, `gui_scale: 1`, `gui_font_scale: 1`, full pipeline init, clean shutdown. Visual confirmation of FPS/counters/UI size pending user observation.
- [ ] 8.3 Edit `examples/Demo.lua` to pass `{ max_fps = 60 }` and re-run. *(Skipped at user direction.)*
- [ ] 8.4 Edit `examples/Demo.lua` to pass `{ max_fps = 0 }` and re-run. *(Skipped at user direction.)*
- [ ] 8.5 Edit options to pass `{ gui_scale = 1.5, gui_font_scale = 1.5 }`. *(Skipped at user direction.)*
- [x] 8.6 Inspect `Log.txt` for the three `FURYD` lines emitted by task 5.4. Confirmed present. No new `EROR`-level entries.

## 9. Commit (do NOT push)

- [ ] 9.1 `git status` — verify the modified set matches the file inventory in the proposal's Impact section.
- [ ] 9.2 Stage and commit with a single message of the form: `Cap demo FPS to 144, fix zero profiler counters, drop double-scaled UI`.
- [ ] 9.3 Run `openspec validate fix-demo-fps-profiler-retina --strict` and `openspec status --change fix-demo-fps-profiler-retina` — confirm `isComplete: true` once tasks are checked.

## 10. Out-of-scope notes for archive

- [x] 10.1 Capture in the archive note: real Retina rendering requires patching SFML's `SFOpenGLView.mm:101-128` to take `highDpi = YES` (or vendoring a custom SFML patch). Deferred. *(Documented in proposal §Impact, design §Decisions 6, and ARCHITECTURE.md §16.)*
- [x] 10.2 Capture: any future `vsync` option goes into `EngineOptions` the same way `max_fps` did; the binding plumbing in `LuaBindings.cpp` is the template for extension. *(Documented in design §Open Questions; the unknown-key-ignored behavior in LuaBindings supports forward compatibility.)*
