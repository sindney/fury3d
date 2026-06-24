## Context

Background: the Lua demo `examples/Demo.lua` runs the deferred-lighting pipeline end-to-end on `Resource/Scene/scene.bin`, but the first runtime checkout surfaced three issues:

1. FPS is uncapped. `examples/main.cpp:39` does `window.setVerticalSyncEnabled(false)` and `engine/Fury/Engine.cpp:179-228` has no rate limit. On an idle MBP the loop spins at ~1300 FPS, wasting GPU and battery.
2. The ImGui profiler reports zero for every render counter. The Lua callback order in `Demo.lua` is `Gui.ShowDefault → Gui.Render → Pipeline.Execute` inside `on_update`. `RenderUtil::BeginFrame()` (called at frame start, `engine/Fury/Engine.cpp:192`) zeroes all counters at `RenderUtil.cpp:263-269`. So when `Gui.ShowDefault` reads `GetDrawCall()` at `Gui.cpp:441-445`, the values are 0 — the pipeline hasn't run yet. The pipeline *does* call `IncreaseDrawCall` and `IncreaseTriangleCount` (`Pipeline.cpp:434,514,602,679`, `PrelightPipeline.cpp:227-244,315-528`), but the GUI never sees the result.
3. UI is oversized on macOS Retina. `Gui.cpp:81` hardcodes `io.FontGlobalScale = 2.0f` and the Lua launcher passes `guiScale = 2` (`main.cpp:42`), which `Gui::Initialize` feeds into `style.ScaleAllSizes`. Under SFML 2 this was a Retina compensation; under SFML 3 the macOS backend hardcodes `highDpi=NO` at `engine/ThirdParty/SFML/src/SFML/Window/macOS/SFOpenGLView.mm:128` so the GL surface is in screen points, not backing pixels. The 2× compensation is now double-scaling.

Constraints:
- The lua-scripting capability is not yet archived to `openspec/specs/`, so its requirements live in `openspec/changes/integrate-sol2-lua-scripting/specs/lua-scripting/spec.md`. This change cannot use `MODIFIED Requirements` against it; the new contract instead lives in a fresh `engine-presentation` capability and the future archive of `integrate-sol2-lua-scripting` may absorb or cross-reference it.
- Existing Lua callers pass a single callbacks table to `Engine.run`. The new options table must be backwards-compatible.
- The macOS HiDPI GL surface limitation is an upstream-SFML choice; we are not patching SFML in this change.

## Goals / Non-Goals

**Goals:**
- Cap the demo at a sane default frame rate (144 FPS) without burning a core.
- Make the profiler counters reflect what was actually rendered, regardless of GUI-vs-scene draw order.
- Eliminate the double-scaling on macOS Retina so the demo window and UI look proportionate on a 14"–16" MBP.
- Expose the three knobs (max_fps, gui_scale, gui_font_scale) to Lua without breaking the existing one-argument `Engine.run` call shape.

**Non-Goals:**
- Enabling actual HiDPI / Retina rendering. That would require either patching SFML's macOS backend to call `[oglView setWantsBestResolutionOpenGLSurface:YES]`, or replacing the windowing layer. Documented as a known limitation; out of scope here.
- Per-window-resize dynamic rescaling of the GUI. ImGui supports it but the current style scale is set once at `Initialize` time and we keep that contract.
- Replacing `sf::Window::setFramerateLimit` with a higher-precision custom timer. SFML's built-in limiter is good enough for the demo.
- Adding a busy-wait fallback or vsync toggle in the options table — the existing `setVerticalSyncEnabled(false)` call stays in C++ for now.

## Decisions

### Decision 1: Use `sf::Window::setFramerateLimit`, not a hand-rolled cap

`Engine::Run` will call `window.setFramerateLimit(options.max_fps)` once before entering the main loop (after `OnInit`). `max_fps == 0` (or `false` from Lua) translates to `setFramerateLimit(0)`, which SFML treats as "no limit". The cap value is captured into a single `EngineOptions` struct passed alongside `EngineCallbacks`.

**Alternative considered:** hand-rolled `std::this_thread::sleep_for` based on `sf::Clock`. Rejected — SFML's implementation already accounts for the work the frame did, so the residual sleep is more accurate, and a hand-rolled version would have to replicate that. Not worth the maintenance burden for a demo.

### Decision 2: Snapshot the render counters on `EndFrame()`, not `BeginFrame()`

Move the per-frame counter reset from `BeginFrame()` to `EndFrame()`, and have `EndFrame()` copy the working totals into a parallel `m_LastFrame*` set of fields before zeroing. The public getters return the `m_LastFrame*` snapshot. Frame N's working totals are visible from `EndFrame()` of frame N until `EndFrame()` of frame N+1, so any GUI that calls a getter from anywhere inside frame N+1 sees stable frame-N values regardless of when in the frame it queries.

**Alternative considered:** require Lua callbacks to draw the scene before the GUI. Rejected — that would push a render-order constraint onto every script, and it doesn't solve the case where a tool wants to read counters mid-frame for debugging.

**Alternative considered:** atomic getters that return both working and last-frame totals. Rejected — overkill for a single-threaded render path; the snapshot model is six new fields and four memcpy-equivalent lines.

### Decision 3: Add an `EngineOptions` struct rather than overloading `EngineCallbacks`

Introduce `struct EngineOptions { int max_fps = 144; float gui_scale = 1.0f; float gui_font_scale = 1.0f; };` in `Engine.h` alongside `EngineCallbacks`. `Engine::Run` gains an overload `Run(window, callbacks, options)`; the existing two-arg `Run(window, callbacks)` calls through with default options. This keeps the callback shape clean — options aren't behavior, they're settings.

The Lua side passes options via the second argument of `Engine.run`. `LuaBindings.cpp` reads each field with `cb_table.get_or<T>(default)` semantics so missing keys fall back to C++ defaults.

**Alternative considered:** stuff option fields into `EngineCallbacks`. Rejected — conceptually muddles "things you do" with "how the loop runs". A future fourth option (vsync? scene-render hook?) shouldn't keep growing `EngineCallbacks`.

### Decision 4: Drop the hardcoded `io.FontGlobalScale = 2.0f`; default `guiScale` to 1.0

Change `Gui::Initialize` to accept a font scale parameter (or read it from a global it stores), defaulting to `1.0f`. Remove the `io.FontGlobalScale = 2.0f` line at `Gui.cpp:81`. Default the launcher's call to `Engine::Initialize(window, 1.0f, ...)`. Lua overrides via `gui_scale` and `gui_font_scale` in the options table flow through `Engine::Run`'s options into `Gui::Initialize`.

**Subtlety:** Gui is currently initialized inside `Engine::Initialize`, which runs *before* `Engine::Run`, which is what reads the options table. Two ways to thread the options:

- (a) Move Gui initialization out of `Engine::Initialize` into `Engine::Run`, after `OnInit` and before the main loop. Cleaner ownership; matches the "options live with Run" model.
- (b) Default Gui init to 1.0× and provide a `Gui::SetScale` / `Gui::SetFontScale` callable from inside `OnInit` via the Lua bindings.

Choose **(a)**. Gui init at the start of `Run` is one extra function call and removes the awkward `Engine::Initialize` `guiScale` parameter, which was effectively a layering violation (Run-time concern in Initialize-time API).

### Decision 5: Reduce default window size from 1920×1080 to 1280×720

Independent of the scaling fix, the 1920×1080 default doesn't fit on the usable area of a 14" MBP (1512×982 effective). Drop to 1280×720 in `main.cpp`. The Lua side currently doesn't own window creation — it could in a future change, but that's a separate concern.

### Decision 6: Document the SFML 3 Retina situation, don't patch around it

Add a short note in `docs/ARCHITECTURE.md` (new §16, or extend §15) and `docs/LUA.md` (Gotchas section) explaining: SFML 3.1 macOS backend forces `highDpi=NO`, the GL surface is at point resolution, and users on Retina screens will see slightly blurry pixels by design. Future work could vendor an SFML patch enabling `wantsBestResolutionOpenGLSurface`, but that interacts with `Pass.cpp:652`'s `glViewport(0, 0, m_ViewPortWidth, m_ViewPortHeight)` (which assumes pixel == point) and with the GBuffer texture allocation sizes. Big enough to be its own change.

## Risks / Trade-offs

- **Risk: Lua scripts that pre-date this change pass `guiScale = 2` explicitly to compensate, end up with tiny UI.** → Mitigation: there are no such scripts yet — `Demo.lua` is the only one. Update `Demo.lua` to either pass no options or to explicitly set `gui_scale = 1.0` and document the change in `docs/LUA.md`.

- **Risk: Frame-rate cap interacts badly with the fixed-timestep `numLoops < MAX_FRAMESKIP` logic in `Engine::Run`.** → Mitigation: at 144 FPS each frame is ~6.9 ms, well under the 40 ms `SKIP_TICKS`, so the inner fixed-update loop fires exactly once per frame on average. No change to the fixed-update math is needed.

- **Risk: Moving `Gui::Initialize` from `Engine::Initialize` to `Engine::Run` breaks anything that calls Gui functions from `OnInit` (which fires before `Gui::Initialize` in the new ordering).** → Mitigation: scan `Demo.lua`'s `on_init` — it only touches `OcTree`, `Scene`, `SceneNode`, `Camera`, `Pipeline`, `FileUtil`, none of which call into ImGui. Add an assertion: if `OnInit` calls a Gui function, that's a Lua-side bug. Document the ordering in `LUA.md`.

- **Risk: The snapshot-counter change makes a frame-N teardown see counters from frame N-1 in `OnShutdown`.** → Acceptable: nothing in the codebase reads counters at shutdown. Document the behavior anyway.

- **Trade-off: We're not actually fixing Retina rendering, just the double-scaling symptom.** → Acceptable for this change. The pixels-per-point ratio is documented; users wanting sharp Retina rendering can either run windowed mode at native resolution and live with it, or wait for the follow-up SFML-patch change.

## Migration Plan

In-tree change, no deploy:
1. Land `engine/Fury/RenderUtil.{h,cpp}` snapshot refactor first (commits independently testable).
2. Land `engine/Fury/Engine.{h,cpp}` options + framerate cap + Gui-init relocation.
3. Land `engine/Fury/Gui.cpp` font-scale defaulting fix.
4. Land `engine/Fury/LuaBindings.cpp` options-table parsing.
5. Update `examples/main.cpp` (window size + remove guiScale=2) and `examples/Demo.lua` (verify defaults work).
6. Build, run, verify the three acceptance scenarios from the spec.
7. Update `docs/LUA.md` and `docs/ARCHITECTURE.md`.
8. Commit; do not push without confirmation.

No rollback complexity — single-commit revert if needed.

## Open Questions

- Should `Engine::Initialize`'s `guiScale` parameter be removed entirely, or kept as a no-op for source-compat? Lean toward removing — there are no out-of-tree callers and the breaking change is trivial. Confirm during implementation.
- Should the framerate cap value be readable back from Lua (`Engine.get_max_fps()`)? Not in this change. Listed for the follow-up.
