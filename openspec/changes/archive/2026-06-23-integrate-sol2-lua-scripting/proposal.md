## Why

Today every demo of Fury3D requires a C++ recompile of `examples/Demo.cpp`. Iteration time is dominated by the engine's link step, not by the change itself. ARCHITECTURE.md §15 commits to vendoring sol2 *after* the engine boundary stabilises so demos can be authored as scripts. The boundary is now stable enough (FBX is gone, SFML 3 migration is done, build system is self-contained) that this follow-up is unblocked.

This change rewires Fury3D's top-level usage model: instead of "engine compiled into a `libfury` linked into a custom `demo` executable," the engine itself becomes the executable. It owns the SFML window, the fixed-timestep main loop, and the Lua VM. Demos are `.lua` scripts loaded at runtime that register `Init` / `Update` / `FixedUpdate` / `Shutdown` callbacks — the C++ never has to recompile to ship a new demo. The pattern matches LÖVE2D, Defold, and similar script-driven engines.

## What Changes

- **BREAKING**: Build target restructure. The current `examples/demo` C++ executable goes away. The engine builds a single `fury` executable (was: a `libfury` shared library plus a separate `demo` exe). The new exe **is** the engine: it accepts a `.lua` script path on the command line (or defaults to a known location), boots the engine, runs the loop, and exits.
- **BREAKING**: `Engine::Run(window, callbacks)` becomes the canonical entry point. Lifts the fixed-timestep loop, SFML event pump, frame begin/end, and `Gui::NewFrame` timing out of `Demo.cpp` and into `Engine.cpp`. Lua scripts (and any future C++ apps) provide callbacks; they do not write the loop themselves.
- **BREAKING**: `examples/Demo.cpp` deleted. Replaced by `examples/Demo.lua` (functionally equivalent: same scene, same camera, same pipeline). The `examples/` folder gains a `Demo.lua` and a slim C++ launcher that's shared by all future demos.
- **BREAKING**: `examples/CMakeLists.txt` no longer produces a `demo` target. The launcher source moves into the engine target.
- Vendor **Lua 5.4.7** as a git submodule under `engine/ThirdParty/lua`. Lua's upstream ships a Makefile only — wrap it in a small CMake target that compiles the ~17 `.c` files in `lua/src/` into a static library. Lua 5.4 is the latest stable line; sol2 v3.x supports Lua 5.1–5.4 + LuaJIT. We pick 5.4 (no LuaJIT) for portability and simplicity on Apple Silicon.
- Vendor **sol2 v3.5.0** as a git submodule under `engine/ThirdParty/sol2`. Header-only; we add `engine/ThirdParty/sol2/include` to the engine's include path. v4.0.0-alpha is the only newer tag and is explicitly skipped.
- Add a **Lua bindings module** under `engine/Fury/LuaBindings.{h,cpp}`. Exposes (Demo-driven minimum surface): `Vector4`, `Quaternion`, `MathUtil`, `OcTree`, `Scene`, `SceneNode`, `Camera`, `Transform`, `Component` (just the type), `Pipeline`, `PrelightPipeline`, `FileUtil`, `LogLevel`, `RenderUtil`, `Gui`, plus the `Engine.run` callback registration hook. ~12 types, ~50 methods. Wider bindings (Light, MeshRender, Material, AnimationPlayer, InputUtil signals, etc.) are deferred — a follow-up change.
- Add `docs/LUA.md` covering: the `Engine.run` callback contract, the bound API surface (one section per exposed engine type), a hello-world snippet, and known gotchas (sol2's handling of `shared_ptr`/`weak_ptr`, SFML 3 enum spellings, Lua's nil-vs-empty conventions).
- The engine is **no longer built as a shared library by default**. Linkage of Lua + sol2 + the engine into a single statically-linked executable simplifies the rpath story (no more `BUILD_WITH_INSTALL_RPATH` dance) and removes the `BUILD_SHARED_LIBS` toggle's relevance for the common case. The CMake option stays (off by default) for users who still want a `libfury` for embedding.
- `Engine::Initialize` keeps its existing signature; `Engine::Run` is layered on top, not a replacement. Anyone who was driving the engine from their own loop (no public users today, but the door stays open) can still do so.

Non-goals (deferred):
- Hot-reload of Lua scripts on file change — requires a watcher and care around component lifetimes; out of scope here.
- Bindings for `Light`, `MeshRender`, `Material`, `AnimationClip`, `AnimationPlayer`, `InputUtil` signals, `BoxBounds`, `Frustum`, `Color`, `Texture`. Each is a small follow-up change once the bridge pattern is established.
- Lua-side error messages with stack traces (sol2 supports this, but we use the default panic handler this round).
- Wrapping Lua's stdlib for sandboxing — Lua scripts have full `io`/`os`/`package` access. Acceptable because the engine is dev-tool right now, not a hardened runtime.
- ImGui Lua bindings (lua-imgui exists upstream; not vendored here).
- Test harness for the bindings (no pytest yet — the §14 plan is still upstream of this change).

## Capabilities

### New Capabilities
- `lua-scripting`: The integration of Lua 5.4 + sol2 v3.5 into Fury3D, the new `Engine::Run` callback-driven main loop, the C++ → Lua binding surface for Demo-driven engine types, the `fury` executable that loads and runs `.lua` scripts, and the `docs/LUA.md` reference. This capability covers the *bridge*, not future demos written on top of it.

### Modified Capabilities
<!-- None. The existing `gltf-loader` capability is unrelated to this change. There are no other main specs in openspec/specs/. -->

## Impact

- **Code added**: `engine/Fury/LuaBindings.{h,cpp}` (~600-1000 lines depending on how chatty sol2's `usertype` declarations end up); `engine/Fury/Engine.cpp` gains an `Engine::Run` definition; `engine/Fury/Engine.h` gains the matching declaration plus a small `Engine::Callbacks` POD or `sol::table`-typed handle; `examples/main.cpp` (new — small Lua launcher); `examples/Demo.lua` (new); `docs/LUA.md` (new).
- **Code removed**: `examples/Demo.cpp` (replaced by `Demo.lua` + the new launcher).
- **Code modified**: `engine/CMakeLists.txt` (Lua + sol2 submodule wiring, Lua's CMake-wrapper, link bindings into the engine target, the engine becomes an executable target by default); `examples/CMakeLists.txt` (slimmed down — the demo launcher source moves into the engine target).
- **Repo layout**: New submodules at `engine/ThirdParty/lua` (v5.4.7), `engine/ThirdParty/sol2` (v3.5.0). New file `docs/LUA.md`. New file `examples/Demo.lua`.
- **External dependencies**: Lua and sol2 are header-only-ish; no new system libs. macOS will need `-ldl -lreadline` exactly as Lua's upstream Makefile does — handled inside our CMake wrapper.
- **Demo runtime**: Functionally unchanged. The new `fury` exe with `Demo.lua` produces the same scene, same camera, same deferred lighting pipeline, same 25 Hz fixed-tick rate.
- **Downstream impact**: Anyone who was authoring demos as `examples/Demo.cpp` ports to a `.lua` file (one-pass mechanical port; the bound API mirrors the C++ API closely). Anyone embedding `libfury` keeps working — `BUILD_SHARED_LIBS=ON` still produces the shared library.
- **Architecture doc**: ARCHITECTURE.md §15 already lists "sol2 deferred until the engine boundary stabilises" as committed — no doc churn beyond a status update at the end of this change.
