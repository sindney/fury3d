## Context

Fury3D today builds a `libfury` shared library that's linked into a separate `examples/demo` C++ executable. Every demo iteration requires a C++ recompile + relink, which is the dominant cost in the dev loop. ARCHITECTURE.md §15 already commits to vendoring sol2 once "the engine boundary stabilises"; that boundary just stabilised in commit `cc79a90` (FBX removed, SFML 3 done, build self-contained). This change is the long-promised second phase: rewire the top-level usage model so demos are Lua scripts and the engine itself is the executable that loads them.

The local sibling clone at `furyengine/sol2` is at `c1f95a77` (one commit past v3.5.0). v3.5.0 is the latest stable tag; v4.0.0-alpha is unstable. sol2 is header-only but not zero-cost: the templated `usertype` registration generates a lot of code, so binding declarations should live in **one** translation unit (`LuaBindings.cpp`) to bound compile time.

sol2 needs a Lua runtime. Lua's upstream is a Makefile-only project (`https://github.com/lua/lua`); the latest stable release at time of writing is `v5.4.7`. sol2 v3.x supports Lua 5.1 / 5.2 / 5.3 / 5.4 + LuaJIT. We pick **Lua 5.4** (not LuaJIT) because: (a) LuaJIT's macOS Apple Silicon support has historically been finicky and the perf delta doesn't matter at our scale; (b) Lua 5.4 has the cleanest C API surface; (c) sol2's docs and examples assume 5.x by default.

Demo.cpp's actual surface (after the SFML 3 port) is small and well-bounded: a fixed-timestep loop, an SFML event pump, a scene+camera+pipeline setup that calls 12 engine types and ~50 methods. The user-facing scope of "bridge almost everything" is genuinely covered by binding what Demo.cpp uses end-to-end — anything beyond that is dead code in this milestone. Wider bindings (Light, MeshRender, Material, AnimationPlayer, InputUtil signals) come next, in their own changes, once the bridge pattern is shaken out.

The user explicitly asked that the **fixed-timestep loop move from `Demo.cpp` into the engine**. That's a real C++ API change, not just a binding: `Engine::Run(window, callbacks)` becomes the canonical way to start the engine, and Lua scripts hand it a callback table instead of writing the loop themselves. This also means the new `fury` executable can be a few dozen lines of C++ (open Lua state → register bindings → load the script → call `Engine.run` from Lua) — no demo-specific logic in the launcher.

## Goals / Non-Goals

**Goals:**
- Vendor Lua 5.4.7 and sol2 v3.5.0 as git submodules under `engine/ThirdParty/`.
- Move the fixed-timestep loop into the engine via `Engine::Run`.
- Build a single `fury` executable that loads and runs a `.lua` script (path on argv, defaulting to `Demo.lua`).
- Bridge the Demo-driven engine surface (Vector4, Quaternion, MathUtil, OcTree, Scene, SceneNode, Camera, Transform, Component, Pipeline, PrelightPipeline, FileUtil, LogLevel, RenderUtil, Gui, Engine.run hook) so a Lua script can reproduce the existing C++ demo verbatim.
- Replace `examples/Demo.cpp` with `examples/Demo.lua`. Verify visually that the Lua-driven build produces the same scene.
- Add `docs/LUA.md` covering the API surface and the `Engine.run` callback contract.

**Non-Goals:**
- Hot-reload of Lua scripts on file change. Requires a watcher and some thinking about component lifetimes — separate change.
- Bindings for Light, MeshRender, Mesh, Material, AnimationClip, AnimationPlayer, InputUtil signals (OnKeyDown etc.), BoxBounds, Frustum, Color, Texture. Each is a small follow-up.
- Lua-side error messages with stack traces (sol2 supports `sol::protected_function_result` + `lua_setpanicf`; we use the default panic this round and let it crash with a readable message).
- Sandboxing. Lua scripts have full `io`/`os`/`package` access. Acceptable because the engine is a dev tool right now.
- ImGui-from-Lua bindings. `lua-imgui` exists upstream but isn't vendored here.
- A test harness for the bindings — pytest + pybind11 for the math layer is its own §14 work, upstream of this change.

## Decisions

### D1. Vendor Lua 5.4.7 as a submodule + custom CMake wrapper

**Choice**: `git submodule add https://github.com/lua/lua engine/ThirdParty/lua && cd engine/ThirdParty/lua && git checkout v5.4.7`. Lua's upstream has a `Makefile` and no `CMakeLists.txt`. We add a small CMake wrapper either inline in `engine/CMakeLists.txt` or as `engine/cmake/lua.cmake`:

```cmake
file(GLOB LUA_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/lua/src/*.c)
list(REMOVE_ITEM LUA_SRC
    ${PROJECT_SOURCE_DIR}/ThirdParty/lua/src/lua.c       # standalone interpreter main
    ${PROJECT_SOURCE_DIR}/ThirdParty/lua/src/luac.c)     # bytecode compiler main
add_library(lua STATIC ${LUA_SRC})
target_include_directories(lua PUBLIC ${PROJECT_SOURCE_DIR}/ThirdParty/lua/src)
target_compile_definitions(lua PRIVATE
    $<$<PLATFORM_ID:Darwin>:LUA_USE_MACOSX>
    $<$<PLATFORM_ID:Linux>:LUA_USE_LINUX>)
target_link_libraries(lua PUBLIC $<$<PLATFORM_ID:Darwin>:m dl> $<$<PLATFORM_ID:Linux>:m dl>)
```

`LUA_USE_MACOSX` enables `dlopen` for `require`-loaded C modules and proper `os.tmpname` behavior; without it, Lua falls back to a less-functional default. We do **not** define `LUA_USE_READLINE` because we don't ship a REPL (and pulling in readline introduces a system dep we'd rather not have).

**Rationale**: Submodules match the SFML/tinygltf/rapidjson pattern. v5.4.7 is the latest stable. The custom wrapper is ~15 lines and avoids pulling in a third-party Lua-CMake project. No system Lua needed.

**Alternatives considered**:
- **LuaJIT submodule**: faster execution, but LuaJIT's macOS arm64 build path has historically been finicky (requires `MACOSX_DEPLOYMENT_TARGET` shenanigans, the upstream Makefile assumes x86_64). Performance gap is irrelevant for this engine's workload. Rejected.
- **System-installed Lua via `find_package(Lua)`**: same anti-pattern we just removed for SFML and rapidjson. Rejected for consistency.
- **Vendor Lua via FetchContent_Declare**: adds a network requirement at configure time. Rejected; matches D1 of the previous change.
- **Use sol2's bundled `LuaBuild` script**: sol2's CMake includes a `LuaBuild` find-script that downloads Lua at configure time. Rejected for the same network-at-configure-time reason.

### D2. Vendor sol2 v3.5.0 as a submodule (header-only)

**Choice**: `git submodule add https://github.com/ThePhD/sol2 engine/ThirdParty/sol2 && cd engine/ThirdParty/sol2 && git checkout v3.5.0`. Add `engine/ThirdParty/sol2/include` to the engine's include path. No source files to compile.

**Rationale**: v3.5.0 is the latest stable; v4.0.0-alpha is the only newer tag and is explicitly experimental. sol2 is header-only — there is nothing to "build."

**Alternatives considered**:
- **Use the single-header amalgam at `single/sol/sol.hpp`**: sol2 ships a one-file version. Marginally faster compile vs. the multi-header path. Rejected because it complicates updating (the single-header is generated from the multi-header tree, and we'd need to regenerate on every bump). Multi-header includes match upstream's CI and example code.

### D3. Engine owns the loop; Lua provides callbacks via a table

**Choice**: New static method `Engine::Run(sf::Window&, const Callbacks&)` where `Callbacks` is a small POD struct with four `std::function`s. Lua-side, the `Engine.run` binding accepts a single Lua table whose keys (`on_init`, `on_update`, `on_fixed_update`, `on_shutdown`) become the corresponding `std::function` slots.

```cpp
// engine/Fury/Engine.h additions
struct EngineCallbacks {
    std::function<void()> OnInit;
    std::function<void(float dt)> OnUpdate;
    std::function<void()> OnFixedUpdate;
    std::function<void()> OnShutdown;
};
static void Run(sf::Window& window, const EngineCallbacks& cb);
```

`Engine::Run` lifts this exact body from `Demo.cpp:62-102`:

```cpp
void Engine::Run(sf::Window& window, const EngineCallbacks& cb) {
    if (cb.OnInit) cb.OnInit();
    sf::Clock clock;
    std::int32_t next_game_tick = clock.getElapsedTime().asMilliseconds();
    bool running = true;
    while (window.isOpen() && running) {
        RenderUtil::Instance()->BeginFrame();
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) { running = false; break; }
            sf::Event ev = *event;
            HandleEvent(ev);
        }
        int numLoops = 0;
        const int SKIP_TICKS = 1000 / 25;        // 25 Hz fixed
        const int MAX_FRAMESKIP = 5;
        while (clock.getElapsedTime().asMilliseconds() > next_game_tick && numLoops < MAX_FRAMESKIP && running) {
            if (cb.OnFixedUpdate) cb.OnFixedUpdate();
            FixedUpdate();
            next_game_tick += SKIP_TICKS;
            numLoops++;
        }
        std::int32_t elapsed = clock.getElapsedTime().asMilliseconds();
        float dt = float(elapsed + SKIP_TICKS - next_game_tick) / float(SKIP_TICKS);
        next_game_tick -= elapsed;
        Gui::NewFrame(clock.restart().asSeconds());
        if (cb.OnUpdate) cb.OnUpdate(dt);
        Update(dt);  // emits Engine::OnUpdate signal
        window.display();
        RenderUtil::Instance()->EndFrame();
    }
    if (cb.OnShutdown) cb.OnShutdown();
}
```

**Rationale**: Matches LÖVE2D / Defold / Bevy `App::run` shape. Lua scripts can never get the loop math wrong. Future C++ apps that want a custom loop still bypass `Engine::Run` and call `Engine::Initialize` + their own loop directly — `Engine::Run` is layered, not mandatory.

**Alternatives considered**:
- **Engine exports loop primitives, Lua writes the while-loop**: rejected per the user's explicit guidance ("move the time-step setup to engine").
- **Move the constants `TICKS_PER_SECOND`/`MAX_FRAMESKIP` into `Engine::Run` parameters**: tempting, but deferred. The defaults match Demo.cpp; expose later if needed.

### D4. `fury` becomes the executable; `libfury` becomes opt-in

**Choice**: `engine/CMakeLists.txt` switches the default from `add_library(fury SHARED ...)` to `add_executable(fury ...)`. The launcher source (`examples/main.cpp` — small, ~50 lines) moves into the engine target's source list. A `BUILD_SHARED_LIBS=ON` user still gets a `libfury` library, but the default path produces only the executable.

The launcher's `main`:

```cpp
int main(int argc, char* argv[]) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math,
                       sol::lib::table, sol::lib::io, sol::lib::os);
    fury::LuaBindings::Register(lua);

    sf::ContextSettings settings; /* ...same as old Demo.cpp... */
    sf::Window window(sf::VideoMode({1920, 1080}), "Fury3d", /* ... */);
    fury::Engine::Initialize(window, /* ... */);

    lua["__window"] = std::ref(window);  // injected; Engine.run reads it
    const char* scriptPath = argc > 1 ? argv[1] : "Demo.lua";
    auto result = lua.safe_script_file(scriptPath);
    if (!result.valid()) {
        sol::error err = result;
        FURYE << "Lua error: " << err.what();
        return 1;
    }
    fury::Engine::Shutdown();
    return 0;
}
```

The Lua script, in turn, calls `Engine.run({on_init=..., on_update=..., ...})` whose binding pulls `__window` out of the Lua state and calls `Engine::Run(window, callbacks)` C++-side.

**Rationale**: One static binary is simpler to ship, simpler to debug, simpler to rpath. The shared-lib option stays for users who want to embed the engine; we don't go out of our way to break that path but it's no longer the default.

**Alternatives considered**:
- **Keep the libfury+demo split, add a separate fury-runtime exe**: produces three artifacts instead of one, not clearly better.
- **Make the launcher a CMake "INTERFACE" target on top of libfury**: more CMake gymnastics for no user-visible benefit.

### D5. Demo-driven minimum binding surface

**Choice**: Bind exactly what the current Demo.cpp uses end-to-end. Concrete list:

| Type | Bound members |
|------|---------------|
| `Vector4` | constructors `(x,y,z)` / `(x,y,z,w)` / `(scalar)`; `.x .y .z .w`; static `XAxis YAxis ZAxis`; `Length()`, `Normalize()`, `+ - * /` operators |
| `Quaternion` | `Identity()`; `(x,y,z,w)` ctor |
| `MathUtil` | `EulerRadToQuat(x,y,z)`, `DegToRad`, `RadToDeg`, `PI`, `HalfPI` |
| `OcTree` | static `Create(min, max, depth)` returning `OcTree::Ptr`; `WalkScene` may stay unbound |
| `Scene` | static `Create(name, dir, sceneMgr)`; static `Active` (read/write); `Manager()` accessor (returns base `SceneManager` reference) |
| `SceneNode` | static `Create(name)`; `SetLocalPosition(Vector4)`, `SetLocalRoattion(Quaternion)`, `Recompose(updateOctree=false)`, `AddComponent(Component::Ptr)` |
| `Camera` | static `Create()`; `PerspectiveFov(fov, aspect, near, far)`, `SetShadowFar(float)`, `SetShadowBounds(min, max)` |
| `Transform` | static `Create()` (used as a marker component) |
| `Component` | base type only (so `AddComponent` accepts derived types) |
| `Pipeline` | static `Active` (read/write); `SetCurrentCamera(SceneNode::Ptr)`, `Execute(SceneManager::Ptr)` |
| `PrelightPipeline` | static `Create(name)` returning `Pipeline::Ptr` |
| `FileUtil` | `GetAbsPath()`, `GetAbsPath(relative)`, `LoadCompressedFile(serializable, path)`, `LoadFile(serializable, path)` |
| `LogLevel` | enum: `EROR INFO WARN DBUG` |
| `RenderUtil` | `Instance()`; (no methods bound — internally driven by Engine::Run) |
| `Gui` | `ShowDefault(dt)`, `Render()` (other entry points are owned by Engine::Run) |
| `Engine` | `run(callbacks_table)` only — `Initialize`/`HandleEvent`/`Update`/`FixedUpdate`/`Shutdown` are launcher-level concerns and not exposed |

**Total**: ~16 usertypes, ~50 method bindings. Compiles to ~30 KB of generated code with sol2's `usertype` template machinery, in a single TU (`LuaBindings.cpp`).

**Rationale**: Smallest surface that lets `Demo.lua` reproduce `Demo.cpp` verbatim. Every binding is exercised by the demo, so we can't ship dead bindings. Wider bindings move to follow-up changes once we've shaken out compile time and binding ergonomics.

**Alternatives considered**:
- **Bind everything in `Fury.h`**: ~60 types, ~400 methods. sol2's templated registration would balloon `LuaBindings.cpp` compile time to minutes. Plus we'd have to make decisions about every type's lifetime semantics now, with no driving use case.
- **Bind through a runtime reflection layer**: rejected; the engine has no reflection, and adding one is its own multi-month effort.

### D6. shared_ptr lifetime: sol2's automatic policy is fine

**Choice**: sol2 understands `std::shared_ptr<T>` natively. When a Lua usertype is registered with `sol::usertype<T> ut = lua.new_usertype<T>("T", ...)` and a method returns `std::shared_ptr<T>`, sol2 keeps a Lua-side strong reference. The engine's `Entity`/`Component`/`SceneNode` types are all `enable_shared_from_this`-friendly, so this works without us doing anything special.

`weak_ptr` we avoid binding directly; all engine APIs that take or return `weak_ptr` also have shared variants we can prefer. If a binding must accept a `weak_ptr` parameter (currently: none in the Demo surface), we wrap it in a lambda that calls `.lock()` and asserts non-null.

**Rationale**: Aligns with sol2's documented happy path. The engine's existing reference-counting story carries through to Lua with no code changes.

**Alternatives considered**:
- **Force everyone through `Component::Ptr` etc. typedefs**: those are `shared_ptr<T>` already; the binding is the same either way.

### D7. docs/LUA.md is part of this change

**Choice**: Write `docs/LUA.md` with these sections:
- **Overview** — how the launcher works, what `fury Demo.lua` does step-by-step.
- **Engine.run callback contract** — the four callbacks, their signatures, ordering guarantees, what's allowed in each.
- **Bound API reference** — one subsection per usertype, with a table of bound members and a tiny code example.
- **Hello-world** — a 30-line `.lua` that opens a window, prints a log line every fixed-tick, exits on Escape.
- **Gotchas** — `Vector4` arithmetic returns by value (Lua sees a copy); `:Method()` vs `.Method()` (bound member fns use `:`); `Scene.Active = …` works because we register it as a property; `SceneNode.AddComponent` accepts any derived component because of the `Component` base binding.

**Rationale**: User explicitly asked. The doc grounds future expansion (Light bindings, animation bindings, etc.) — each follow-up change appends a new "Bound API reference" subsection.

## Risks / Trade-offs

- **Risk**: Lua's static lib won't link against the engine on Windows because of `LUA_BUILD_AS_DLL` mismatches. → **Mitigation**: We don't define `LUA_BUILD_AS_DLL`; the static lib gets compiled with the same export visibility as the rest of the engine. Test on the Windows CI before merge (no Windows host available right now, so this is a known gap; flag in §10 of tasks.md).
- **Risk**: sol2's heavy templates blow up compile time of `LuaBindings.cpp`. Past projects have seen 60-90s for similar binding TU sizes. → **Mitigation**: Keep all bindings in one TU so they're compiled once; mark the TU as a separate library or use precompiled headers if it becomes painful (deferred; revisit after first build).
- **Risk**: Lua's `lua_close` and the engine's static singletons race at process exit. → **Mitigation**: Order in the launcher: `lua` (sol::state) destructed → `Engine::Shutdown` → globals fall out of scope. The Lua state must outlive any C++ → Lua callback registrations, so we close it before `Engine::Shutdown`.
- **Risk**: SFML 3 enum spellings don't translate cleanly to Lua identifiers (`sf::Keyboard::Key::A` is fine, but `sf::Mouse::Button::Extra1` etc. are awkward). → **Mitigation**: We don't bind SFML enums in this change. Keyboard / mouse input bindings are deferred. `Demo.lua` uses zero input.
- **Risk**: sol2's `std::function` storage of Lua callbacks holds the Lua state captive. If `Engine::Shutdown` runs before the Lua state closes, the callbacks reference dead Lua memory. → **Mitigation**: The launcher's contract is: load the script, call `Engine.run(...)` from Lua (which blocks until the loop exits), then return from `safe_script_file`, then `Engine::Shutdown`, then `lua` falls out of scope. As long as the script doesn't store callbacks that outlive `Engine.run`'s return, we're safe. Documented in `docs/LUA.md`'s gotchas.
- **Trade-off**: The engine becoming an executable means embedders who relied on `libfury.dylib` need to flip `BUILD_SHARED_LIBS=ON`. Documented as a BREAKING in the proposal; we keep the option live so no one is hard-blocked.
- **Trade-off**: Lua 5.4 not LuaJIT. Lose some perf on a hypothetical heavy-Lua workload. Acceptable today.

## Migration Plan

This change is BREAKING for two distinct downstream populations:

1. **Demo authors who edited `Demo.cpp`**: port to `Demo.lua`. The C++→Lua mapping is mostly mechanical (`Vector4(...)` → `Vector4.new(...)`, `someNode->Method()` → `someNode:Method()`, `Scene::Active = X` → `Scene.Active = X`). The bound API closely mirrors the C++ API.
2. **Anyone embedding `libfury`**: pass `-DBUILD_SHARED_LIBS=ON` at configure time to keep the previous behavior. The shared-lib path remains supported.

**Rollback strategy**: `git revert` the merge commit and `git submodule deinit engine/ThirdParty/lua engine/ThirdParty/sol2`. The pre-change `examples/Demo.cpp` is recoverable from history.

## Open Questions

None blocking this change. The deferred items in ARCHITECTURE.md §15 (PBR, >4 bones, ticks-vs-seconds, `Serializable` future, `Signal` lambdas) are unaffected by Lua scripting and stay open in their own right.
