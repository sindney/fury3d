## Context

fury3d has no shipped game UI. The only in-game UI path is the editor's Dear ImGui (`Gui` Lua table): immediate-mode, code-only layout, no styling system, no retained element tree — unsuitable for game HUD/menus/inventory and for agent-driven UI iteration (nothing to inspect, nothing declarative to diff).

Constraints established by investigation (cloned sources + engine surface map):

- Render thread: game thread builds a `FramePacket`, GL runs only in the frame executor. The working template is `Gui.cpp`: `BuildDrawDataSnapshot()` deep-clones draw data on the game thread into `packet.guiFrame`; `RenderSnapshot()` replays GL in the executor after the postfx chain.
- Input: one SFML event choke point, `Engine.cpp HandleEvent`, already forwarding to ImGui-SFML with capture gating.
- Lua: sol2 over vendored Lua 5.4; single registration file `LuaBindings.cpp`.
- Build: CMake; CMake-native third-party libs integrate via `add_subdirectory` (SFML, Jolt precedent); plain-C libs vendor as compiled sources (lua, meshoptimizer precedent).
- Headless verification: `fury exec` (no GL) + `--screenshot` flags + `furye-cli render-mesh` (hidden-window GL) + `FURY_*` env hooks.

Library evaluation (RmlUi 6.4 / TGUI 1.x / Nuklear 4.13), verified against cloned repos:

| axis | RmlUi | TGUI | Nuklear |
|---|---|---|---|
| declarative styling | RML+RCSS (flexbox, table, anchors, animations, data binding) | txt themes only | none |
| mesh handout | `RenderInterface` (CompileGeometry/RenderGeometry/GenerateTexture/scissor) | `drawVertexArray` (unbatched immediate calls) | `nk_convert` into caller buffers |
| Lua | in-tree plugin, accepts external `lua_State` | none | none |
| tooling | Debugger inspector + full introspection + `DispatchEvent` | tree walk only | none |
| fonts | FreeType + effects, no SDF | FreeType + outlines, no SDF | stb bitmap, no kerning, no SDF |

User decision: **RmlUi**.

Demo-grounding facts (verified against the repo):

- The island test bed `examples/Projects/ocean/ocean_island.bin` (1.024 km terrain, 18 m peak, ocean at 400 cm, sky/sun, kraut trees) has a heightfield static body on the Terrain node — a Jolt capsule walks it today. No player/spawn node exists; a setup script adds one.
- The fox glTF is no longer in the repo, but the TPS pattern survives as `examples/Player.lua` + C++ `CharacterController` (Jolt `CharacterVirtual` capsule; Lua knobs for speeds, camera distance/height, clips). `SetCameraDistance(0)` + `SetCameraHeight(170)` ≈ first person.
- FPS gaps: no cursor grab/hide or relative-mouse API anywhere (look is LMB-drag only); `CharacterController` sensitivity hardcoded; no `Camera:SetFov` Lua binding (must re-call `PerspectiveFov`).
- Settings gaps: fps cap is boot-only (`Engine.run{max_fps}`), vsync hardcoded off, no runtime window resize/fullscreen API, and `OnWindowResized` has no runtime consumer (camera aspect doesn't track drag-resize).
- Settings already Lua-ready: `RenderSettings` postfx chain toggles (SSAO/SSR/FXAA/CRT), HDR, CSM on/off + map size + shadow far + split blend, `Pipeline.ApplyRenderSettings` re-resolve, ocean SSR/foam/choppiness/wind, sky TOD/day-length/clouds, controller speeds.
- The engine has **no planar reflections**; reflection control = SSR chain effect + `OceanComponent:SetSsrEnabled`.
- No game-settings store exists; Lua `io` works in `fury` scripts for a JSON cfg.

## Goals / Non-Goals

**Goals:**

- Vendor stock RmlUi as a submodule; no upstream patches (fork only if a future need forces one).
- Game UI renders through the engine's render-thread pipeline via the snapshot pattern; RmlUi never touches GL.
- SFML input forwarded at the existing choke point with capture gating, same standing as ImGui.
- RmlUi Lua plugin runs inside the engine's existing `lua_State`; game scripts author documents, data models, and event handlers.
- `furye-cli gui` subcommands give agents a headless loop: dump element tree, inspect element, dispatch synthetic events, screenshot to PNG.
- RmlUi Debugger (visual inspector) in editor/dev builds.
- Island demo shell proving the stack end-to-end: menu flow (main/pause/options), live settings with persistence, FPS player on ocean_island, HUD.

**Non-Goals:**

- SDF font rendering (no candidate ships it; RmlUi's pluggable `FontEngineInterface` keeps it possible later).
- Backdrop filters, clip masks, layer compositing (optional `RenderInterface` hooks) — initial backend skips them; documents using those features degrade gracefully.
- Replacing editor ImGui, or a WYSIWYG UI editor in furye.
- Gamepad focus navigation, IME composition, complex-script shaping (HarfBuzz), SVG/Lottie plugins.
- Cooking changes: UI assets (RML/RCSS/TTF/images) ship as raw files through the existing content-folder/pak rules; no new cook step.
- Fullscreen / exclusive display modes: `sf::Window::create` recreates the GL context, invalidating every cached GL object — deferred until context-loss handling exists. Demo resolution options are windowed `setSize`.
- Planar reflections: the engine doesn't have them; the options menu exposes SSR toggles (chain effect + ocean SSR) as the reflection control.
- Global game-time pause: the engine has no time-scale; pause menu disables the controller and frees the cursor while the living world (ocean/sky) keeps animating.

## Decisions

### D1: RmlUi over TGUI and Nuklear

RmlUi is the only candidate meeting the CSS-styling requirement (RCSS: flexbox/table/absolute, selectors, animations/transitions, data binding, ninepatch decorators), the only one with in-tree Lua, and the only one with a built-in inspector. Its `RenderInterface` hands out compiled mesh+texture handles — precisely our backend contract. TGUI's `drawVertexArray` is a clean hook but emits one call per glyph-run (engine-side batching mandatory) and has no CSS/Lua/inspector. Nuklear is the easiest vendoring but code-only UI, unkerned bitmap fonts, and no introspection — weakest for both game UI and agent tooling.

### D2: Submodule + `add_subdirectory`, no fork

Verified on upstream v6.4 CMake:

- `CMake/Dependencies.cmake:33`: `if(NOT TARGET Lua::Lua)` — "let users define the target already". We define a `Lua::Lua` ALIAS to our vendored lua before `add_subdirectory(ThirdParty/RmlUi)`, set `RMLUI_LUA_BINDINGS=ON` and `RMLUI_LUA_BINDINGS_LIBRARY=lua`. Upstream `find_package(Lua)` is skipped entirely.
- FreeType: **not vendored separately** — SFML Graphics already FetchContent-builds freetype VER-2-14-3 from source with every optional dep disabled, and declares it with `OVERRIDE_FIND_PACKAGE`, which transparently redirects RmlUi's `find_package(Freetype)` to that same hermetic build. One freetype for the whole engine, zero new vendoring. (Confirmed at apply time; a standalone `ThirdParty/freetype` submodule was tried and removed — it collided with SFML's targets.)
- `RMLUI_SAMPLES`/`RMLUI_TESTS` default OFF → `RMLUI_SHELL` OFF → `Backends/` and `Samples/` add nothing; `Tests/` untouched.
- Install rules are explicitly skipped "when RmlUi is included using add_subdirectory" (upstream comment at CMakeLists:156).
- All customization (render, system, file, input, future font engine) is via public abstract interfaces — host-side code, no patches.
- Pin the submodule to the v6.x release tag; document the bump procedure. If a patch is ever needed, the user forks (kraut-cli precedent) and we repoint the submodule.

Alternatives considered: vendoring RmlUi sources into our CMake (rejected — 300+ files, hand-maintained file lists, harder upgrades); fork-first (rejected — nothing to patch today).

### D3: Render integration via snapshot replay, RmlUi never calls GL

Mirror the proven `Gui.cpp` pattern:

- Game thread: `GameUI::Update(dt)` runs `Context::Update()` (layout/animation), then `Context::Render()` — but our `RenderInterface` implementation only **records**: `CompileGeometry` copies vertex/index spans into snapshot-owned buffers and returns an id; `RenderGeometry` appends a batch {geometry id, translation, texture, scissor}; textures map to engine `Texture` handles.
- The snapshot (shared_ptr) rides the `FramePacket` next to `guiFrame`.
- GL thread executor: after `pipeline->ExecutePacket` + `overlayJobs`, before `Gui::RenderSnapshot` (editor chrome stays on top; in the `fury` player the UI is the only overlay), replay batches: ortho projection, GLSL 330 core shader via `GLLoader`, per-batch scissor, premultiplied-alpha blend, and per repo hygiene **every sampler bound every draw**. `RenderUtil::BeginFrame/EndFrame` remains the outer state bracket.
- Texture callbacks: `GenerateTexture(RGBA)` / `LoadTexture(path via FileInterface)` create engine `Texture` (sRGB=false, no mips), with GL upload dispatched through `RenderThread::EnqueueJob`/`DispatchGL` (ocean e351c99 pattern); `ReleaseTexture` drops the handle, GL destroy also dispatched. In no-GL contexts (headless `fury exec`) texture callbacks register metadata only and never touch GL.
- Optional hooks (`PushLayer`/filters/clip mask) return no-op/defaults; RCSS filters degrade to no-ops.

Alternatives considered: letting RmlUi's stock GL3 renderer draw (rejected — owns GL state outside the render-thread executor, duplicates GLLoader, breaks snapshot determinism); rendering UI into an offscreen texture then compositing (rejected — extra copy, complicates input/DPI, no benefit since we already have an overlay slot).

### D4: One `GameUI` system owning contexts; input at the existing choke point

- `GameUI` (engine/Fury/GameUI.{h,cpp}) owns: RmlUi system interfaces (clock via engine time), `FileInterface` routing through the engine VFS so RML/RCSS/images/fonts resolve from content folders and paks unchanged, one `Context` ("main") sized in dp to the window, document registry.
- Input: `GameUI::HandleEvent(sf::Event)` called in `Engine.cpp HandleEvent` alongside `Gui::HandleEvent`; maps to `ProcessMouseMove/ButtonDown/Wheel/KeyDown/TextInput` (the official SFML platform backend is the reference mapping). Consumed-event tracking feeds `GameUI::WantCaptureMouse()/WantCaptureKeyboard()` exposed to Lua (Gui.h:43 pattern) so games can gate world input behind UI.
- Update tick on the game thread in the normal frame update; `Context::SetDimensions` + `SetDensityIndependentPixelRatio` on window resize/DPI change (platform-window-dpi spec).
- Game UI is active in the `fury` player and in furye play mode; editor edit-mode keeps ImGui only (game UI preview in the editor viewport is a later follow-up).

### D5: RmlUi Lua plugin inside the engine's sol2 state

`Rml::Lua::Initialise(engine L)` (public API, accepts an external state) registers the `rmlui` global into the same Lua 5.4 state sol2 uses — raw C-API bindings and sol2 coexist by construction. RML inline handlers (`onclick="fn()"`), `AddEventListener`, and Lua data models then work in game scripts, and `LuaBindings.cpp` adds a thin `GameUI` sol2 table (LoadDocument/Show/Hide/Toggle, GetDocument, data-model helpers, WantCapture*). Registered identically in the headless `exec` path (no GL) so `tests/lua` can drive UI logic.

Alternatives considered: sol2-only hand bindings without the plugin (rejected — loses inline RML handlers and Lua data models, duplicates a maintained binding layer); separate UI Lua state (rejected — scripts couldn't touch both scene and UI).

### D6: `furye-cli gui` subcommands on the hidden-window GL path

The `render-mesh` precedent: gui subcommands need a GL context, so they live in the launcher's main path with a hidden SFML window, compiled into `furye-cli` only. Subcommands:

- `gui tree <scene> <doc.rml> [--frame N]` — JSON element tree: tag, id, classes, child rects, visibility. Machine-parseable stdout.
- `gui inspect <scene> <doc.rml> <selector>` — JSON for first match: address, border/padding/content boxes, computed style block, attributes. Selector = `#id`, `.class`, or tag (matches RmlUi's `GetElementById`/query API).
- `gui event <scene> <doc.rml> <selector> <event> [--param k=v]...` — `DispatchEvent` synthetic event, then prints the resulting tree/state diff (e.g. click a button, read back the counter label).
- `gui shot <scene> <doc.rml> <out.png> [--size WxH] [--frame N] [--script init.lua]` — render N frames (default 30, the frozen-headless-water lesson: advance wave/anim time so the frame is representative), PNG via the existing backbuffer capture.
- All exit 0/1/2 (success/user error/internal error), never open a visible window, appear in `furye-cli help`.

This gives agents the full loop: write RML/RCSS → `gui tree`/`inspect` to verify structure/layout → `gui event` to verify behavior → `gui shot` to verify pixels.

### D7: Fonts via stock FreeType engine; effects and DP scaling for quality

FreeType raster at UI size + RCSS font-effects (outline/shadow/glow) covers game HUD needs; `LoadFontFace` (with fallback-face flag) resolves through the VFS; `SetDensityIndependentPixelRatio` keeps physical text size across DPI. SDF stays available as a future `SetFontEngineInterface` plugin without touching integration code.

### D8: Demo shell architecture — Lua state machine over four documents

The island demo (`examples/Projects/ocean/` + `examples/IslandGame.lua` bootstrap modeled on `Player.lua`) runs a Lua-side state machine `MainMenu -> InGame <-> Pause`, driving four documents on the "main" context: `main_menu.rml`, `pause_menu.rml`, `options.rml` (shared, shown from both menus), `hud.rml` (crosshair dot + FPS counter — the FPS counter makes the fps-cap setting observable). Boot state: island scene loaded, cinematic drift camera (slow scripted orbit), main menu shown, cursor free. Start: hide menu, enable controller, grab cursor. ESC in game: release cursor, disable controller input, show pause menu (world keeps living — no global pause exists). Exit paths use `Window.Close` (never `os.exit` — thread-dtor lesson).

Style: simple modern AAA — dark translucent panels over the live scene, thin accent bar, one clean sans face, uppercase small-caps labels, hover glow + pressed darken via RCSS pseudo-classes, short fade/slide transitions (RCSS animations). No bitmap-heavy skinning; ninepatch only where panels need borders.

### D9: Settings plumbing — small engine additions, everything else already Lua

New (bounded C++):

- `Window` Lua bindings: `SetFpsCap(n|false)` / `SetVsync(b)` over the existing SFML window (one-liners; today boot-only/hardcoded).
- `Window.SetResolution(w,h)`: windowed `sf::Window::setSize` (context preserved) + a real resize consumer: update active camera aspect and rebind the render surface/viewport (today `OnWindowResized` has no runtime consumer — this fixes drag-resize too).
- `InputUtil`: `SetCursorGrabbed(b)` / `SetCursorVisible(b)` (SFML `setMouseCursorGrabbed`/`setMouseCursorVisible`) + relative mouse delta (position-delta per frame; SFML 2 has no raw input — delta-from-center with recentering is the standard approximation).
- `CharacterController`: first-person mode (look without LMB-drag, yaw on body / pitch on camera, clamp pitch) + Lua-settable mouse sensitivity; hide-the-mesh is unnecessary — the FPS player spawns with no visual mesh at all.
- `Camera:SetFov(fov)` Lua binding (re-projects preserving aspect/near/far).

Already Lua-ready (menu wires, no new C++): postfx master + per-effect SSAO/SSR/FXAA, HDR, CSM on/off + map size + shadow far, ocean SSR/foam/choppiness/wind, sky time-of-day, walk/run speeds.

Persistence: a Lua `Settings` table writes/reads a flat key=value cfg (`Projects/ocean/game_settings.cfg`) via Lua `io`; applied at boot, saved on change/apply. No C++ settings store (the QoL registry is editor-only).

Alternatives considered: do FPS look purely in Lua from deltas (rejected — `CharacterController` already owns camera-relative movement + boom math; a first-person flag keeps one owner); fullscreen via window recreate (rejected — GL context loss, see Non-Goals).

### D10: FPS player on the island

`setup_island_player.lua` (modeled on `setup_physics_scene.lua`) adds: `Player` node + `CharacterController` (capsule height 180 cm, radius ~35 cm — 1.8 m eye ≈ camera height 170 cm), camera bound at head, spawn on the beach above the 400 cm waterline (walkable down to ~296 cm per the ocean setup), `modelYawOffset` irrelevant (no mesh). Movement: WASD camera-relative, Shift run, Space jump, scene gravity, collides with the terrain heightfield statics that already exist in `ocean_island.bin`. Mouse look: grabbed cursor + delta-driven yaw/pitch (D9). ESC: state machine to pause (D8). Verified against slopes/steps at the waterline and 1 km sightlines (CSM shadow-far and far-plane settings sanity-checked for the island scale).

## Risks / Trade-offs

- [Bitmap fonts, no SDF: text scaled far above baked size softens] → Use font-effects and per-size faces; document the limitation; `FontEngineInterface` upgrade path needs no rework of D3/D5.
- [FindFreetype picks a system freetype on macOS, breaking hermetic builds] → Steer via `FREETYPE_*` cache vars to the vendored build (D2); CI configure log prints the resolved path; worst case `CMAKE_DISABLE_FIND_PACKAGE_Freetype` + predefined `Freetype::Freetype` alias.
- [RmlUi callback touches GL off the render thread → guard assert/crash] → D3 forbids GL in callbacks; uploads only via `EnqueueJob`/`DispatchGL`; `FURY_GL_THREAD_GUARD` stays on in debug.
- [Raw-C-API plugin + sol2 in one state misbehave] → RmlUi Lua supports 5.4 explicitly and is designed for external states; smoke test in tasks (register, run inline handler, run data model) before building features on it.
- [UI perf on large documents] → RmlUi only recompiles geometry on invalidation; snapshot replay is a handful of draw calls; measure with Tracy (upstream even ships `RMLUI_TRACY_PROFILING`, off by default).
- [Debugger needs a font asset shipped] → Ship one default UI font under examples resources; document the requirement.
- [Submodule pinning drifts from upstream] → Pin tag, bump deliberately, changelog review at bump time; fork-and-repoint is the documented escape hatch.
- [Windowed `setSize` resize leaves camera aspect / render surface stale — the API exists but nothing consumes `OnWindowResized` at runtime] → D9 adds the resize consumer as part of the resolution plumbing; verified by screenshot at each menu resolution option.
- [SFML 2 has no raw mouse input; delta-from-center recentering can jitter at fps drops or near screen edges] → Cursor grabbed + recenter each frame after reading delta; sensitivity slider in Options covers feel; revisit if play-test shows drift.
- [Island scale (1 km) exposes range assumptions: CSM shadow-far, camera far plane, ocean tile vs beach] → Demo task sanity-checks far plane/shadow-far for the island; settings menu exposes shadow-far so it is tunable in-place.
- [Pause is not a real time freeze (no engine time-scale); ocean/sky keep animating behind the pause menu] → Accepted AAA "living world" behavior; documented in the demo spec; controller input is cut so the player state cannot change while paused.

## Migration Plan

Additive change; no existing behavior migrates. The ImGui `Gui` Lua table keeps working (editor + debug panels). Games opt into GameUI per scene/script. Rollback = remove submodule + CMake block; no asset or scene format changes.

## Open Questions

- UI asset home in the Projects layout (`ui/` folder next to `Scenes/`?) and pak path conventions — settle in apply task 1.
- furye edit-mode viewport: embed game UI later, or keep play-mode-only permanently — start play-mode + player only.
- Default project UI scale policy (dp ratio source: window DPI vs project override).
- Demo defaults: day length / time-of-day start for the island menu backdrop, and whether the cinematic drift camera is a scripted spline or simple orbit — settle during the demo task, low stakes.
