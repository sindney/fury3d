## Why

The engine has no shipped game UI: in-game UI today is the same immediate-mode ImGui path the editor uses (`Gui` Lua table) — code-only layout, no styling system, no retained/inspectable element tree. Games need declarative, styleable HUD/menus/inventory UI with proper layout and font effects, and agent-driven UI work needs an introspectable tree plus screenshots. A three-way evaluation (RmlUi vs TGUI vs Nuklear, verified against the cloned sources) picked RmlUi: it is the only candidate with CSS-like styling (RML/RCSS: flexbox/table/absolute layout, anchors, animations, data binding, ninepatch decorators), the only one with in-tree Lua bindings, its `RenderInterface` is exactly the mesh+texture handout our render-thread architecture wants, and its Debugger plugin + element introspection API is the ideal base for agent tooling. Font rendering is FreeType bitmap with outline/shadow/glow effects (no SDF — a pluggable `FontEngineInterface` leaves an upgrade path). Integration needs no upstream patches: upstream CMake guards `if(NOT TARGET Lua::Lua)` and skips install rules under `add_subdirectory`, samples/tests no-op when off, so RmlUi vendors as a plain submodule.

## What Changes

- Vendor RmlUi (MIT, v6.x) as a git submodule under `engine/ThirdParty/RmlUi`, integrated via `add_subdirectory` (SFML/Jolt precedent): predefined `Lua::Lua` target pointing at our vendored Lua 5.4, `RMLUI_LUA_BINDINGS=ON`, FreeType vendored under `engine/ThirdParty/freetype` with `FindFreetype` steered to it via standard cache vars. No fork, no upstream patches.
- New `GameUI` engine system: owns RmlUi contexts, implements `SystemInterface` (engine clock) and `FileInterface` (engine VFS/pak-aware), forwards SFML events at the existing `Engine.cpp HandleEvent` choke point with UI capture gating, updates contexts on the game thread.
- New `GameUIRenderer` implementing RmlUi `RenderInterface`: `CompileGeometry`/`RenderGeometry`/`GenerateTexture` emit engine draw data snapshotted into the frame packet (the `Gui.cpp BuildDrawDataSnapshot`/`RenderSnapshot` pattern) and replayed on the GL thread after the postfx chain, GLSL 330 core, bind-every-sampler hygiene. UI textures upload as engine `Texture` (sRGB=false) via render-thread dispatch.
- Fonts: RmlUi default FreeType engine, game-loadable font assets, RCSS font effects; DP-ratio scaling for window DPI.
- Lua: `Rml::Lua::Initialise(engine lua_State)` so RML inline handlers and Lua data models run in the existing sol2 state, plus a `GameUI` table in `LuaBindings.cpp` (show/hide document, data-model helpers, events, custom element registration).
- RmlUi Debugger plugin (visual inspector) available in editor/dev builds.
- New `furye-cli gui` subcommand family (kraut/render-mesh precedent): load a scene+document headless with a hidden GL window, dump the element tree, inspect element geometry/computed style, dispatch synthetic events, capture screenshots — the agent build/test loop for UI.
- RmlUi Debugger plugin (visual inspector) available in editor/dev builds.
- **Demo game shell on `ocean_island`** (the integration test): boot into the island with a cinematic drift camera + main menu (Start / Options / Exit); Start spawns a 1.8m FPS player (Jolt `CharacterVirtual` capsule on the terrain heightfield statics, adapted from the `Player.lua`/`CharacterController` TPS pattern with `CameraDistance(0)`); ESC opens a pause menu (Continue / Options / Exit to Main Menu / Exit to Desktop); one shared Options panel; crosshair + FPS-counter HUD; simple modern AAA styling (dark translucent panels, accent line, hover/pressed/transition states).
- **Options-menu settings plumbing** (small, bounded C++ additions so the menu is real, not decorative): runtime FPS cap (60/90/144/uncapped) + vsync via new `Window` Lua bindings, windowed resolution 1280x720/1920x1080/2560x1440/3840x2160 via `setSize` + a resize consumer (camera aspect + render surface), cursor grab/hide + relative mouse look for FPS, `Camera:SetFov` Lua binding, `CharacterController` mouse-sensitivity + first-person mode. Settings already Lua-ready and wired into the menu: post-process master + per-effect (SSAO/SSR/FXAA), HDR, CSM shadows on/off + map size + shadow far, ocean SSR/foam/choppiness/wind, sky time-of-day, FOV, mouse sensitivity, walk/run speed. Settings persist to a JSON file via Lua `io`. (The engine has no planar reflections — the menu exposes SSR toggles instead; fullscreen is deferred because `sf::Window::create` recreates the GL context and would invalidate all cached GL objects.)
- Demo UI assets (HUD + menu) under `examples/Projects/` and `tests/lua/` coverage.

## Capabilities

### New Capabilities
- `game-ui`: RmlUi integration into the engine runtime — context lifecycle, document (RML/RCSS) loading from project assets, SFML input forwarding with capture gating, render-thread-safe geometry/texture handout into the frame pipeline, font and DP-scale handling, debugger access.
- `game-ui-lua`: Lua exposure of the game UI — RmlUi Lua plugin in the engine state, `GameUI` Lua API, data models and event handlers from game scripts, custom element registration.
- `game-ui-cli`: `furye-cli gui` subcommands for headless UI inspection and screenshots (element tree dump, element inspect, synthetic event dispatch, PNG capture) enabling agent-driven UI iteration.
- `game-ui-demo`: the island demo game shell — main/pause menu flow, options menu with live engine settings + persistence, FPS player controller on ocean_island, HUD, AAA-style RCSS styling. Doubles as the end-to-end verification of the above three capabilities; includes the small engine plumbing the settings menu needs (runtime fps cap/vsync, windowed resolution, cursor grab, FOV/sensitivity bindings).

### Modified Capabilities
<!-- None: input forwarding, frame-packet UI snapshot, and cli subcommands are additive extensions owned by the new capability specs, following the kraut-toolchain/render-mesh-cli precedent. -->

## Impact

- **New dependencies**: `engine/ThirdParty/RmlUi` (submodule, MIT), `engine/ThirdParty/freetype` (vendored, FTL). No other external deps.
- **Build**: `engine/CMakeLists.txt` — add_subdirectory(RmlUi) with predefined `Lua::Lua`, freetype vendoring + FindFreetype steering, link `rmlui_core`/`rmlui_debugger`/`rmlui_lua` into all three binaries.
- **Runtime touch points**: `Engine.cpp` (event forwarding + update), `RenderThread`/`FramePacket` (UI draw-data snapshot channel, mirroring `guiFrame`), `LuaBindings.cpp` (GameUI table + new `Window`/camera settings bindings), `Gui` capture gating interplay.
- **Demo plumbing touch points**: `Window`/SFML wrapper (runtime `setFramerateLimit`/`setVerticalSyncEnabled`/`setSize`), `InputUtil` (cursor grab/hide + relative mouse delta), `CharacterController` (first-person mode + sensitivity), resize consumer (camera aspect + render surface), `examples/Player.lua`-style bootstrap.
- **CLI**: `engine/Fury/Cli.cpp` — `gui` subcommand family on `furye-cli` (hidden-window GL path like `render-mesh`).
- **Assets**: RML/RCSS/font/image UI assets plus menu/HUD documents and the island spawn script under `examples/Projects/`, resolved through the content-folder/pak pipeline.
- **Docs/tests**: `tests/lua/game_ui*.lua`, `FURY_UI_DEBUG` env hook, UI authoring notes.
