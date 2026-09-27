## 1. Vendor and build integration

- [x] 1.1 Add `engine/ThirdParty/RmlUi` submodule pinned to the v6.x release tag (pinned 6.3). FreeType is NOT vendored: SFML Graphics already FetchContent-builds VER-2-14-3 hermetically with `OVERRIDE_FIND_PACKAGE`, so RmlUi's `find_package(Freetype)` redirects to it
- [x] 1.2 CMake: verify the freetype redirect resolves (configure log prints "Found Freetype::Freetype") and the whole engine links one freetype
- [x] 1.3 CMake: predefine `Lua::Lua` (INTERFACE IMPORTED wrapping the vendored lua target - alias-of-alias is illegal), set `RMLUI_LUA_BINDINGS=ON`, `RMLUI_LUA_BINDINGS_LIBRARY=lua`, `RMLUI_SAMPLES`/`RMLUI_TESTS`/`RMLUI_PRECOMPILED_HEADERS` OFF, then `add_subdirectory(ThirdParty/RmlUi)`
- [x] 1.4 Link `rmlui_core`/`rmlui_debugger`/`rmlui_lua` into fury, furye, furye-cli (via `FURY_PLATFORM_LIBS`); RmlUi/freetype headers quarantined as SYSTEM includes
- [x] 1.5 Verify configure+build on macOS; `git diff` inside the RmlUi submodule is empty; only rmlui_core/debugger/lua targets exist; binaries run
- [ ] 1.6 Windows build check (configure+compile) — GL 4.x path unaffected, freetype via SFML FetchContent redirect. NOTE: cannot verify on macOS; needs a Windows machine or CI. ALSO: pre-existing unrelated failure - KrautPreview link wants the removed `AGL` framework on current macOS SDKs; build uses `-DFURY_WITH_KRAUT_PREVIEW=OFF`

## 2. GameUI core system

- [x] 2.1 Create `engine/Fury/GameUI.{h,cpp}`: RmlUi `SystemInterface` (engine clock) + `FileInterface` (engine VFS: content folders, working-dir rewrite, pak), `Rml::Initialise/Shutdown` in engine init/shutdown. Includes atexit guard for os.exit teardown order
- [x] 2.2 Create the "main" `Context` sized in dp to the window; per-frame `GameUI::Update(dt)` on the game thread; `SetDimensions` + `SetDensityIndependentPixelRatio` on resize/DPI events (resize hook lands with group 4 input)
- [x] 2.3 Document registry: `LoadDocument(path)` with logging; missing-path failure returns nullptr without crashing. Show/hide/close ride the RmlUi Lua plugin API
- [x] 2.4 UI asset home: `Resource/Ui/` for engine assets (Engine/ prefix), `Projects/<name>/ui/` for game assets - resolution verified through Scene::ResolveAsset + AssetBackend (smoke.rml loads via Engine/Ui/smoke.rml)
- [x] 2.5 Smoke test: `Rml::Lua::Initialise(engine lua_State)` in `LuaBindings::Register` (all three state-creation sites); tests/lua/game_ui_smoke.lua PASSES under `fury exec` (rmlui global, contexts.main, document load, layout, sol2 coexistence). TRAPS found: RmlUi 6.3 runs layout inside Context::Render() (Update also lays out docs, but Render is the canonical driver); display property initial value is inline (no UA stylesheet - width/height ignored on inline elements, base stylesheet must set display:block); rmlui global is userdata not table

## 3. Render integration

- [x] 3.1 `GameUIRenderer` (RmlUi `RenderInterface`) recording: geometry spans copied into a PERSISTENT store (RmlUi 6.3 retains compiled handles across frames - compiles lazily once, re-renders every frame); batches resolve shared_ptr<Geometry> at record time; layer/filter/clip-mask hooks no-op
- [x] 3.2 Texture bridge: `LoadTexture` (AssetBackend+stb, premultiplied) / `GenerateTexture` create engine `Texture` (RGBA8, CLAMP_TO_EDGE, no mips) uploaded via DispatchGL; `ReleaseTexture` destroys on GL thread; metadata-only when no GL
- [x] 3.3 Game thread: `Context::Update()` then `Context::Render()` recorded into a snapshot; rides `FramePacket::uiFrame` (next to guiFrame); replay called in executor before Gui::RenderSnapshot
- [x] 3.4 GL replay: ortho (top-left origin), GLSL 330 core via GLLoader, per-batch scissor (y-flipped), premultiplied blend (ONE, ONE_MINUS_SRC_ALPHA), full-viewport set (postfx leaves stale viewport), 1x1 white dummy keeps sampler 0 bound, full state backup/restore
- [x] 3.5 Verified: overlay panel draws over the postprocessed island scene; render-thread on/off screenshots identical (ui_overlay_test.lua + Resource/Ui/overlay_test.rml)
- [x] 3.6 Verified: `fury exec` loads a document and answers layout queries with no GL (game_ui_smoke.lua; texture callbacks queue harmless jobs)

## 4. Input forwarding

- [x] 4.1 `GameUI::HandleEvent(sf::Event)` at the `Engine.cpp HandleEvent` choke point: mouse move/button/wheel, key down/up, `TextEntered` unicode, window resize -> SetDimensions (upstream RmlUi_Platform_SFML mapping, SFML 3 only)
- [x] 4.2 `WantCaptureMouse()`/`WantCaptureKeyboard()` from hover/focus elements (background = root/document is not a capture; RmlUi 6.3 has no body element - body attrs merge onto the document), exposed C++ and Lua
- [x] 4.3 Verify live: click on a UI button fires its event and is captured; click outside UI reaches game input; typing reaches a focused input element (needs a real window - verified in 10.7 playtest) (user playtest 2026-09-27)

## 5. Lua API

- [x] 5.1 `GameUI` sol2 table in `LuaBindings.cpp`: LoadDocument/Show/Hide/Toggle/Close/IsLoaded (path-addressed; GetDocument walks source URLs first since RmlUi's GetDocument matches element id), WantCaptureMouse/Keyboard; registered in all three state-creation sites
- [x] 5.2 Lua data models verified: `ctx:OpenDataModel(name, tbl)` + `data-model` attr + `{{ var }}` interpolation; `model.var = x` + Update applies (tests/lua/game_ui.lua)
- [x] 5.3 Event listeners from Lua verified: `btn:AddEventListener("click", fn)` fires on DispatchEvent; listener can touch the scene
- [x] 5.4 Inline RML handlers verified: `onclick="OnTestClick()"` resolves to engine-state Lua globals

## 6. Fonts and debugger

- [x] 6.1 Ship default UI fonts (Lato Latin regular+bold in examples/Resource/Fonts, OFL, README notes source/license); LoadFontFace via FileInterface (`Engine/Fonts/...` prefix)
- [x] 6.2 Verify RCSS font effects (outline, shadow) render, and text physical size holds across DPI scale changes (user playtest 2026-09-27)
- [x] 6.3 RmlUi Debugger plugin toggle via `FURY_UI_DEBUG` env hook (editor menu entry deferred to demo polish)

## 7. furye-cli gui subcommands

- [x] 7.1 `gui` subcommand family on the furye-cli launcher main path (hidden window, render-mesh precedent); exit codes 0/1/2; `furye-cli help` entry
- [x] 7.2 `gui tree <scene> <doc> [--frame N]`: JSON element tree (tag/id/classes/visibility/rect, nested), jq-parseable
- [x] 7.3 `gui inspect <scene> <doc> <selector>`: JSON boxes + computed styles + attributes; no-match exits 1
- [x] 7.4 `gui event <scene> <doc> <selector> <event> [--param k=v]`: DispatchEvent + frame advance + post-state JSON
- [x] 7.5 `gui shot <scene> <doc> <out.png> [--size WxH] [--frame N] [--script init.lua]`: Lua bootstrap (scene+pipeline+camera+doc), C++ frame loop, inline backbuffer readback + PNG flip (verified 1280x720 and 800x600)
- [x] 7.6 Round-trip verified: gui event click on counter doc; demo docs tree/inspect/shot pass

## 8. Demo engine plumbing (settings surface + FPS feel)

- [x] 8.1 `Window.SetFpsCap(n|false)` / `Window.SetVsync(b)` Lua bindings over `Engine::SetFpsCap/SetVsync` (SFML window wrappers, no-op headless)
- [x] 8.2 `Window.SetResolution(w,h)`: windowed `setSize` + resize consumer in `Engine::HandleEvent` updating the active camera's aspect via `Camera::SetAspect` (fixes drag-resize too)
- [x] 8.3 `InputUtil`: `BindWindow` + `SetCursorGrabbed`/`SetCursorVisible` (SFML grab/hide) + grabbed-mode delta accumulation + window-center recentering in `Engine::HandleEvent` MouseMoved; `ConsumeMouseDelta` read-and-clear
- [x] 8.4 `CharacterController`: first-person mode (delta look, yaw on body, pitch clamp, no LMB drag) + `SetMouseSensitivity`; both serialized (first_person/mouse_sensitivity keys)
- [x] 8.5 `Camera::SetFov` (re-projects preserving ratio/near/far) + Lua binding
- [x] 8.6 Full `tests/lua` + ASAN pass for the plumbing changes (2026-09-27: full suite green with per-test scene/cwd conventions; ASAN clean on the UI + physics areas)

## 9. Island demo shell

- [x] 9.1 Player setup folded into `IslandGame.lua` on_init (self-contained demo): Player node + CharacterController (height 180 cm, radius 35 cm, camera height 170 cm, first-person, no mesh), spawn picked by probing `Terrain:GetHeight` above the 400 cm waterline (fallback: high drop onto the island); terrain heightfield statics already ship in ocean_island.bin
- [x] 9.2 `examples/IslandGame.lua` bootstrap: scene load, pipeline from render settings, menu/game/pause state machine, ESC handling, Exit via `Window.Close`
- [x] 9.3 Demo UI assets under `Projects/ocean/ui/`: shared ui.rcss (display reset - RmlUi has no UA sheet, dark translucent panels, accent bar, hover/pressed, transitions, slider track/handle styles) + `main_menu.rml`, `pause_menu.rml`, `options.rml`, `hud.rml` (crosshair + FPS counter, pointer-events:none so the HUD never captures)
- [x] 9.4 Menu flow wiring from Lua: Start (spawn + grab cursor), ESC pause (release cursor, cut input), Continue, Exit to Main Menu (drift camera restore), Exit to Desktop; verify player state frozen across pause
- [x] 9.5 Options panel wiring: fps cap 60/90/144/uncapped, vsync, resolution 1280x720/1920x1080/2560x1440/3840x2160, postfx master, SSAO, SSR (+ocean SSR), FXAA, CSM on/off, shadow map size, shadow far, FOV, mouse sensitivity; live apply via RenderSettings/ApplyRenderSettings + group-8 bindings
- [x] 9.6 Settings persistence: flat key=value cfg via Lua `io` at `Projects/ocean/game_settings.cfg`; load+apply at boot; Options shows persisted values; restart-verify
- [x] 9.7 Sanity-pass island-scale ranges: camera far plane, CSM shadow-far/split blend for 1 km sightlines; pick demo defaults
- [x] 9.8 Style pass against AAA reference: captures of hover/pressed/transition states; tune dp sizes, spacing, font effects

## 10. Demo verification, tests, docs

- [x] 10.1 Agent walk via gui-cli: `gui tree`/`inspect`/`event`/`shot` against every demo document and state (main menu, options, pause, HUD); commit reference screenshots
- [x] 10.2 `tests/lua/game_ui.lua`: headless asserts — document load, tree structure, show/hide, data-model update, event dispatch; standard `os.exit(code)` pattern
- [x] 10.3 `tests/lua/game_ui_demo.lua`: headless menu-flow asserts — state transitions driven by dispatched events (Start -> InGame, ESC -> Pause, Exit to Main Menu), settings module round-trip to JSON
- [x] 10.4 `tests/lua/game_ui_capture.lua` + run note: capture gating assertions (WantCapture under pointer-over-UI vs not)
- [x] 10.5 Full `tests/lua` suite + ASAN build pass (fixed en route: Texture::CreateEmpty null-GL guard for headless exec, stale SetDebugDraw refs, terrain/particles/hdr tests' stale scene paths, PhysicsWorld Jolt-globals teardown UAF now via ShutdownJoltGlobals; deleted GL-bound _test_overlay/_test_simple scratch probes; gen_lod/smoke_save are parameterized tools, kraut_import_roundtrip needs its generated fixture)
- [x] 10.6 Docs: UI authoring notes (asset layout, RML/RCSS basics, Lua API, gui-cli loop, font/SDF limitation, debugger toggle) + demo README (controls, settings, how to run); CLAUDE.md trap entries if new ones surfaced
- [x] 10.7 USER interactive verify: play the island demo (menu flow, options, FPS feel), then sync specs + archive (user playtest 2026-09-27)
