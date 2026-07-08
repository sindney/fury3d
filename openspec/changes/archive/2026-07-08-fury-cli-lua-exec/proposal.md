## Why

The `fury` CLI can already convert and inspect scene files offline (`convert`, `info`), but there is no way for a human or an AI agent to *programmatically* iterate, mutate, and re-save a scene from outside the editor. Every programmatic scene operation today requires either opening the editor (`furye`) or writing a one-off Lua launcher script that boots the engine, opens a window, and runs `Engine.run` — a heavyweight path designed for runtime demos, not for batch scene surgery.

The scene format is JSON and the engine already has a Lua VM with sol2 bindings for the scene graph, transforms, cameras, lights, and mesh-render components. What's missing is (a) a CLI entry point that loads a scene, runs a Lua script against it, and exits — without booting a window — and (b) Lua coverage for the mesh / material / mesh-util / mesh-simplifier surface so scripts can actually do useful work (adjust materials, generate LODs, count component types, convert formats).

This unlocks a high-leverage workflow: an LLM agent can read `docs/LUA_API.md`, write a short Lua script, and run `fury exec scene.json agent.lua` to inspect or transform a scene. The agent never has to read C++ or open a GUI, and the JSON scene format is easy for it to learn and produce.

## What Changes

- **New CLI subcommand `exec`**: `fury exec <scene> <script.lua> [args...]` loads a scene (`.json`, `.bin`, `.gltf`, `.glb`, `.fbx` — same extension dispatch as `info`), creates a `sol::state`, registers engine Lua bindings, swaps `Scene::Active` to the loaded scene (preserving working-dir path resolution), runs the user's script, then exits. **No SFML window is opened. No `Engine::Initialize` is called.** Scripts get their arguments via the standard `arg` table. The script may call `FileUtil.SaveByExtension` (or a new `Scene.SaveActive(path)` helper) to persist a modified scene; read-only inspection is the default.
- **New Lua bindings** for the mesh / material / mesh-util surface so scripts can do real work:
  - `Mesh` usertype: name, AABB, vertex buffers (positions/normals/uvs/tangents/bone_ids/bone_weights), indices, submeshes, LOD chain accessors (`GetLodCount`, `GetLodMesh`, `SetLodMeshes`, `ClearLodChain`).
  - `Material` usertype: name, opaque flag, uniform get/set keyed by string, texture get/set keyed by string.
  - `MeshUtil` namespace table: `CreateCube`, `CreateSphere`, `CreateIcoSphere`, `CreateCylinder`, `CreateQuad`, `TransformMesh`, `OptimizeMesh`, `CalculateNormal`, `CalculateTangent`.
  - `MeshSimplifier` namespace table: `SimplifyMesh(mesh, opts)` + `MeshSimplifyOptions` (lod_count, reduction_ratio, target_error, lock_borders).
  - `EntityManager.ForEach<T>`-style iteration helpers exposed as `Scene.ForEachMesh(fn)`, `Scene.ForEachMaterial(fn)`, `Scene.ForEachNode(fn)` so scripts can scan all assets without recursing the node tree by hand.
- **Auto-generated Lua API reference**: a CMake-time step parses `engine/Fury/LuaBindings.cpp` and emits `docs/LUA_API.md` — a per-binding reference (type, constructor, every method/property, signature). The hand-written narrative sections of `docs/LUA.md` (Engine.run contract, gotchas, recipes) remain hand-written; only the per-binding API table is generated. Eliminates doc drift. The `exec` subcommand's `--help` output and `docs/CLI.md` point users/agents to `docs/LUA_API.md` and to `engine/Fury/LuaBindings.cpp` as the source of truth.
- **New `tests/lua/` test bed**: a folder of runnable Lua scripts, each invocable via `fury exec`, serving as smoke tests for the new Lua bindings and as worked examples for agents. The scripts deliberately do NOT replicate existing CLI features — `fury convert` and `fury info` already cover format conversion and scene summarization headlessly. Instead, each script exercises a specific binding surface that has no CLI equivalent:
  - `tests/lua/smoke_save.lua` — round-trip a scene through `Scene.GetActive()` + `FileUtil.SaveByExtension` from Lua. Verifies the Lua save-path binding works end-to-end. (Format conversion itself is the `convert` subcommand's job; this script only proves the Lua binding can save.)
  - `tests/lua/smoke_iterate.lua` — exercise `Scene.ForEachMesh` / `ForEachMaterial` / `ForEachNode` and log per-type counts. Verifies the iteration bindings work. (Scene summarization is the `info` subcommand's job; this script only proves the Lua iteration bindings work.)
  - `tests/lua/gen_lod.lua` — pick a named mesh (or all meshes), run `MeshSimplifier.SimplifyMesh`, attach the LOD chain via `Mesh.SetLodMeshes`, save the modified scene. Genuinely new — no existing CLI feature generates LODs.
  - `tests/lua/README.md` — one-paragraph run instructions for each script, with the explicit note that `smoke_save` and `smoke_iterate` are binding smoke tests, not replacements for `convert`/`info`.
- **`docs/CLI.md`** updated with a new `fury exec` section (syntax, accepted extensions, exit codes, no-engine-boot invariant, sample invocations).
- **`docs/LUA.md`** updated: the per-binding reference is removed (now auto-generated into `docs/LUA_API.md`); narrative sections are kept and a pointer to `docs/LUA_API.md` is added at the top.

### Non-overlap with existing features

The `fury` launcher path (`./fury script.lua` or `./furye script.lua`) already runs Lua scripts that can load and save scenes — but it always opens an `sf::Window`, always calls `Engine::Initialize`, and always runs the `Engine.run` frame loop (see `examples/main.cpp:124-205` and `test_shutdown.lua:51`). `exec` is the first path that runs Lua **headlessly**: no window, no engine boot, no frame loop, synchronous batch execution. This is what makes it usable from CI and from agents that have no display.

The test scripts in `tests/lua/` are explicitly NOT replacements for `fury convert` / `fury info` (both already headless, both already cover format conversion and scene summarization). They are binding smoke tests: `smoke_save.lua` proves `FileUtil.SaveByExtension` works from Lua; `smoke_iterate.lua` proves `Scene.ForEach*` works from Lua; `gen_lod.lua` covers LOD generation, which has no existing CLI equivalent.

## Capabilities

### New Capabilities

- `lua-scene-scripting`: Lua bindings covering `Mesh`, `Material`, `MeshUtil`, `MeshSimplifier`, and `Scene.ForEach*` iteration helpers — the surface a script needs to inspect and mutate scene contents beyond the already-bound node/transform/camera/light/MeshRender component layer.
- `lua-api-docgen`: A CMake-time generator that parses `engine/Fury/LuaBindings.cpp` and emits `docs/LUA_API.md` as the canonical per-binding API reference, eliminating manual doc drift.
- `lua-test-scripts`: A `tests/lua/` folder of runnable Lua scripts (binding smoke tests + LOD generation) invocable via `fury exec`. The smoke tests exercise specific Lua bindings end-to-end; they deliberately do not replicate the `convert` or `info` CLI subcommands (which already cover format conversion and scene summarization headlessly). LOD generation has no existing CLI equivalent.

### Modified Capabilities

- `cli`: Adds the `exec <scene> <script.lua> [args...]` subcommand to the recognized subcommand token list, the dispatch table in `Cli::Run`, and `DoHelp`. Establishes the no-window / no-`Engine::Initialize` / `Scene::Active` swap invariants for the exec path.

## Impact

- **Code**:
  - `engine/Fury/Cli.h` / `Cli.cpp` — new `kExecHelp` constant, `DoExec(argc, argv)` handler, token-list entry, dispatch branch, `DoHelp` entry. Mirrors the documented "Future expansion" recipe at `docs/CLI.md:381-395`.
  - `engine/Fury/LuaBindings.cpp` — append `Mesh` / `Material` / `MeshUtil` / `MeshSimplifier` usertypes and `Scene.ForEach*` helpers per the recipe at `docs/LUA.md:612-621`. Single-file registration continues (no module split introduced by this change).
  - `engine/Fury/Engine.cpp` — confirm the `Engine::Shutdown` static-resource reset (memory note `gl_context_shutdown_segfault.md`) is not needed for `exec` because `exec` does not boot the engine nor open a GL context. If the mesh-simplifier path or `MeshUtil` primitives are touched and lazily create statics, the exec handler must explicitly reset them before exit (mirroring `Engine::Shutdown`'s `MeshUtil::Reset()` call).
  - New `engine/Fury/Tools/LuaApiDocGen.{h,cpp}` (or a Python script under `engine/Tools/`) — the doc generator. CMake custom target `lua_api_doc` runs it at build time; output is `docs/LUA_API.md`.
  - `engine/CMakeLists.txt` — wire the docgen custom target into the default build (runs once on build, regenerates when `LuaBindings.cpp` changes).
  - `examples/main.cpp` — no change (the `Cli::LooksLikeSubcommand` token list lives in `Cli.cpp`).
- **Docs**:
  - `docs/CLI.md` — new `fury exec` section.
  - `docs/LUA.md` — strip per-binding reference, add pointer to `docs/LUA_API.md`.
  - `docs/LUA_API.md` — auto-generated, checked into the repo (so agents reading the repo without a build still see it; rebuilt fresh on every build).
  - `tests/lua/README.md` — new.
- **Dependencies**: No new third-party dependencies. The docgen tool uses the existing C++ parser front-end (or a minimal hand-rolled scanner over `LuaBindings.cpp`, which has a regular structure). `meshoptimizer` is already vendored and built.
- **APIs**: New Lua surface is purely additive (new usertypes, new namespace-table functions). No existing Lua binding changes shape. No C++ public API changes.
- **Risk / invariants**:
  - The `exec` path is the first CLI subcommand that creates a `sol::state`. Must establish the convention for engine-resource lifetime on the CLI path (no GL context; lazy `MeshUtil` primitives must be reset before exit if touched).
  - The auto-generated `docs/LUA_API.md` is checked in AND regenerated on build — must be deterministic so CI doesn't churn it.
  - Tests live in `tests/lua/` and run via `fury exec`; v1 has no CTest wiring (no C++ test framework exists in the repo). Running them is manual: `./fury exec examples/Resource/Scene/scene.json tests/lua/scene_info.lua`. A CTest wrapper is a follow-up, not part of this change.
