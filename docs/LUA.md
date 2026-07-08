# Fury3D — Lua scripting

> Status (2026-07-07): minimum viable bridge — exposes the engine API surface
> that `examples/Editor.lua` actually exercises. Wider bindings (Light,
> MeshRender, Material, AnimationPlayer, InputUtil signals, etc.) are deferred
> to follow-up changes.

## API reference

The per-binding Lua API reference lives at **[`docs/LUA_API.md`](LUA_API.md)**,
auto-generated from `engine/Fury/LuaBindings.cpp` at build time. That file is
the source of truth for what's bound; this doc focuses on narrative —
callback contracts, runtime flags, gotchas, recipes.

For the **C++ side** of the bindings (signatures, conventions, how to add a
new binding), read `engine/Fury/LuaBindings.cpp` directly.

## How it works

The `fury` executable is a Lua launcher. It:

1. Opens an `sf::Window` at 1920×1080 with depth-24 / stencil-8 / GL 3.3 context settings (matching the previous C++ demo).
2. Calls `fury::Engine::Initialize(...)` to bring up the engine subsystems.
3. Opens a `sol::state` and calls `fury::LuaBindings::Register(lua)` to wire engine types into the script's global namespace.
4. Injects the active SFML window into Lua as `__window` (a private global the bindings read; don't shadow it in your scripts).
5. Loads and executes the script at `argv[1]`, defaulting to `Editor.lua` in the working directory.
6. The script calls `Engine.run({...callbacks...})` — that drops into C++ and runs the main loop until the window closes.
7. After `Engine.run` returns the Lua state is closed, then `fury::Engine::Shutdown()` runs, then the process exits.

## `Engine.run` callback contract

```lua
Engine.run({
    on_init         = function() ... end,    -- called once before the loop starts
    on_update       = function(dt) ... end,  -- called once per render frame; dt is a float (seconds-fraction since last fixed tick)
    on_fixed_update = function() ... end,    -- called 0..MAX_FRAMESKIP times per frame to catch up to 25 Hz
    on_shutdown     = function() ... end,    -- called once after the loop exits
}, {
    max_fps         = 144,    -- optional, default 144. 0 (or false) disables the cap.
    gui_scale       = 1.0,    -- optional, default 1.0. ImGuiStyle::ScaleAllSizes multiplier.
    gui_font_scale  = 1.0,    -- optional, default 1.0. Assigned to ImGuiIO::FontGlobalScale.
})
```

All four callbacks are optional. Omitting one is equivalent to passing `nil` — the engine just won't invoke that hook.

The second argument (the options table) is also optional. Calling `Engine.run({...callbacks...})` with no second arg is fine — defaults apply.

**Options:**

- `max_fps` (number, default `144`): frame-rate cap applied via `sf::Window::setFramerateLimit`. `0`, `false`, or a negative number disables the cap (the demo will run as fast as the host allows). Per-frame work + the underlying SFML limiter define the actual upper bound; on macOS expect ±5 FPS slack.
- `gui_scale` (number, default `1.0`): multiplier passed to `ImGuiStyle::ScaleAllSizes`. Controls widget sizes, padding, borders. `1.5` makes the UI about 50% larger.
- `gui_font_scale` (number, default `1.0`): assigned directly to `ImGuiIO::FontGlobalScale`. Controls only the bitmap font size. Usually keep this equal to `gui_scale`.

Unknown keys in the options table are silently ignored, so you can leave a `vsync = true` (or similar) entry in your script and it won't error — it just won't do anything yet.

Ordering guarantees per frame:
1. `RenderUtil:BeginFrame()` (engine-internal).
2. `Engine::HandleEvent` for each pending SFML event.
3. Zero or more `on_fixed_update` invocations (engine targets 25 Hz with a max of 5 catch-up ticks per frame, then yields).
4. `Gui::NewFrame(frame_dt)` (engine-internal).
5. `on_update(dt)` — your render-rate logic. **This is where you call `Pipeline.GetActive():Execute(scene_manager)`** to actually draw.
6. `window.display()` and `RenderUtil:EndFrame()` (engine-internal).

The loop exits when the SFML window receives `sf::Event::Closed`. After that, `on_shutdown` fires, then `Engine.run` returns.

**Profiler counters are previous-frame snapshots.** `RenderUtil:GetDrawCall()` and friends return values as of the most recently completed frame, not the in-progress one. This means `Gui.ShowDefault` can safely be drawn before or after `Pipeline.Execute` in `on_update` — counter visibility doesn't depend on ordering.

**Lifetime caveat.** Lua functions stored in the callback table keep their `sol::function` refs alive for the duration of `Engine.run`. Don't stash callback closures somewhere they outlive `Engine.run`'s return — they reference the Lua state, which is closed by the launcher right after the script finishes.

## Bound API reference

The per-binding reference is now auto-generated into
**[`docs/LUA_API.md`](LUA_API.md)** from `engine/Fury/LuaBindings.cpp` at
build time. That file is the canonical surface; this doc keeps the
narrative sections (callback contracts, gotchas, recipes). For the C++
source of truth, read `engine/Fury/LuaBindings.cpp` directly.

### `arg` — command-line arguments

The launcher populates a standard Lua `arg` table from `argv`:

```
arg[0]    = script path (the same string passed as argv[1] to `fury`, or "Editor.lua" by default)
arg[1..N] = argv[2..argc-1] (each entry a string)
#arg      = count of post-script arguments
```

This matches the convention of the standalone `lua` interpreter, so scripts authored elsewhere drop in. `Editor.lua` honors `arg[1]` as an optional startup-scene path:

```sh
./fury Editor.lua                 # loads Resource/Scene/scene.bin (the default)
./fury Editor.lua outdoor.fbx     # loads Resource/Scene/outdoor.fbx as the startup scene
./fury Editor.lua /path/to/x.glb  # absolute paths work too
./fury Editor.lua nope.fbx        # logs a warning, falls back to scene.bin
```

The resolution rule Editor.lua uses: try the literal first (so absolute and CWD-relative paths work), then prepend `Resource/Scene/`. If both miss, surface a status message and fall back to `scene.bin` so the editor is still interactive.

The launcher also recognizes two **runtime flags** — `--screenshot <path>` and `--screenshot-frame <N>` — anywhere in `argv` after the script path. They drive a debug PNG capture and are stripped from `arg` before the script sees it; scripts run identically with or without them. See `docs/CLI.md` (§ "Screenshot mode") for the full reference.

## Hello, world

A minimal `.lua` script that opens a window and prints a heartbeat every fixed tick:

```lua
local frame = 0

local function on_init()
    print("hello, fury3d")
end

local function on_update(dt)
    frame = frame + 1
end

local function on_fixed_update()
    if frame % 25 == 0 then
        print("frame " .. frame)
    end
end

local function on_shutdown()
    print("goodbye after " .. frame .. " frames")
end

Engine.run({
    on_init = on_init,
    on_update = on_update,
    on_fixed_update = on_fixed_update,
    on_shutdown = on_shutdown,
})
```

Run with: `./fury hello.lua` (from the working directory you want resources resolved against).

## Gotchas

- **`:` vs `.`** — usertype methods use `:` (which passes `self` as the first arg). Free functions in tables (`MathUtil.EulerRadToQuat`, `FileUtil.GetAbsPath`) use `.`. Mixing them fails silently or with a sol2 error about argument types.
- **Vector4 arithmetic returns by value.** `local sum = a + b` produces a new `Vector4`; the original `a` is unchanged. If you want in-place modification, use `:Normalize()` or assign through `.x`/`.y`/etc.
- **`Scene.Active` and `Pipeline.Active` are getter/setter functions, not properties.** Use `Scene.SetActive(...)` and `Scene.GetActive()` (same for `Pipeline`). See the §6 box above for why.
- **`__window`** is a private launcher-injected global pointing at the active `sf::Window`. The Lua bindings read it inside `Engine.run`. Don't shadow this name in your scripts or you'll break `Engine.run`.
- **Lua stdlib is fully open.** The launcher loads `base`, `string`, `math`, `table`, `io`, `os`, `package`. Scripts can read/write files, exec processes, etc. Acceptable for a dev tool today; revisit before shipping any script-running runtime to end users.
- **Callback errors are caught.** Unhandled errors inside `on_init` / `on_update` / `on_fixed_update` / `on_shutdown` are logged via `FURYE` and the loop continues. The engine doesn't abort on a Lua callback error.
- **Only the keys listed in `LuaBindings.cpp`'s `Key` table are pre-bound.** Add more entries to the table if you need keys outside the demo's set.
- **Component access is one-way.** `SceneNode:AddComponent(c)` works; the templated `GetComponent<T>()` is not bound. If you need to read components back, do it C++-side (the engine code can still introspect components freely).
- **The vector type is Vector4 even for 3D positions.** This is a long-standing engine convention, not a Lua-binding artifact. See `docs/ARCHITECTURE.md` §5.1 for the rationale.
- **macOS Retina is currently non-native.** SFML 3.1's macOS backend hardcodes `highDpi = NO` (`engine/ThirdParty/SFML/src/SFML/Window/macOS/SFOpenGLView.mm:128`), so the OpenGL surface is sized in screen points, not backing pixels. UI and scene look slightly soft on Retina displays — there's no `gui_scale` value that produces sharp pixels short of patching SFML. See `docs/ARCHITECTURE.md` §16. The engine compensates by defaulting `gui_scale` and `gui_font_scale` to `1.0` (the previous SFML-2-era 2× compensation would now double-scale).
- **Calling Gui functions from `on_init` is undefined.** The engine initializes ImGui inside `Engine::Run`, *after* `on_init` returns. Touching `Gui.ShowDefault` / `Gui.Render` from `on_init` will hit an uninitialized ImGui context. Stick to `on_update`.

## Future expansion

When the bridge needs to grow:

1. Add includes and the new `lua.new_usertype<T>(...)` blocks at the bottom of `engine/Fury/LuaBindings.cpp`.
2. If `T` derives from another bound type, list the chain via `sol::base_classes, sol::bases<...>()`.
3. If `T` has overloads with the same arity, use `sol::overload(static_cast<...>(&T::method), ...)` to disambiguate.
4. If a method takes `shared_ptr<Base>` where Base is registered, do **not** rely on sol2's automatic upcast — bind a typed wrapper as a free function (see `LoadSceneFromCompressedFile` / `LoadPipelineFromFile` for the pattern).
5. Add a new section to this doc with the new methods.
6. Run the demo to make sure existing bindings still work — sol2's heavy template instantiation can flag incompatibilities at compile time, but lifetime issues only show up at run time.
