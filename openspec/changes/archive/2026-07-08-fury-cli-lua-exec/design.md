## Context

The `fury` binary has two execution paths today:

1. **Lua launcher** (`examples/main.cpp` default): opens an `sf::Window`, calls `Engine::Initialize`, creates a `sol::state`, registers `LuaBindings`, and runs `Engine.run` with the user's Lua script. Designed for interactive demos and the editor.
2. **CLI subcommands** (`engine/Fury/Cli.cpp`): `convert`, `info`, `help`, `version`. All offline, no window, no engine boot, no Lua VM.

The CLI path has never created a `sol::state` — `exec` will be the first. The CLI path has also never touched `Scene::Active` except via `convert scene`/`info`, both of which already do the swap-and-reset dance that `Importer.LoadScene` (`engine/Fury/LuaBindings.cpp:586-591`) established.

Lua bindings today live in a single file `engine/Fury/LuaBindings.cpp` (~1,222 lines, one `LuaBindings::Register(sol::state_view)` function). Already bound: `Vector4`, `Quaternion`, `MathUtil`, `Scene`/`SceneNode`/`Entity`, `Transform`/`Camera`/`Light`/`MeshRender` (component layer), `Pipeline`/`PrelightPipeline`, `FileUtil`, `Importer`, `Editor` (under `WITH_EDITOR`), `InputUtil`/`Key`/`MouseButton`, `Engine.run`. **Not bound**: `Mesh` (data buffers + LOD chain), `SubMesh`, `Material` (mutation surface), `MeshUtil`, `MeshSimplifier`. The C++ APIs for all of these already exist (`Mesh.h:227-236` for LOD chain, `MeshUtil.h:34-76`, `MeshSimplifier.h:17-68`) — only the Lua bridge is missing.

Memory note `gl_context_shutdown_segfault.md` applies to the launcher path: `Scene::Active`, `Pipeline::Active`, and `MeshUtil` statics hold resources that must be reset in `Engine::Shutdown` before the GL context dies. The `exec` path will not open a GL context, but the design must reason explicitly about why the static-resource teardown is *not* needed there.

No C++ test framework exists in the repo. Existing Lua test scripts (`examples/test_*.lua`) are run manually via the launcher path.

## Goals / Non-Goals

**Goals:**

- A `fury exec <scene> <script.lua> [args...]` subcommand that loads a scene, runs a Lua script against it, and exits — without opening a window, booting the engine, or creating a GL context.
- Sufficient Lua bindings for an agent to (a) inspect a scene (counts, types, AABBs), (b) mutate common components (transform/camera/light/mesh-render/material/shadow settings), and (c) perform mesh operations including LOD generation.
- A canonical, never-stale Lua API reference at `docs/LUA_API.md`, generated from `engine/Fury/LuaBindings.cpp` at build time. Source is the single source of truth.
- A `tests/lua/` folder of runnable Lua scripts that double as smoke tests for the new subcommand/bindings and as worked examples for agents.

**Non-Goals:**

- No CTest wiring or C++ unit-test framework in this change. Test scripts are run manually via `fury exec` in v1.
- No refactoring of `LuaBindings.cpp` into per-module files. Single-file registration continues; new bindings are appended per the recipe at `docs/LUA.md:612-621`.
- No new third-party dependencies. `meshoptimizer` is already vendored; docgen is a hand-rolled scanner (no libclang).
- No rendering from `exec`. Scripts that want to render or screenshot use the existing Lua launcher path (with `--screenshot`). `RenderUtil`, `Gui`, `Window`, `Editor.*` are out of scope from `exec` and will return clean errors if called.
- No C++ public API changes. The Lua surface is purely additive.
- No `Editor.*` surface changes (stays `WITH_EDITOR`-gated; `exec` runs on the `fury` headless binary).

## Decisions

### D1. Headless `exec` — no window, no `Engine::Initialize`, no GL context

`exec` matches the rest of the CLI: it creates a `sol::state` and a `Scene` but never an `sf::Window`, never calls `Engine::Initialize`, and never creates an OpenGL context. The use cases in scope (iterate, mutate, save, inspect, generate LODs) are CPU-only.

**Alternative considered:** open a hidden GL context so scripts could call `RenderUtil`. Rejected — would pull in SFML window init, defeats the "no engine boot" CLI invariant, and no scoped use case needs it. Users who want rendering use the launcher path with `--screenshot`.

**Consequence:** `RenderUtil`/`Gui`/`Window`/`Editor.*` are inaccessible from `exec`. Documented as a limitation in `docs/CLI.md` and `docs/LUA.md`.

### D2. Resource lifetime on the headless path — no `Engine::Shutdown` needed

Memory note `gl_context_shutdown_segfault.md` flags that `Scene::Active`, `Pipeline::Active`, and `MeshUtil` statics must be reset before the GL context dies. The launcher path solves this by calling `Engine::Shutdown` (which resets those statics) before the SFML window tears down.

`exec` does not open a GL context, so the ordering hazard does not arise. **But** the design must verify that no path an `exec` script can take lazily allocates GL-backed resources:

- `MeshUtil` primitives (`GetUnitCube`/`GetUnitSphere`/...) are CPU-side `Mesh` objects — vertex/index buffers in `std::vector`, no GL buffers allocated at creation. GL upload happens at render time (which `exec` never triggers). Safe.
- `MeshSimplifier::SimplifyMesh` operates on CPU mesh data via `meshoptimizer`. Safe.
- `Mesh::OptimizeMesh`, `CalculateNormal`, `CalculateTangent` — CPU-only. Safe.
- `Material` mutation — CPU struct fields only. Safe.

**Mitigation:** the `exec` handler resets `Scene::Active` to `nullptr` on exit (matches `convert scene`/`info`). It does **not** call `MeshUtil::Reset()` because no GL-backed primitives are touched. If a future binding introduces a CPU-accessible API that lazily allocates GL resources, this assumption must be revisited. A comment in `DoExec` (`engine/Fury/Cli.cpp`) will document this invariant.

### D3. Lua VM lifetime — per-exec, synchronous, no `Engine.run` callback contract

`DoExec` creates its own `sol::state`, opens the same stdlib set as `examples/main.cpp:144-151` (`base`, `string`, `math`, `table`, `io`, `os`, `package`), calls `LuaBindings::Register(lua)`, then runs the user's script via `sol::protected_function` with an error handler. Scripts run to completion synchronously; there is no `Engine.run` callback table, no frame loop, no event polling.

**Rationale:** `exec` scripts are one-shot batch operations (count, mutate, save, exit). The `Engine.run` contract is for interactive demos and is unnecessary here.

### D4. Argument forwarding — `arg` table, no `--screenshot` parsing

`arg[0]` is the script path. `arg[1..N]` are trailing args after the script path. Matches the launcher path's convention (see `docs/LUA.md`). `exec` does **not** parse `--screenshot` / `--screenshot-frame` — those are runtime flags for the launcher path only. Unknown trailing flags are passed through to the script as `arg` entries (the script decides whether to error).

### D5. `Scene::Active` swap on entry, reset to `nullptr` on exit

Matches `Importer.LoadScene` and `convert scene`. Required so working-dir-relative path resolution (texture lookups, sibling-file references) works inside the script. Reset to `nullptr` on exit matches `convert scene`/`info`.

### D6. Mesh buffer access — read/write via flat Lua tables

`mesh:GetPositions()` returns a flat Lua table `{x0,y0,z0,x1,y1,z1,...}`. `mesh:SetPositions(tbl)` accepts the same shape. Same for normals/uvs/tangents/bone_ids/bone_weights/indices.

**Alternative considered:** expose `sol::as_container` view over the underlying `std::vector<float>`. Rejected — complicates the agent-facing API with ownership/lifetime questions and `sol::as_container` semantics that an LLM is unlikely to write correctly on the first try. Plain tables round-trip cleanly and are easy to reason about.

**Trade-off:** round-tripping a 100k-vertex mesh through a Lua table is slow (allocations). Acceptable for inspection/mutation use cases. Documented as a known perf characteristic. Direct-buffer bindings are a follow-up, not in scope for v1.

### D7. LOD chain accessors mirror the C++ API one-to-one

`Mesh:GetLodCount()`, `Mesh:GetLodMesh(i)`, `Mesh:SetLodMeshes(meshes_tbl, thresholds_tbl)`, `Mesh:ClearLodChain()`. Direct mirror of `Mesh.h:227-236`. No transformation, no sugar.

### D8. `MeshSimplifier.SimplifyMesh(mesh, opts)` — namespace table, plain-table opts

`MeshSimplifier.SimplifyMesh(mesh, { lod_count = 3, reduction_ratio = 0.5, target_error = 0.5, lock_borders = true })`. All opts keys optional; defaults match `MeshSimplifyOptions` defaults. Returns `{ lod_meshes = {...}, thresholds = {...} }`.

A free function under a namespace table (not a method on `Mesh`) because that's how it's structured in C++ (`SimplifyMesh` is a free function in `MeshSimplifier.h`, not a `Mesh` member).

### D9. `Scene.ForEach*` iteration helpers — wrap `EntityManager::ForEach<T>` + recursive node walk

- `Scene.ForEachMesh(fn)` — wraps `EntityManager::ForEach<Mesh>`.
- `Scene.ForEachMaterial(fn)` — wraps `EntityManager::ForEach<Material>`.
- `Scene.ForEachNode(fn)` — recursive walk of the SceneNode tree from `GetRootNode()`.

**Rationale:** agents shouldn't have to hand-recurse the scene tree for every "count meshes" task. The typed-asset iteration already exists in `EntityManager`; only the Lua bridge is missing.

### D10. Docgen — hand-rolled scanner, no libclang

`engine/Fury/LuaBindings.cpp` has a regular structure: `lua.new_usertype<T>("Name", "member", &T::method, ...)` and `lua.create_named_table("Namespace")` calls follow a predictable pattern. A ~200-line Python script (under `engine/Tools/lua_api_docgen.py`) scans for these patterns, extracts type names + member names + (best-effort) signatures, and emits `docs/LUA_API.md`.

**Alternative considered:** libclang-based AST walker. Rejected — heavy dep, build complexity, and LuaBindings.cpp's regular structure doesn't need it. The scanner's output is deterministic (sorted by registration order, stable formatting) so check-ins don't churn.

**Fallback for unparseable bindings:** the scanner emits a `<!-- docgen: unable to introspect, see LuaBindings.cpp:LINE -->` marker so the gap is visible and reviewable. Complex lambda-bound members fall back to this marker; the gap is then filled by the existing hand-written `docs/LUA.md` narrative where needed.

### D11. `docs/LUA_API.md` is checked in AND regenerated on build

- **Checked in:** agents reading the repo without building can see the API. CI doesn't need to build to verify freshness.
- **Regenerated on every build:** prevents drift. CMake custom target `lua_api_docgen` runs the scanner; output is `docs/LUA_API.md`. The custom target is wired into the default build via `add_custom_command(TARGET fury POST_BUILD ...)`.
- **Stale check (follow-up, not v1):** a separate CI step asserts `docs/LUA_API.md` matches the scanner output. v1 silently regenerates; the next commit checks in any diff.

### D12. `docs/LUA.md` keeps narrative, loses per-binding reference

The hand-written `docs/LUA.md` (622 lines) keeps its narrative sections: `Engine.run` contract, gotchas, `arg` table convention, screenshot flags, "Future expansion" recipe. The per-binding reference sections are removed (now in `docs/LUA_API.md`). A pointer to `docs/LUA_API.md` is added at the top.

### D13. Test scripts run via `fury exec`, not CTest; deliberately not CLI-feature replicas

v1: manual invocation `./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_iterate.lua`. No CTest wiring. CTest + assertions are a follow-up change.

The test scripts are binding smoke tests, not CLI-feature replicas. The repo already has `fury convert` (headless format conversion) and `fury info` (headless scene summarization) — replicating those from Lua would be redundant. Instead:

- `smoke_save.lua` only proves `FileUtil.SaveByExtension` works from Lua (round-trip a scene through `Scene.GetActive()` + save).
- `smoke_iterate.lua` only proves `Scene.ForEachMesh` / `ForEachMaterial` / `ForEachNode` work from Lua.
- `gen_lod.lua` covers LOD generation — genuinely new, no existing CLI equivalent.

**Rationale:** bootstrapping the feature with itself is elegant and proves the bindings work end-to-end. Worked-example value for agents is higher than automated regression value in v1. CTest wiring would require a C++ test runner wrapper, which is a separate concern.

### D14. Exit codes follow the existing CLI convention

0 = script ran without raising. 1 = user error (missing file, bad extension, script error caught via `sol::protected_function`'s error handler). 2 = internal error (C++ exception escaping `DoExec`). Matches the requirement in `openspec/specs/cli/spec.md:107-123`.

### D15. Accepted scene input extensions match `info`

`exec` accepts `.json`, `.bin`, `.gltf`, `.glb`, `.fbx` — same dispatch as `info` (`Cli.cpp:611-613`). For `.fbx`, the FBX2glTF subprocess + `GltfImporter::Import` chain runs first (same as `convert fbx`), producing an in-memory `Scene` that becomes the script's input. The script never sees the temp `.glb`.

## Risks / Trade-offs

- **[Docgen scanner is fragile]** → Mitigation: marker comments for unparseable bindings (visible gap, not silent failure); scanner does not block the build on parse errors; future CI step asserts freshness. The scanner's grammar is small and `LuaBindings.cpp` is hand-maintained by a small team, so structural drift is slow.
- **[Headless path can't render]** → Mitigation: documented as a non-goal; users wanting render use the launcher path with `--screenshot`. Clean error if `RenderUtil`/`Gui`/`Window` are called from `exec` (the bindings return `nil`/empty since the underlying singletons aren't initialized).
- **[Lua scripts can crash the process]** → Mitigation: `sol::protected_function` with error handler catches Lua errors → exit 1. C++ `try/catch` in `DoExec` catches C++ exceptions → exit 2. Documented in the cli spec.
- **[Mesh buffer round-trip via Lua tables is slow for huge meshes]** → Mitigation: acceptable for inspection/mutation; documented in `docs/LUA_API.md` under `Mesh`. Direct-buffer bindings are a follow-up.
- **[`tests/lua/` scripts have no automated regression catching]** → Mitigation: v1 trade-off; CTest wiring is a documented follow-up. Manual runs are sufficient for smoke-testing the new feature.
- **[Resource-lifetime assumption (D2) could regress]** → Mitigation: the invariant ("`exec` scripts cannot reach any code path that lazily allocates GL-backed resources") is documented in `DoExec` and `docs/CLI.md`. New bindings that violate this assumption must update `DoExec`'s teardown.
- **[Docgen output churn]** → Mitigation: deterministic output (sorted, stable formatting). The scanner sorts bindings by registration order in `LuaBindings.cpp` (which is stable) and emits one section per type alphabetically within namespace. Re-running on the same input produces byte-identical output.
- **[Overlap with `convert`/`info` CLI subcommands]** → Mitigation: `exec`'s test scripts are explicitly framed as binding smoke tests, not CLI-feature replicas (D13). `smoke_save.lua` and `smoke_iterate.lua` exist only to prove the Lua bindings work end-to-end; they do not claim to replace `convert` (which already does headless format conversion) or `info` (which already does headless scene summarization). `gen_lod.lua` is the only test script with no CLI equivalent. Documented in `tests/lua/README.md`.
- **[Overlap with the Lua launcher path]** → Mitigation: the launcher path always opens an `sf::Window`, calls `Engine::Initialize`, and runs the `Engine.run` frame loop (`examples/main.cpp:124-205`). `exec` is the first headless Lua-execution path; documented as a non-goal to support rendering from `exec` (design decision D1). Scripts that need rendering use the launcher path with `--screenshot`.

## Open Questions

- **`--dry-run` flag?** Defer. Scripts can simply not call `SaveByExtension`. Not needed for v1.
- **Example snippets in `docs/LUA_API.md`?** Defer. `tests/lua/` scripts serve as worked examples; auto-gen stays minimal.
- **CTest wiring for `tests/lua/`?** Follow-up change. Would need a small C++ runner that invokes `fury exec` per script and checks exit codes.
- **Should `exec` accept stdin as a script source** (e.g., `fury exec scene.json -`)? Defer. Out of scope for v1.
