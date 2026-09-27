# game-ui Specification

## Purpose
TBD - created by archiving change add-rmlui-game-ui. Update Purpose after archive.
## Requirements
### Requirement: RmlUi SHALL be vendored as an unmodified submodule integrated via add_subdirectory

RmlUi SHALL be vendored as a git submodule at `engine/ThirdParty/RmlUi`, pinned to a v6.x release tag, and built via `add_subdirectory` in `engine/CMakeLists.txt` with `RMLUI_SAMPLES`/`RMLUI_TESTS` OFF and `RMLUI_LUA_BINDINGS` ON. The engine SHALL predefine a `Lua::Lua` target wrapping its vendored Lua 5.4 before the `add_subdirectory` call so upstream's `find_package(Lua)` is skipped. FreeType SHALL come from the hermetic build SFML Graphics already FetchContent-builds (VER-2-14-3, optional deps disabled), with RmlUi's `find_package(Freetype)` redirected to it via FetchContent `OVERRIDE_FIND_PACKAGE` — one freetype for the whole engine, no separate vendoring. The integration SHALL NOT require patches to upstream RmlUi sources (no fork).

#### Scenario: Clean configure and build

- **WHEN** a developer configures and builds the engine from a clean clone with submodules
- **THEN** all three binaries (`fury`, `furye`, `furye-cli`) link `rmlui_core`, `rmlui_debugger`, and `rmlui_lua`
- **AND** no RmlUi samples, tests, or backends are built
- **AND** the configure log shows FreeType resolved to the vendored copy

#### Scenario: No upstream patches

- **WHEN** the submodule is checked out at the pinned tag
- **THEN** `git diff` inside `engine/ThirdParty/RmlUi` is empty

### Requirement: The engine SHALL own an RmlUi context updated on the game thread

A `GameUI` system SHALL create one RmlUi context ("main") sized in dp to the window, update it once per frame on the game thread, and resize it (dimensions + density-independent pixel ratio) on window resize/DPI change. RmlUi system services SHALL come from the engine: time from the engine clock, file access through an RmlUi `FileInterface` that routes through the engine VFS so RML/RCSS/images/fonts resolve from content folders and paks like other assets.

#### Scenario: Startup creates the context

- **WHEN** the engine initializes with a window
- **THEN** the "main" RmlUi context exists with dimensions matching the window's dp size

#### Scenario: Window resize

- **WHEN** the window is resized or moved to a display with a different DPI scale
- **THEN** the context dimensions and dp ratio are updated so UI layout and text physical size stay correct

### Requirement: Documents SHALL load from engine content paths

`GameUI` SHALL load RML documents by content-relative path (e.g. `ui/hud.rml`), resolving the document and its RCSS/font/image references through the engine VFS (content folders, working-dir rewrite rules, and paks). Load failures SHALL be reported on the log with the failing path and SHALL NOT crash or abort the frame.

#### Scenario: Load a HUD document from a project

- **WHEN** a game script calls `GameUI.LoadDocument("ui/hud.rml")` and the asset exists under the project content folder
- **THEN** the document loads, its referenced stylesheets and images resolve through the same VFS rules
- **AND** the document can be shown on screen

#### Scenario: Missing document

- **WHEN** a game script loads a document path that does not exist
- **THEN** the load returns a failure, the log names the missing path
- **AND** the engine keeps running with no UI added

### Requirement: SFML input SHALL be forwarded to the UI with capture gating

Window events SHALL be forwarded to the RmlUi context at the existing `Engine.cpp HandleEvent` choke point (mouse move/button/wheel, key down/up, text input as unicode), using the same mapping as RmlUi's official SFML platform backend. The engine SHALL expose `GameUI.WantCaptureMouse()` and `GameUI.WantCaptureKeyboard()` (C++ and Lua) so game code can gate world input behind UI, mirroring the existing `Gui` capture API.

#### Scenario: Click on a UI button

- **WHEN** the user clicks on a visible button element
- **THEN** the click is forwarded to the context, the button's click event fires
- **AND** `WantCaptureMouse()` reports the UI wants the mouse so the game does not also process the click

#### Scenario: Click outside the UI

- **WHEN** the user clicks where no UI element is present
- **THEN** the context reports the event unhandled
- **AND** game input proceeds normally

#### Scenario: Text input into a UI field

- **WHEN** an input element has focus and the user types
- **THEN** `sf::Event::TextEntered` unicode reaches the context and the element's text updates

### Requirement: UI geometry and textures SHALL render through the engine's render-thread pipeline

RmlUi SHALL never call GL. The engine's `RenderInterface` implementation SHALL record compiled geometry (vertex/index buffers) and draw batches (geometry, translation, texture, scissor) into a snapshot on the game thread, carry the snapshot in the frame packet, and replay it on the GL thread after the postprocess chain and before the editor overlay, using a GLSL 330 core shader, per-batch scissor, premultiplied-alpha blending, and every texture sampler bound on every draw. UI textures from `GenerateTexture`/`LoadTexture` SHALL be created as engine `Texture` objects (sRGB=false) with GL upload dispatched through the render thread; in no-GL contexts texture callbacks SHALL register metadata only.

#### Scenario: HUD draws over the 3D scene

- **WHEN** a scene with a shown HUD document runs in the `fury` player
- **THEN** the HUD draws on top of the postprocessed frame
- **AND** a captured screenshot shows the expected UI pixels

#### Scenario: Render-thread parity

- **WHEN** the same scene runs with the render thread enabled and disabled
- **THEN** the rendered UI is identical
- **AND** no GL-thread guard assert fires in debug builds

#### Scenario: Headless exec does not touch GL

- **WHEN** a document is loaded and queried under `fury exec` (no GL context)
- **THEN** layout and element queries work
- **AND** no GL call is attempted and the process exits cleanly

### Requirement: Font faces SHALL load from assets with effects and fallback support

The engine SHALL load TTF/OTF font faces through the VFS via RmlUi's FreeType font engine, including fallback faces, and SHALL support RCSS font effects (outline, shadow, glow) and dp-ratio scaling of text.

#### Scenario: Styled text in a document

- **WHEN** a document references a loaded font face and applies a font effect
- **THEN** text renders with the correct face and effect at the window's dp scale

### Requirement: Dev builds SHALL expose the RmlUi visual debugger

Editor/dev binaries SHALL be able to toggle the RmlUi Debugger plugin (visual element inspector) at runtime, gated by a `FURY_UI_DEBUG` env hook and/or editor menu toggle, following the existing debug-toggle conventions.

#### Scenario: Toggle the inspector

- **WHEN** the developer enables UI debug in a dev build
- **THEN** the RmlUi debugger overlay appears and shows element info for the hovered element

