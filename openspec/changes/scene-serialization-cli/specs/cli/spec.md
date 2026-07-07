## MODIFIED Requirements

### Requirement: The CLI SHALL expose `convert`, `info`, `help`, `version`, and `verify` subcommands

`fury::Cli::Run` SHALL recognize these subcommands:

- `convert <kind> <input> <output>` — convert between asset formats. Kinds: `gltf` (glTF → scene), `fbx` (FBX → glTF → scene, chained via the vendored FBX2glTF subprocess and the glTF importer), `scene` (engine scene → engine scene, `.json`/`.bin` → `.json`/`.bin`, format-preserving re-save or cross-format conversion). Unknown kinds SHALL exit with code 1 and a clear error message.
- `info <path>` — print a single-asset summary (node count, mesh count split into static vs skinned, submesh count, total triangle / vertex count, material count, animation clip count, joint count, scene-wide AABB). Accepts inputs with extensions `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`. Format inferred from extension; FBX inputs are converted to a temp glTF first.
- `verify <path> [--round-trip [<out>]]` — load a scene and check it deserialized cleanly; with `--round-trip`, additionally save to `<out>` (format by extension, default temp `.bin`), reload, and diff structural counts. Prints counts to stdout and a `OK`/`MISMATCH` line; exits 0 on success/counts-match, 1 on load failure or count mismatch.
- `help` / `--help` / `-h` — print top-level help (list of subcommands, one-line description each). With an additional argument naming a subcommand (`help convert`, `convert --help`), print that subcommand's full help text.
- `version` / `--version` — print the engine version string (single line) and exit 0.

Each subcommand SHALL accept a trailing `--help` or `-h` flag that prints its own help and exits with code 0 without performing the action.

#### Scenario: Top-level help lists subcommands

- **WHEN** a user runs `./fury --help`
- **THEN** stdout contains the subcommand names `convert`, `info`, `verify`, `version`, each on its own line with a brief description

#### Scenario: Subcommand-specific help for convert

- **WHEN** a user runs `./fury convert --help`
- **THEN** stdout contains the full syntax `fury convert gltf <input> <output>`, `fury convert fbx <input> <output>`, and `fury convert scene <input> <output>`, the list of supported input extensions per kind (`gltf`: `.gltf`, `.glb`; `fbx`: `.fbx`; `scene`: `.json`, `.bin`), the list of supported output extensions per kind (`gltf`: `.json`, `.bin`; `fbx`: `.gltf`, `.glb`, `.json`, `.bin`; `scene`: `.json`, `.bin`), and a brief description of the lossy material and animation mappings (gltf/fbx only)

#### Scenario: Subcommand-specific help for verify

- **WHEN** a user runs `./fury verify --help`
- **THEN** stdout contains the full syntax `fury verify <path> [--round-trip [<out>]]`, the list of supported input extensions (`.json`, `.bin`), the default round-trip output (temp `.bin` in the system tempdir), the counted fields, and the exit-code contract

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

## ADDED Requirements

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

### Requirement: `fury verify` SHALL load a scene and optionally round-trip it, reporting a structural-count diff

`fury verify <path> [--round-trip [<out>]]` SHALL load a scene file (`.json`/`.bin`) into a fresh `Scene` with `working_dir = dirname(path) + "/"`. If the load returns false or throws, the subcommand SHALL print the error to stderr and exit 1. On a successful load, it SHALL print the structural counts (nodes, meshes total/static/skinned, submeshes, vertices, triangles, materials, textures, animations, joints) to stdout.

Without `--round-trip`, the subcommand SHALL exit 0 after printing counts.

With `--round-trip` and no `<out>`, the subcommand SHALL save the loaded scene to a temp `.bin` in the system tempdir (path prefixed `fury_verify_`). With `--round-trip <out>`, it SHALL save to `<out>` via `FileUtil::SaveByExtension` (format by `<out>`'s extension). It SHALL then reload `<out>` into a second fresh `Scene`, compute the same structural counts, print both sets of counts, print a `OK` line if all counts match or a `MISMATCH: <field> <a> vs <b>` line per differing field, and exit 0 if all counts match or 1 if any count differs or the second load fails. The temp/output file SHALL be preserved on mismatch (path named in stderr) and deleted on success.

Counted fields for the round-trip comparison: `nodes`, `meshes_total`, `meshes_static`, `meshes_skinned`, `submeshes`, `vertices`, `triangles`, `materials`, `textures`, `animations`, `joints`. AABB is reported but not compared.

`fury verify` SHALL NOT open an SFML window, SHALL NOT call `Engine::Initialize`, and SHALL NOT create a Lua VM.

#### Scenario: Verify a loadable scene without round-trip

- **WHEN** a user runs `./fury verify Resource/Scene/scene.bin`
- **THEN** stdout contains lines naming `nodes`, `meshes`, `materials`, `textures`, `joints` with numeric values
- **AND** the process exits 0

#### Scenario: Verify a corrupt scene exits non-zero

- **WHEN** a user runs `./fury verify /tmp/broken.bin`
- **AND** `FileUtil::LoadCompressedFile` returns false (e.g. parse error, missing member)
- **THEN** stderr contains an error message naming the file and the failure
- **AND** the process exits with code 1

#### Scenario: Verify round-trip on a healthy scene reports OK

- **WHEN** a user runs `./fury verify Resource/Scene/scene.bin --round-trip`
- **THEN** stdout contains two sets of structural counts (original and round-tripped)
- **AND** stdout contains a line beginning with `OK`
- **AND** the temp round-trip file is deleted
- **AND** the process exits 0

#### Scenario: Verify round-trip on a lossy save reports MISMATCH

- **WHEN** a user runs `./fury verify some.json --round-trip some_rt.json`
- **AND** the round-trip drops a texture (e.g. a textures-array read bug causes the reloaded texture count to be lower)
- **THEN** stdout contains a line beginning with `MISMATCH: textures` showing the two counts
- **AND** stderr names the preserved round-trip file's path for inspection
- **AND** the round-trip file at `some_rt.json` is NOT deleted
- **AND** the process exits with code 1

#### Scenario: Verify round-trip with explicit .json output

- **WHEN** a user runs `./fury verify scene.bin --round-trip /tmp/rt.json`
- **THEN** the round-trip file at `/tmp/rt.json` is a UTF-8 JSON document (not LZ4-compressed)
- **AND** stdout reports the counts and an `OK` or `MISMATCH` line

#### Scenario: Unsupported input extension on verify

- **WHEN** a user runs `./fury verify in.fbx`
- **THEN** stderr lists `.json` and `.bin` as the supported input extensions for `verify`
- **AND** the process exits with code 1

#### Scenario: No engine boot on verify

- **WHEN** a user runs `./fury verify scene.bin --round-trip`
- **THEN** no SFML window is opened
- **AND** no Lua state is created
- **AND** `fury::Engine::Initialize` is not called
