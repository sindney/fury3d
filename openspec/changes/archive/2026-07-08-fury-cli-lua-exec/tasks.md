## 1. CLI `exec` subcommand wiring

- [x] 1.1 Add `kExecHelp` static string to `engine/Fury/Cli.cpp` (mirrors `kConvertHelp` / `kInfoHelp` shape; covers syntax, accepted extensions, exit codes, no-window/no-engine-boot invariants, pointer to `docs/LUA_API.md` and `engine/Fury/LuaBindings.cpp`)
- [x] 1.2 Add `"exec"` to the `tokens[]` array in `Cli::LooksLikeSubcommand` (`engine/Fury/Cli.cpp:623-625`)
- [x] 1.3 Add `else if (std::strcmp(argv[1], "exec") == 0) return DoExec(argc, argv);` branch in `Cli::Run` (`engine/Fury/Cli.cpp:655-658`)
- [x] 1.4 Add an `exec` branch to `DoHelp` (`engine/Fury/Cli.cpp:246-259`) that prints `kExecHelp`
- [x] 1.5 Declare `static int DoExec(int argc, char **argv);` in `engine/Fury/Cli.h` alongside `DoConvert` / `DoInfo`

## 2. `DoExec` implementation

- [x] 2.1 Parse `argv`: `<scene>` at `argv[2]`, `<script.lua>` at `argv[3]`, trailing args at `argv[4..]`. Handle `--help` / `-h` at `argv[2]` → print `kExecHelp`, exit 0. Missing scene or script → stderr error, exit 1.
- [x] 2.2 Validate scene extension via `ToLowerExt` (`Cli.cpp:215-223`); accept `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`; reject anything else with stderr listing supported extensions, exit 1.
- [x] 2.3 Load the scene: `.json` → `FileUtil::LoadFile`, `.bin` → `FileUtil::LoadCompressedFile`, `.gltf`/`.glb` → `GltfImporter::Import`, `.fbx` → `FbxConverter::Convert` to temp `.glb` then `GltfImporter::Import` (mirror `DoConvert`'s fbx path). Set `scene->SetWorkingDir(dirname(scene) + "/")`.
- [x] 2.4 Wrap the rest of `DoExec` in an RAII guard that sets `Scene::Active = scene` on construction and resets to `nullptr` on destruction (success + all error paths).
- [x] 2.5 Create `sol::state lua`, open stdlib set (`base`, `string`, `math`, `table`, `io`, `os`, `package` — matches `examples/main.cpp:144-151`), call `LuaBindings::Register(lua)`.
- [x] 2.6 Build the `arg` table: `arg[0] = argv[3]` (script path), `arg[1..N] = argv[4..argc-1]`. Set on `lua["arg"]`.
- [x] 2.7 Load the script file via `sol::protected_function` with a `sol::reference` error handler that captures the Lua error message + line; on error, print to stderr with script name and exit 1.
- [x] 2.8 Wrap the whole handler in `try { ... } catch (const std::exception& e) { stderr << "exec: " << e.what(); return 2; }`.
- [x] 2.9 Add a comment in `DoExec` documenting the headless resource-lifetime invariant (design decision D2: no GL context, no `MeshUtil::Reset()` call needed because no GL-backed primitives are touched).

## 3. Lua bindings: `Mesh`

- [x] 3.1 Append `lua.new_usertype<Mesh>("Mesh", ...)` to `LuaBindings::Register` in `engine/Fury/LuaBindings.cpp` (per the recipe at `docs/LUA.md:612-621`)
- [x] 3.2 Bind `:GetName()`, `:SetName(name)`, `:GetAABB()` (returns `BoxBounds`)
- [x] 3.3 Bind buffer accessors with flat-table round-trip: `:GetPositions() / :SetPositions(tbl)`, `:GetNormals() / :SetNormals(tbl)`, `:GetUVs() / :SetUVs(tbl)`, `:GetTangents() / :SetTangents(tbl)`, `:GetBoneIds() / :SetBoneIds(tbl)`, `:GetBoneWeights() / :SetBoneWeights(tbl)`, `:GetIndices() / :SetIndices(tbl)`
- [x] 3.4 Bind submesh accessors: `:GetSubmeshCount()`, `:GetSubmeshIndices(i)`
- [x] 3.5 Bind LOD chain accessors mirroring `Mesh.h:227-236`: `:GetLodCount()`, `:GetLodMesh(i)`, `:SetLodMeshes(meshes_tbl, thresholds_tbl)`, `:ClearLodChain()`

## 4. Lua bindings: `Material`

- [x] 4.1 Append `lua.new_usertype<Material>("Material", ...)` to `LuaBindings::Register`
- [x] 4.2 Bind `:GetName()`, `:SetName(name)`, `:IsOpaque()`, `:SetOpaque(bool)`, `:GetTextureCount()`
- [x] 4.3 Bind `:GetUniform(key)` returning number-or-table-or-nil; bind `:SetUniform(key, value)` with type inference from Lua value shape (number → `Uniform1f`/`Uniform1ui`, 3-table → `Uniform3f`, 4-table → `Uniform4f`)
- [x] 4.4 Bind `:GetTexture(key)` returning path-or-nil; bind `:SetTexture(key, path)`

## 5. Lua bindings: `MeshUtil` + `MeshSimplifier`

- [x] 5.1 Register `MeshUtil` as a namespace table (`lua.create_named_table("MeshUtil")`) with: `CreateCube()`, `CreateQuad()`, `CreateSphere(segments)`, `CreateIcoSphere(subdivisions)`, `CreateCylinder(segments)` — mirror `MeshUtil.h:46-55`
- [x] 5.2 Add `MeshUtil.TransformMesh(mesh, matrix_flat_table_16)`, `MeshUtil.OptimizeMesh(mesh)`, `MeshUtil.CalculateNormal(mesh)`, `MeshUtil.CalculateTangent(mesh)` — mirror `MeshUtil.h:57-67`
- [x] 5.3 Register `MeshSimplifier` as a namespace table with `SimplifyMesh(mesh, opts)` returning `{ lod_meshes = {...}, thresholds = {...} }`. `opts` is a plain Lua table with optional `lod_count` / `reduction_ratio` / `target_error` / `lock_borders` (defaults from `MeshSimplifyOptions`). If `opts` is nil/omitted, use defaults. Wrap `MeshSimplifier::SimplifyMesh` (`MeshSimplifier.h:68`).

## 6. Lua bindings: `Scene.ForEach*` helpers

- [x] 6.1 Extend the existing `Scene` usertype binding (`LuaBindings.cpp:200-219`) with `:ForEachMesh(fn)` wrapping `EntityManager::ForEach<Mesh>` (short-circuit on non-nil return)
- [x] 6.2 Add `:ForEachMaterial(fn)` wrapping `EntityManager::ForEach<Material>`
- [x] 6.3 Add `:ForEachNode(fn)` doing a recursive pre-order walk from `GetRootNode()` (short-circuit on non-nil return)

## 7. Lua API docgen tool

- [x] 7.1 Implement `engine/Tools/lua_api_docgen.py` (Python) — scan `engine/Fury/LuaBindings.cpp` for `lua.new_usertype<T>("Name", ...)` and `lua.create_named_table("Name")` calls; extract type/namespace names + member names + best-effort signatures from member pointers
- [x] 7.2 Emit Markdown to `docs/LUA_API.md` with one section per binding, members in source order
- [x] 7.3 For unparseable bindings (complex lambdas, computed properties), emit `<!-- docgen: unable to introspect, see LuaBindings.cpp:LINE -->` marker; do not abort
- [x] 7.4 Ensure deterministic output (bindings in registration order; deterministic formatting) so re-running produces byte-identical output
- [x] 7.5 Run the script once locally; verify `docs/LUA_API.md` includes sections for the new `Mesh` / `Material` / `MeshUtil` / `MeshSimplifier` / `Scene.ForEach*` bindings

## 8. Docgen CMake integration

- [x] 8.1 Register a `lua_api_docgen` custom target in `engine/CMakeLists.txt` that runs `python3 engine/Tools/lua_api_docgen.py > docs/LUA_API.md`
- [x] 8.2 Wire the custom target as a `POST_BUILD` step of the `fury` target via `add_custom_command(TARGET fury POST_BUILD COMMAND ...)`
- [x] 8.3 Verify a normal `cmake --build build` regenerates `docs/LUA_API.md` after edits to `LuaBindings.cpp`
- [x] 8.4 Check in the initial `docs/LUA_API.md` so a fresh clone sees it without building

## 9. Update `docs/LUA.md`

- [x] 9.1 Remove the per-binding API reference sections (the per-type tables and method listings)
- [x] 9.2 Add a pointer at the top to `docs/LUA_API.md` (auto-generated, canonical) and to `engine/Fury/LuaBindings.cpp` (source of truth)
- [x] 9.3 Verify the narrative sections are retained: `Engine.run` callback contract, `arg` table convention, gotchas, screenshot flags, "Future expansion" recipe

## 10. Update `docs/CLI.md`

- [x] 10.1 Add a `fury exec` section with: full syntax (`fury exec <scene> <script.lua> [args...]`), accepted extensions (`.json`/`.bin`/`.gltf`/`.glb`/`.fbx`), exit codes (0/1/2), no-window/no-engine-boot/no-GL-context invariants
- [x] 10.2 Document the `arg` table convention (`arg[0]` = script path, `arg[1..N]` = trailing args)
- [x] 10.3 Document the FBX/glTF import chain that runs before the script when the input is `.fbx`/`.gltf`/`.glb`
- [x] 10.4 Add a pointer to `docs/LUA_API.md` and `engine/Fury/LuaBindings.cpp` for the Lua API surface
- [x] 10.5 Document the limitation that `RenderUtil`/`Gui`/`Window`/`Editor.*` are inaccessible from `exec`, and that scripts wanting rendering should use the Lua launcher path with `--screenshot`
- [x] 10.6 Update the top-level help list and the "currently: `convert`, `info`, `version`" parenthetical to include `exec`

## 11. `tests/lua/` test scripts (binding smoke tests, NOT CLI-feature replicas)

- [x] 11.1 Create `tests/lua/` directory at repo root
- [x] 11.2 Implement `tests/lua/smoke_save.lua` — round-trip `Scene.GetActive()` to `arg[1]` via `FileUtil.SaveByExtension` (or `Scene.SaveActive`). Output format inferred from `arg[1]` extension. Purpose: prove the Lua save binding works end-to-end. NOT a replacement for `fury convert`.
- [x] 11.3 Implement `tests/lua/smoke_iterate.lua` — call `Scene.ForEachMesh` / `ForEachMaterial` / `ForEachNode` against `Scene.GetActive()`, log callback counts to stdout. Read-only. Purpose: prove the iteration bindings work end-to-end. NOT a replacement for `fury info`.
- [x] 11.4 Implement `tests/lua/gen_lod.lua` — selects mesh(es) by name from `arg[1]` (or all if `--all`); calls `MeshSimplifier.SimplifyMesh(mesh, {})`; attaches LOD chain via `mesh:SetLodMeshes`; if `arg[2]` provided, saves via `FileUtil.SaveByExtension`, else logs per-mesh LOD counts. Genuinely new — no existing CLI equivalent.
- [x] 11.5 Write `tests/lua/README.md` with one paragraph per script (purpose, arguments, sample `fury exec` invocation). Explicitly note that `smoke_save.lua` and `smoke_iterate.lua` are binding smoke tests, not replacements for `fury convert` or `fury info`.

## 12. Manual smoke testing

- [x] 12.1 `./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_iterate.lua` → verify ForEachMesh/Material/Node callback counts in stdout, exit 0, no file written, no window opened
- [x] 12.2 `./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_save.lua /tmp/out.bin` → verify `/tmp/out.bin` begins with the 8-byte LZ4 envelope, exit 0
- [x] 12.3 `./fury exec examples/Resource/Scene/scene.bin tests/lua/smoke_save.lua /tmp/out.json` → verify `/tmp/out.json` is valid UTF-8 JSON with top-level `textures`/`materials`/`meshes`/`nodes`, exit 0
- [x] 12.4 `./fury exec examples/Resource/Scene/scene.json tests/lua/gen_lod.lua --all /tmp/out.json` → verify every mesh has `GetLodCount() > 0`, `/tmp/out.json` is valid JSON, exit 0
- [x] 12.5 `./fury exec examples/Resource/Scene/scene.json tests/lua/gen_lod.lua --all` (no save) → verify per-mesh LOD counts logged, exit 0, no file written
- [x] 12.6 `./fury exec model.fbx tests/lua/smoke_save.lua /tmp/out.json` → verify FBX2glTF chain runs, temp `.glb` cleaned up, exit 0
- [x] 12.7 `./fury exec scene.json script_with_runtime_error.lua` → verify stderr has script name + line + error, exit 1
- [x] 12.8 `./fury exec /nonexistent.json script.lua` → verify stderr names missing file, exit 1
- [x] 12.9 `./fury exec scene.json /nonexistent.lua` → verify stderr names missing script, exit 1
- [x] 12.10 `./fury exec scene.obj script.lua` → verify stderr lists supported extensions, exit 1
- [x] 12.11 `./fury exec --help` → verify `kExecHelp` printed, exit 0, no scene loaded, no `sol::state` created
- [x] 12.12 `./fury --help` → verify top-level help lists `convert`, `info`, `exec`, `version`
- [x] 12.13 Rebuild and verify `docs/LUA_API.md` regenerates without churn (deterministic output, byte-identical to the checked-in version)
