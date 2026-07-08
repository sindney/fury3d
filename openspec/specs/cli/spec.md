# cli

## Purpose

The argv-driven entry point on the `fury` binary. Covers the dispatch contract (subcommand vs Lua-script path), the subcommand inventory (`convert`, `info`, `help`, `version`), the multi-step pipelines (`convert fbx` → FBX2glTF → glTF importer → SaveFile), exit-code conventions, and the help-text contract that `docs/CLI.md` mirrors.

## Requirements

### Requirement: The `fury` binary SHALL dispatch CLI subcommands without booting the engine when argv[1] is a known subcommand

The `fury` executable's `main()` SHALL inspect `argv[1]` before any window or engine initialization. When `argv[1]` is one of the reserved subcommand tokens (`convert`, `info`, `exec`, `help`, `--help`, `-h`, `version`, `--version`), `main()` SHALL invoke `fury::Cli::Run(argc, argv)` and return its exit code without creating an `sf::Window`, without calling `fury::Engine::Initialize`, and without running the Lua-launcher path's `sol::state`. The `exec` subcommand SHALL create its own short-lived `sol::state` internally (see the dedicated `fury exec` requirement below) — this is not the launcher's VM, and `Engine.run` is never invoked. For any other value of `argv[1]` (or when `argc == 1`), the current Lua-launcher path SHALL run unchanged.

#### Scenario: Subcommand bypasses engine init
- **WHEN** a user runs `./fury convert gltf in.gltf out.json`
- **THEN** no SFML window is opened (no window appears on screen)
- **AND** no Lua state is created
- **AND** `fury::Engine::Initialize` is not called
- **AND** the process exits with the code returned by `fury::Cli::Run`

#### Scenario: Default invocation runs Lua launcher
- **WHEN** a user runs `./fury` with no arguments
- **THEN** the current behavior is preserved: SFML window opens, engine initializes, Lua VM loads `Demo.lua`

#### Scenario: Lua script path with no subcommand collision
- **WHEN** a user runs `./fury MyScript.lua`
- **THEN** `MyScript.lua` is loaded as the Lua entry point (current behavior)
- **AND** the CLI dispatch is not triggered

#### Scenario: Help-equivalents normalized
- **WHEN** a user runs `./fury -h`, `./fury --help`, or `./fury help`
- **THEN** all three produce the same top-level help output and exit code 0

#### Scenario: Exec subcommand routes through Cli::Run
- **WHEN** a user runs `./fury exec scene.json script.lua`
- **THEN** `Cli::LooksLikeSubcommand("exec")` returns `true`
- **AND** `Cli::Run(argc, argv)` is invoked
- **AND** no SFML window is opened
- **AND** `fury::Engine::Initialize` is not called
- **AND** the `exec` handler creates its own short-lived `sol::state` internally (the Lua launcher path is not run)

### Requirement: The CLI SHALL expose `convert`, `info`, `exec`, `help`, and `version` subcommands

`fury::Cli::Run` SHALL recognize these subcommands:

- `convert <kind> <input> <output>` — convert between asset formats. Kinds: `gltf` (glTF → scene), `fbx` (FBX → glTF → scene, chained via the vendored FBX2glTF subprocess and the glTF importer), `scene` (engine scene → engine scene, `.json`/`.bin` → `.json`/`.bin`, format-preserving re-save or cross-format conversion). Unknown kinds SHALL exit with code 1 and a clear error message.
- `info <path>` — print a single-asset summary (node count, mesh count split into static vs skinned, submesh count, total triangle / vertex count, material count, animation clip count, joint count, scene-wide AABB). Accepts inputs with extensions `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`. Format inferred from extension; FBX inputs are converted to a temp glTF first.
- `exec <scene> <script.lua> [args...]` — load a scene (`.json`, `.bin`, `.gltf`, `.glb`, `.fbx` — same extension dispatch as `info`), create a `sol::state`, register engine Lua bindings, set `Scene::Active` to the loaded scene, run the user's Lua script, then exit. No SFML window is opened, no `Engine::Initialize` is called, no GL context is created. See the dedicated `fury exec` requirement for full behavior.
- `help` / `--help` / `-h` — print top-level help (list of subcommands, one-line description each). With an additional argument naming a subcommand (`help convert`, `convert --help`), print that subcommand's full help text.
- `version` / `--version` — print the engine version string (single line) and exit 0.

Each subcommand SHALL accept a trailing `--help` or `-h` flag that prints its own help and exits with code 0 without performing the action.

#### Scenario: Top-level help lists subcommands
- **WHEN** a user runs `./fury --help`
- **THEN** stdout contains the subcommand names `convert`, `info`, `exec`, `version`, each on its own line with a brief description

#### Scenario: Subcommand-specific help for convert
- **WHEN** a user runs `./fury convert --help`
- **THEN** stdout contains the full syntax `fury convert gltf <input> <output>`, `fury convert fbx <input> <output>`, and `fury convert scene <input> <output>`, the list of supported input extensions per kind (`gltf`: `.gltf`, `.glb`; `fbx`: `.fbx`; `scene`: `.json`, `.bin`), the list of supported output extensions per kind (`gltf`: `.json`, `.bin`; `fbx`: `.gltf`, `.glb`, `.json`, `.bin`; `scene`: `.json`, `.bin`), and a brief description of the lossy material and animation mappings (gltf/fbx only)

#### Scenario: Subcommand-specific help for exec
- **WHEN** a user runs `./fury exec --help` or `./fury help exec`
- **THEN** stdout contains the full syntax `fury exec <scene> <script.lua> [args...]`
- **AND** the list of accepted scene extensions (`.json`, `.bin`, `.gltf`, `.glb`, `.fbx`)
- **AND** the exit-code convention (0 success, 1 user error, 2 internal error)
- **AND** the no-window / no-engine-boot / no-GL-context invariants
- **AND** a pointer to `docs/LUA_API.md` and `engine/Fury/LuaBindings.cpp` for the available Lua API surface

#### Scenario: Info on engine scene.bin
- **WHEN** a user runs `./fury info examples/bin/Resource/Scene/scene.bin`
- **THEN** stdout contains lines naming `nodes`, `meshes`, `materials`, `animation_clips`, `joints`, and `aabb` with numeric or bounds values

#### Scenario: Info on glTF source
- **WHEN** a user runs `./fury info Triangle.gltf`
- **THEN** stdout contains the same fields as above, computed from the source glTF

#### Scenario: Unknown subcommand
- **WHEN** a user runs `./fury foo`
- **AND** `foo` is not a known subcommand and not a path to a `.lua` file that exists
- **THEN** the launcher's current behavior is preserved (it tries to run `foo` as a Lua script and fails with the existing error path)

#### Scenario: Unknown convert kind
- **WHEN** a user runs `./fury convert obj in.obj out.json`
- **THEN** stderr contains an error naming `obj` as unsupported and listing the supported kinds (`gltf`, `fbx`, `scene`)
- **AND** the process exits with code 1

### Requirement: `fury convert scene` SHALL convert engine scene files between `.json` and `.bin` without booting the engine

`fury convert scene <input> <output>` SHALL load an engine scene file (`.json` via `FileUtil::LoadFile`, `.bin` via `FileUtil::LoadCompressedFile`) and save it via `FileUtil::SaveByExtension(output)` (format by `output`'s extension). The input extension SHALL be `.json` or `.bin`; the output extension SHALL be `.json` or `.bin`. Any other extension on either argument SHALL exit with code 1 and a stderr message listing the supported extensions. The handler SHALL NOT open an SFML window, SHALL NOT call `Engine::Initialize`, and SHALL NOT create a Lua VM. The `Scene`'s `working_dir` SHALL be set to `dirname(input) + "/"` so texture relative paths resolve against the input file's directory (matching `Importer.LoadScene`'s runtime behavior). `Scene::Active` SHALL be set to the loaded scene for the duration of the load and reset to nullptr on exit.

#### Scenario: Convert scene.bin to scene.json
- **WHEN** a user runs `./fury convert scene Resource/Scene/scene.bin /tmp/scene.json`
- **THEN** the file at `/tmp/scene.json` is a UTF-8 JSON document (not LZ4-compressed)
- **AND** the document has top-level `textures`, `materials`, `meshes`, and `nodes` keys
- **AND** stdout contains a `wrote /tmp/scene.json` confirmation
- **AND** the process exits 0

#### Scenario: Convert scene.json to scene.bin
- **WHEN** a user runs `./fury convert scene /tmp/scene.json /tmp/scene.bin`
- **THEN** the file at `/tmp/scene.bin` begins with the 8-byte LZ4 envelope (network-order original size + compressed size)
- **AND** the process exits 0

#### Scenario: Format-preserving re-save
- **WHEN** a user runs `./fury convert scene in.json out.json`
- **THEN** `out.json` is a valid JSON scene document that reloads successfully via `FileUtil::LoadFile`
- **AND** the process exits 0

#### Scenario: Unsupported input extension
- **WHEN** a user runs `./fury convert scene in.gltf out.json`
- **THEN** stderr lists `.json` and `.bin` as the supported input extensions for the `scene` kind
- **AND** the process exits with code 1

#### Scenario: Unsupported output extension
- **WHEN** a user runs `./fury convert scene in.bin out.fbx`
- **THEN** stderr lists `.json` and `.bin` as the supported output extensions for the `scene` kind
- **AND** the process exits with code 1

#### Scenario: No engine boot on convert scene
- **WHEN** a user runs `./fury convert scene in.bin out.json`
- **THEN** no SFML window is opened
- **AND** no Lua state is created
- **AND** `fury::Engine::Initialize` is not called

### Requirement: `fury exec` SHALL load a scene, run a Lua script against it, and exit without booting the engine

`fury exec <scene> <script.lua> [args...]` SHALL:

1. Load the scene at `<scene>`. The extension SHALL be one of `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`. Format inferred from extension (`.json` → `FileUtil::LoadFile`, `.bin` → `FileUtil::LoadCompressedFile`, `.gltf`/`.glb` → `GltfImporter::Import`, `.fbx` → `FbxConverter::Convert` to a temp `.glb` then `GltfImporter::Import`). Any other extension SHALL exit with code 1 and a stderr message listing the supported extensions.
2. Set the loaded scene's working directory to `dirname(scene) + "/"` so sibling-file path resolution works inside the script.
3. Set `Scene::Active` to the loaded scene for the duration of the script run, then reset it to `nullptr` on exit (success or failure) via an RAII guard.
4. Create a `sol::state`, open the standard Lua stdlib set (`base`, `string`, `math`, `table`, `io`, `os`, `package`), and call `LuaBindings::Register(lua)` on it.
5. Forward arguments to the script via the `arg` table: `arg[0]` = `<script.lua>` path, `arg[1..N]` = trailing args after the script path. Unknown trailing flags SHALL be forwarded unchanged (the script decides whether to error).
6. Run the script via `sol::protected_function` with an error handler. Lua errors SHALL be reported to stderr with the script name and line number, and the process SHALL exit with code 1. `Scene::Active` SHALL be reset to `nullptr` even on Lua-error exit.
7. Exit with code 0 on successful script completion, code 1 on user errors (missing scene file, missing script file, unsupported extension, Lua error), code 2 on internal C++ exceptions escaping `DoExec`.
8. NOT open an `sf::Window`, NOT call `Engine::Initialize`, NOT create an OpenGL context, NOT call `MeshUtil::Reset()` (no GL-backed primitives are touched on the headless path — see design decision D2).
9. NOT parse `--screenshot` / `--screenshot-frame` runtime flags — those are launcher-path-only. Unknown trailing args after the script path SHALL be forwarded to the script via `arg` unchanged.

The script MAY call `FileUtil.SaveByExtension(path, scene)` (or a `Scene.SaveActive(path)` helper if introduced) to persist the (possibly modified) scene. Read-only inspection without saving is the default.

#### Scenario: Exec runs a read-only inspection script
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/scene_info.lua`
- **THEN** the script runs to completion
- **AND** stdout contains the script's logged output
- **AND** no scene file is written
- **AND** `Scene::Active` is reset to `nullptr` on exit
- **AND** no SFML window is opened
- **AND** no `Engine::Initialize` is called
- **AND** the process exits 0

#### Scenario: Exec runs a script that saves a modified scene
- **WHEN** a user runs `./fury exec scene.json tests/lua/gen_lod.lua mesh_name /tmp/out.json`
- **AND** the script calls `Scene.SaveActive("/tmp/out.json")` (or `FileUtil.SaveByExtension`)
- **THEN** the file at `/tmp/out.json` is a valid engine scene document
- **AND** the process exits 0

#### Scenario: Exec accepts glTF input
- **WHEN** a user runs `./fury exec model.glb script.lua`
- **THEN** the glTF importer runs first, producing an in-memory `Scene`
- **AND** the script runs against that scene
- **AND** the process exits 0 (on script success)

#### Scenario: Exec accepts FBX input via FBX2glTF chain
- **WHEN** a user runs `./fury exec model.fbx script.lua`
- **THEN** the FBX2glTF subprocess runs first, producing a temp `.glb`
- **AND** the glTF importer reads the temp `.glb` and emits an in-memory `Scene`
- **AND** the temp `.glb` is cleaned up on success
- **AND** the script runs against that scene
- **AND** the process exits 0 (on script success)

#### Scenario: Exec rejects unsupported scene extension
- **WHEN** a user runs `./fury exec model.obj script.lua`
- **THEN** stderr lists `.json`, `.bin`, `.gltf`, `.glb`, `.fbx` as supported extensions
- **AND** the process exits with code 1

#### Scenario: Exec missing scene file
- **WHEN** a user runs `./fury exec /nonexistent.json script.lua`
- **THEN** stderr contains an error naming the missing file
- **AND** the process exits with code 1

#### Scenario: Exec missing script file
- **WHEN** a user runs `./fury exec scene.json /nonexistent.lua`
- **THEN** stderr contains an error naming the missing script
- **AND** the process exits with code 1

#### Scenario: Exec Lua error exits 1
- **WHEN** a user runs `./fury exec scene.json script.lua`
- **AND** the script raises a Lua error at runtime
- **THEN** stderr contains the script name, line number, and error message
- **AND** the process exits with code 1
- **AND** `Scene::Active` is reset to `nullptr` on exit

#### Scenario: Exec C++ exception exits 2
- **WHEN** a user runs `./fury exec scene.json script.lua`
- **AND** a C++ exception escapes the script handler
- **THEN** stderr contains the exception's `what()` and `exec`
- **AND** the process exits with code 2
- **AND** `Scene::Active` is reset to `nullptr` on exit

#### Scenario: Exec forwards trailing args via the arg table
- **WHEN** a user runs `./fury exec scene.json script.lua foo bar`
- **THEN** the running script sees `arg[0] == "script.lua"`, `arg[1] == "foo"`, `arg[2] == "bar"`, `#arg == 2`

#### Scenario: Exec does not parse screenshot flags
- **WHEN** a user runs `./fury exec scene.json script.lua --screenshot /tmp/x.png`
- **THEN** `arg[1] == "--screenshot"` and `arg[2] == "/tmp/x.png"` (forwarded to script unchanged)
- **AND** no screenshot is taken
- **AND** no SFML window is opened

#### Scenario: Exec resets Scene::Active on success
- **WHEN** a user runs `./fury exec scene.json script.lua` and the script completes without error
- **THEN** `Scene::Active` is `nullptr` after the process exits

#### Scenario: Exec resets Scene::Active on failure
- **WHEN** a user runs `./fury exec scene.json script.lua` and the script raises a Lua error
- **THEN** `Scene::Active` is `nullptr` after the process exits (the swap-and-reset dance runs in an RAII guard)

#### Scenario: Exec --help exits 0 without loading
- **WHEN** a user runs `./fury exec --help` or `./fury exec -h`
- **THEN** stdout contains the `fury exec` help text
- **AND** no scene is loaded
- **AND** no `sol::state` is created
- **AND** the process exits 0

### Requirement: The CLI SHALL use stable exit codes

The CLI SHALL exit with code `0` on success, code `1` for user errors (bad arguments, unsupported input, file not found, unsupported glTF features), and code `2` for internal errors (assertion failures, exceptions escaping the subcommand handler). Error messages SHALL be written to stderr; informational output SHALL be written to stdout.

#### Scenario: User error returns 1
- **WHEN** the user passes an output path with an unsupported extension
- **THEN** the process exits with code 1

#### Scenario: Internal error returns 2
- **WHEN** an unexpected exception escapes a subcommand handler
- **THEN** the process exits with code 2
- **AND** stderr contains the exception's `what()` string and the subcommand name

#### Scenario: Success returns 0
- **WHEN** a `convert` subcommand completes without error
- **THEN** the process exits with code 0
- **AND** stdout (optionally) contains a one-line "wrote <path>" confirmation

### Requirement: The CLI SHALL be documented in `docs/CLI.md` with the same exhaustive shape as `docs/LUA.md`

A `docs/CLI.md` file SHALL be present in the repository, structured to be self-sufficient for an AI agent to use the CLI without reading source code. It SHALL contain:

- A "How it works" section describing the dispatch shape (subcommand router in `examples/main.cpp`, no engine boot on the CLI path).
- A subcommand reference: one section per subcommand listing full argument syntax, all flags, supported file extensions, exit codes, and known limitations / lossy mappings.
- An appendix describing the engine's runtime scene format: top-level keys (`materials`, `meshes`, `nodes`), how the node tree is structured, what components attach to nodes, what's precomputed (AABBs, submesh-per-material splits, the LZ4 envelope for `.bin`).
- A "Future expansion" section explaining how to add a new subcommand (where it lives in `Cli.cpp`, how help strings are wired, how to document it in this file).

Subcommand help text printed by the CLI itself SHALL match the descriptive content in `docs/CLI.md` (same supported extensions, same limitations, same exit codes).

The `fury exec` section SHALL additionally document:

- The no-window / no-`Engine::Initialize` / no-GL-context invariants.
- The `arg` table convention for argument forwarding (`arg[0]` = script path, `arg[1..N]` = trailing args).
- The accepted scene extensions and how `.fbx`/`.gltf`/`.glb` inputs are imported (via FBX2glTF + `GltfImporter`) before the script runs.
- A pointer to `docs/LUA_API.md` and `engine/Fury/LuaBindings.cpp` for the Lua API surface.
- The limitation that `RenderUtil`/`Gui`/`Window`/`Editor.*` are inaccessible from `exec`, and that scripts wanting rendering should use the Lua launcher path with `--screenshot`.

#### Scenario: docs/CLI.md exists and covers every subcommand
- **WHEN** the repository is inspected
- **THEN** `docs/CLI.md` exists
- **AND** it contains a section for each subcommand returned by `./fury help` (currently: `convert`, `info`, `exec`, `version`)

#### Scenario: Help text and docs/CLI.md agree on supported extensions
- **WHEN** the user reads `./fury convert --help` and the corresponding section in `docs/CLI.md`
- **THEN** the same supported input extensions and output extensions are listed in both

#### Scenario: docs/CLI.md documents exec limitations and pointers
- **WHEN** the user reads the `fury exec` section of `docs/CLI.md`
- **THEN** it documents the no-window / no-engine-boot / no-GL-context invariants
- **AND** it documents the `arg` table convention
- **AND** it points to `docs/LUA_API.md` and `engine/Fury/LuaBindings.cpp` for the Lua API surface
- **AND** it documents that `RenderUtil`/`Gui`/`Window`/`Editor.*` are inaccessible from `exec`

### Requirement: The CLI SHALL chain `convert fbx` through FBX2glTF + glTF importer when the output is `.json` or `.bin`

`fury convert fbx <input.fbx> <output>` SHALL behave according to the output extension:

- `<output>` ends in `.gltf` or `.glb` → invoke `FbxConverter::Convert(input, dirname(output))` and move/rename the produced file to `<output>`. No glTF-importer step is run.
- `<output>` ends in `.json` or `.bin` → invoke `FbxConverter::Convert` to produce a temp `.glb` in a system tempdir, then call `GltfImporter::Import` on the temp file, then call `FileUtil::SaveFile` (for `.json`) or `FileUtil::SaveCompressedFile` (for `.bin`) on the resulting `Scene`. The temp `.glb` SHALL be cleaned up on success and preserved (with its path named in the error message) on failure of the glTF-importer step.

The same warning- and rejection-policy applies as for `convert gltf`: lossy PBR → Lambert mapping with one warning per source material, morph targets / sparse accessors / byte-stride / required extensions rejected at the importer step.

#### Scenario: Chain FBX to scene.bin
- **WHEN** a user runs `./fury convert fbx Resource/Scene/james.fbx /tmp/james.bin`
- **THEN** the FBX2glTF subprocess runs first, producing a temp `.glb`
- **AND** the glTF importer reads the temp `.glb` and emits an in-memory `Scene`
- **AND** the scene is written to `/tmp/james.bin` via `FileUtil::SaveCompressedFile`
- **AND** the temp `.glb` is deleted
- **AND** the process exits 0

#### Scenario: FBX to glTF stops after FBX2glTF
- **WHEN** a user runs `./fury convert fbx in.fbx out.glb`
- **THEN** only the FBX2glTF subprocess is invoked
- **AND** no glTF-importer call is made
- **AND** the produced `out.glb` is the FBX2glTF binary's output

#### Scenario: Chain preserves intermediate on importer failure
- **WHEN** the glTF-importer step fails after FBX2glTF succeeds (e.g. the FBX contained morph targets that FBX2glTF preserved into the glTF and our importer rejects)
- **THEN** the temp `.glb` is preserved
- **AND** the error message names the temp file's path so the developer can inspect it

#### Scenario: Unsupported FBX output extension
- **WHEN** a user runs `./fury convert fbx in.fbx out.fbx` or `./fury convert fbx in.fbx out.xml`
- **THEN** the process exits with code 1
- **AND** stderr lists the supported output extensions for the `fbx` kind (`.gltf`, `.glb`, `.json`, `.bin`)

### Requirement: The Lua-launcher path SHALL recognize `--screenshot` and `--screenshot-frame` runtime flags

The non-subcommand path of the `fury` binary (the path that creates a window and runs a Lua script) SHALL recognize two new flags appearing anywhere in `argv` after `argv[1]`:

- `--screenshot <path>`: Capture a PNG of the rendered scene to `<path>` after the configured number of frames, then exit cleanly. See the `screenshot-debug` spec for full behavior.
- `--screenshot-frame <N>`: Configure the frame index at which capture happens. Default `2`.

These are **runtime flags**, not CLI subcommands — they do NOT trigger `Cli::Run` and do NOT cause `Cli::LooksLikeSubcommand` to return `true`. The Lua-launcher path SHALL parse them out of `argv` (along with their values), then pass the remaining argv to the Lua `arg` table as the existing forwarding requirement specifies. From the Lua script's perspective, the flags are invisible.

When `--screenshot` is absent, the launcher behavior is unchanged from the prior spec — the engine runs to completion under user control.

#### Scenario: Screenshot flags do not trigger CLI subcommand routing

- **WHEN** a user runs `./fury Demo.lua --screenshot /tmp/x.png`
- **THEN** `Cli::LooksLikeSubcommand("Demo.lua")` returns `false` (its current behavior — `--screenshot` doesn't change subcommand detection)
- **AND** the engine creates an `sf::Window` and initializes the Lua VM (the Lua launcher path runs)
- **AND** the screenshot is captured per the `screenshot-debug` spec

#### Scenario: Flags are stripped before Lua sees `arg`

- **WHEN** a user runs `./fury Demo.lua tank.fbx --screenshot /tmp/x.png --screenshot-frame 5`
- **THEN** the running Lua script sees `arg[0] == "Demo.lua"`, `arg[1] == "tank.fbx"`, `#arg == 1`
- **AND** the script does NOT see `--screenshot`, `/tmp/x.png`, `--screenshot-frame`, or `5`

#### Scenario: Subcommand path is unaffected

- **WHEN** a user runs `./fury convert fbx in.fbx out.glb --screenshot /tmp/x.png`
- **THEN** the `convert` subcommand handles the `--screenshot` flag according to its own argument-parsing rules (in v1 the subcommands SHALL reject unknown trailing flags with a clear error — `--screenshot` is a runtime flag and is invalid for offline subcommands)
- **AND** no window is created and no screenshot is taken
