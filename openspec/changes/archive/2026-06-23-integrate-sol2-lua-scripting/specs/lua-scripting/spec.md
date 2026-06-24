## ADDED Requirements

### Requirement: Lua SHALL be vendored as a pinned git submodule

The engine SHALL include `Lua` at `engine/ThirdParty/lua/`, registered as a git submodule pointing at the upstream `lua/lua` repository, pinned to release tag `v5.4.7`. Lua's upstream build is a Makefile (no CMake); the engine SHALL provide a hand-rolled CMake wrapper that compiles the Lua C sources at `engine/ThirdParty/lua/*.c` (the upstream `lua/lua` repo lays sources flat at the repo root, not under a `src/` subdirectory) into a static library target `lua` consumable by other engine targets. The standalone-interpreter `main` (`lua.c`) and the alternative single-file build (`onelua.c`) SHALL be excluded from the static library to avoid duplicate-symbol errors.

#### Scenario: Fresh clone resolves the Lua submodule at the pinned tag
- **WHEN** a developer clones the fury3d repository and runs `git submodule update --init --recursive`
- **THEN** `engine/ThirdParty/lua/lua.h` exists on disk
- **AND** `git -C engine/ThirdParty/lua rev-parse HEAD` resolves to the same SHA as `git -C engine/ThirdParty/lua rev-parse v5.4.7`

#### Scenario: Engine links against the static lua library
- **WHEN** the `fury` engine target is built
- **THEN** symbols of the form `luaL_newstate`, `lua_pcall`, `lua_pushcfunction` appear in the resulting binary (verifiable via `nm`)

#### Scenario: lua.c and onelua.c are excluded from the static lib
- **WHEN** the static `lua` library is built
- **THEN** the resulting object set does not include the standalone interpreter's `main` symbol from `lua.c`, nor the single-file build's `main` from `onelua.c`

### Requirement: sol2 SHALL be vendored as a pinned git submodule

The engine SHALL include `sol2` at `engine/ThirdParty/sol2/`, registered as a git submodule pointing at the upstream `ThePhD/sol2` repository, pinned to release tag `v3.5.0`. sol2 is header-only; the engine SHALL add `engine/ThirdParty/sol2/include` to its include path. v4.0.0-alpha is an unstable pre-release and SHALL NOT be selected.

#### Scenario: Fresh clone resolves the sol2 submodule at the pinned tag
- **WHEN** a developer clones the fury3d repository and runs `git submodule update --init --recursive`
- **THEN** `engine/ThirdParty/sol2/include/sol/sol.hpp` exists on disk
- **AND** `git -C engine/ThirdParty/sol2 rev-parse HEAD` resolves to the same SHA as `git -C engine/ThirdParty/sol2 rev-parse v3.5.0`

#### Scenario: Engine compiles against vendored sol2
- **WHEN** an engine source file `#include`s `<sol/sol.hpp>`
- **THEN** the include resolves and the file compiles without requiring sol2 to be installed system-wide

### Requirement: Engine SHALL own the main loop via Engine::Run

The engine SHALL provide a static method `Engine::Run` that owns the application's main loop. `Engine::Run` SHALL drive: SFML event polling and dispatch through `Engine::HandleEvent`, the fixed-timestep loop (default 25 Hz with a maximum of 5 frame-skips per render, matching the previous Demo.cpp behavior), `RenderUtil::BeginFrame` / `EndFrame` framing, `Gui::NewFrame` per-frame timing, and `window.display()`. The caller SHALL provide a callback bundle for `OnInit` (called once after engine init), `OnUpdate(float dt)` (called per render frame), `OnFixedUpdate()` (called per fixed tick), and `OnShutdown` (called once before engine shutdown). Demo source code SHALL NOT contain its own `while (window.isOpen())` loop or its own fixed-timestep math.

#### Scenario: Engine::Run drives the loop
- **WHEN** a host program calls `Engine::Run(window, callbacks)`
- **THEN** the loop runs until either the SFML window closes or a callback signals exit
- **AND** `OnInit` fires exactly once before the first frame
- **AND** `OnUpdate(dt)` fires once per rendered frame with `dt` in seconds
- **AND** `OnFixedUpdate` fires zero-or-more times per frame to catch up to the 25 Hz fixed-tick schedule, capped at 5 catch-up ticks per frame
- **AND** `OnShutdown` fires exactly once after the loop exits

#### Scenario: Demo source has no loop math
- **WHEN** the working tree is inspected
- **THEN** no source file under `examples/` contains a `while (window.isOpen())` loop, an `sf::Event` polling loop, or `next_game_tick`-style fixed-timestep accumulator math

### Requirement: Engine SHALL build as an executable target

The Fury3D build SHALL produce an executable target named `fury` that statically links the engine, Lua, and sol2 into a single binary. The executable SHALL accept an optional command-line argument that is the path to a Lua script to execute, defaulting to `Demo.lua` in the working directory if no argument is given. The shared library form (`libfury.dylib` / `.so` / `.dll`) SHALL remain available behind the `BUILD_SHARED_LIBS` CMake option (default OFF for the new common case) for users who want to embed the engine.

#### Scenario: Default build produces an executable
- **WHEN** a developer runs `cmake -S engine -B build && cmake --build build`
- **THEN** an executable file at `build/fury` (or `build/fury.exe` on Windows) is produced
- **AND** the previous `libfury.dylib` is NOT produced unless `BUILD_SHARED_LIBS=ON` is explicitly set

#### Scenario: Executable accepts a script path argument
- **WHEN** the `fury` executable is invoked as `./fury path/to/script.lua`
- **THEN** the engine starts and executes `path/to/script.lua` as the demo script

#### Scenario: Executable falls back to Demo.lua
- **WHEN** the `fury` executable is invoked with no arguments and `./Demo.lua` exists in the working directory
- **THEN** the engine starts and executes `Demo.lua`

### Requirement: Engine SHALL expose a Lua binding surface for Demo-driven types

The engine SHALL register a sol2 `usertype` for each of the following engine types so Lua scripts can call their methods: `Vector4`, `Quaternion`, `MathUtil` (free functions), `OcTree`, `Scene`, `SceneNode`, `Camera`, `Transform`, `Component` (base type only), `Pipeline`, `PrelightPipeline`, `FileUtil` (free functions), `LogLevel` (enum), `RenderUtil`, `Gui` (free functions). The `Engine.run` callback registration hook SHALL be reachable from Lua. The bridge SHALL live in `engine/Fury/LuaBindings.{h,cpp}` and SHALL be invoked once at engine startup (typically inside `Engine::Initialize` or in the new launcher's `main`).

#### Scenario: Lua can construct a Vector4
- **WHEN** a Lua script executes `local v = Vector4.new(1.0, 2.0, 3.0, 1.0)`
- **THEN** `v` is a usertype value whose `:Length()`, `.x`, `.y`, `.z`, `.w` accessors return the C++-side values

#### Scenario: Lua can drive Scene::Active and Pipeline::Active
- **WHEN** a Lua script assigns `Scene.Active = Scene.Create("main", FileUtil.GetAbsPath(), octree)`
- **THEN** subsequent C++ code that reads `Scene::Active` sees the same `shared_ptr` instance

#### Scenario: Lua can register Engine.run callbacks
- **WHEN** a Lua script calls `Engine.run({ on_init = f1, on_update = f2, on_fixed_update = f3, on_shutdown = f4 })`
- **THEN** the engine's main loop fires each callback at the right times, with `f2` receiving a `dt` number

### Requirement: Demo SHALL be authored as Lua

The C++ source file `examples/Demo.cpp` SHALL be deleted. A functionally-equivalent `examples/Demo.lua` SHALL replace it: same camera setup (perspective FOV 0.7854 rad, aspect 1.778, near 1, far 100, shadow far 30, shadow bounds [-5,5]), same scene loaded from `Resource/Scene/scene.bin`, same pipeline loaded from `Resource/Pipeline/DefferedLightingLambert.json`. When run via `fury Demo.lua`, the resulting visible output SHALL be observably identical to the pre-change C++ demo.

#### Scenario: Demo.cpp is gone
- **WHEN** the working tree is inspected
- **THEN** `examples/Demo.cpp` does not exist
- **AND** `examples/Demo.lua` exists

#### Scenario: Demo.lua produces the same scene
- **WHEN** a developer runs `fury examples/Demo.lua` from the repo root (with the Resource directory accessible)
- **THEN** an SFML window opens, the existing scene from `Resource/Scene/scene.bin` renders, the deferred lighting pipeline executes, and no `EROR`-level entries appear in `Log.txt`

### Requirement: Repository SHALL include a Lua bindings reference doc

The repository SHALL include `docs/LUA.md` documenting the Lua bindings. The doc SHALL contain at minimum: a one-paragraph overview of how the Lua launcher works, an `Engine.run` callback contract section (the four callback names, their signatures, ordering guarantees), one section per exposed engine type listing its bound methods, a hello-world Lua snippet, and a "Known gotchas" section covering sol2's `shared_ptr`/`weak_ptr` semantics and SFML 3 scoped-enum spellings as seen from Lua.

#### Scenario: docs/LUA.md exists and is non-empty
- **WHEN** the working tree is inspected
- **THEN** `docs/LUA.md` exists and is at least 100 lines long
- **AND** the doc contains a level-1 or level-2 heading mentioning "Engine.run" or "Engine::Run"
- **AND** the doc contains a hello-world code fence written in Lua
