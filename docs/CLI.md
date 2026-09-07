# Fury3D — CLI

> Status (2026-07-07): `fury` is a single binary that hosts both the Lua
> launcher (runtime path) and an offline asset CLI. AI agents and authoring
> tools can drive asset workflows through the CLI without ever touching the
> Lua runtime.

## How it works

The `fury` binary's `main()` inspects `argv[1]` before any window or engine
initialization:

- If `argv[1]` is a known CLI subcommand token (`convert`, `info`, `exec`,
  `help`, `--help`, `-h`, `version`, `--version`), `main()` dispatches to
  `fury::Cli::Run`. The CLI path is **pure C++ asset workflows**: no SFML
  window opens, `Engine::Initialize` is never called, no Lua VM is created
  on the `convert` / `info` paths. The `exec` path is a special case — it
  creates a short-lived `sol::state` and runs a Lua script against a loaded
  scene, but it still does NOT open a window, NOT call `Engine::Initialize`,
  and NOT create an OpenGL context. See the `fury exec` section below.
- Otherwise, `argv[1]` is treated as a Lua script path (current behavior;
  defaults to `Editor.lua` if no arg). See `docs/LUA.md` for that surface.
  Any remaining `argv[2..]` is forwarded to the script as a standard Lua
  `arg` table — `Editor.lua` honors `arg[1]` as an optional startup scene
  (e.g. `./fury Editor.lua outdoor.fbx`). See LUA.md for the convention.
  Two **runtime flags** (`--screenshot`, `--screenshot-frame`) are
  recognized on the launcher path and stripped from `arg` before the
  script sees it. See "Screenshot mode" below.

This means a CLI invocation like `./fury convert gltf in.gltf out.json` is
fast and predictable: no graphics state, no UI, no script VM. The CLI is
exactly what an AI agent or build system wants.

## Screenshot mode

The launcher path accepts two runtime flags that capture a PNG of the
rendered scene and exit. Useful for verifying a Lua script's visual output
from a CLI loop, screenshotting a regression, or smoke-testing imports
without driving the GUI by hand.

```
fury <script.lua> [args...] --screenshot <path> [--screenshot-frame <N>]
```

**Flags:**

- `--screenshot <path>` — capture an 8-bit RGBA PNG of the rendered
  back-buffer to `<path>` after the configured number of frames, then
  exit cleanly. The PNG is encoded via the engine's vendored
  `stb_image_write`; no new third-party dependency. The flag and its
  value are stripped from `arg` so user scripts can be run identically
  with or without capture.
- `--screenshot-frame <N>` — frame index (1-based) at which capture
  happens. Default `2` (one full update tick has run, so scripts that
  set state in `on_init` and animate in `on_update` show their first
  animated frame). `N` must be a positive integer; values larger than
  `1000` are clamped with a warning. `N <= 0` exits with code 1.
- `--screenshot-series <path,N,interval>` — temporal capture: grab the
  back-buffer every `interval` frames starting at `--screenshot-frame`,
  `N` frames total, then write ONE contact-sheet atlas PNG (near-square
  grid, chronological left-to-right top-to-bottom) to `<path>` and exit.
  For diagnosing temporal artifacts (flicker, popping, stepping) that a
  single screenshot can't show. `N` 1..64, `interval` >= 1.

**Headless caveat.** SFML/OpenGL needs a window to provide a GL context,
so an `sf::Window` opens briefly during capture (typically 50–150 ms
before the engine exits). True offscreen rendering is out of scope for v1.

**Exit codes:**

- `0` — capture succeeded; the PNG exists at `<path>`.
- `1` — capture failed (e.g. the parent directory doesn't exist), or
  the user supplied a bad flag value. The error is logged via the engine
  logger and the process exits without crashing.

**Examples:**

```bash
# Capture the demo's startup scene at frame 2 (default).
./fury Editor.lua --screenshot /tmp/demo_default.png

# Capture an FBX import — verify imported textures applied correctly.
./fury Editor.lua tank.fbx --screenshot /tmp/demo_tank.png

# Wait 30 frames so an on_update animation has progressed.
./fury Editor.lua --screenshot /tmp/late.png --screenshot-frame 30

# furye: open per-asset editor windows at startup (arg[2] = comma-separated
# type:name list; types: particle, mesh) for headless editor verification.
./furye Editor.lua Projects/outdoor/outdoor_water.bin \
  particle:FireEmber,mesh:Feu --screenshot /tmp/editors.png --screenshot-frame 90
```

## Subcommands

### `fury convert` — translate an asset to the engine's runtime form

```
fury convert gltf <input.gltf|.glb> <output.json|.bin>
fury convert fbx  <input.fbx>      <output.gltf|.glb|.json|.bin>
```

**Kinds:**

- `gltf` — load a glTF 2.0 file via the vendored tinygltf, translate to
  engine types (Scene, SceneNode, Mesh + SubMesh, Material, Joint,
  AnimationClip), and serialize via `FileUtil::SaveFile` (`.json`,
  human-readable) or `FileUtil::SaveCompressedFile` (`.bin`,
  LZ4-compressed).
- `fbx` — convert FBX → glTF via the vendored `FBX2glTF` subprocess first.
  If the output extension is `.glb`, the chain stops there. If it's
  `.json`/`.bin`, the chain continues through the glTF importer to produce
  the engine runtime form. Intermediate `.glb` files land in a tempdir and
  are cleaned up on success (preserved with their path named in the error
  on failure of the importer step). `.gltf` output is **not supported** in
  v1 because FBX2glTF is invoked with `--binary` and writes `.glb`.

**Texture sibling files (`.json`/`.bin` outputs).** When the output is
`.json` or `.bin`, embedded glTF/FBX texture bytes are extracted to
sibling files next to the output and the saved scene's texture entries
reference those files by bare filename. Filename rules:

- Prefer the original filename carried by the source (FBX2glTF preserves
  the original FBX texture filename in `image.name`, e.g. `body.jpg`).
- Otherwise synthesize `<output_stem>_<texture_name>.<ext>`, where
  `<ext>` is sniffed from the encoded bytes (JPEG / PNG / BMP).
- On filename collision, the second/third/etc. extracted file gets a
  `_<n>` suffix.
- If a byte-equal file already exists at the target path, the write is
  skipped (idempotent re-saves don't re-touch mtimes).

So `fury convert fbx tank.fbx /tmp/tank_out.json` produces
`/tmp/tank_out.json` plus `/tmp/body.jpg`, `/tmp/wheels.jpg`,
`/tmp/grass.jpg` (the original FBX-embedded texture filenames). Saved
scenes are portable — copy the output directory anywhere and the texture
references resolve.

**Output format is inferred from the extension.** Mismatches error clearly
with exit code 1.

**Lossy material mapping (v1, LDR-only):** the engine ships a Lambert
deferred pipeline; HDR/PBR is a deliberate later step. The glTF importer
maps PBR metallic-roughness → Lambert as follows:

| glTF (PBR)                              | Engine (Lambert)                                                |
|-----------------------------------------|-----------------------------------------------------------------|
| `baseColorFactor` (rgba)                | `diffuse_color` (rgb) + `transparency` (1 − a)                  |
| `baseColorTexture`                      | `diffuse_texture` slot                                          |
| `emissiveFactor`                        | `emissive_color`                                                |
| `alphaMode = OPAQUE`                    | `opaque = true`                                                 |
| `alphaMode = BLEND` / `MASK`            | `opaque = false`                                                |
| `metallicFactor`, `roughnessFactor`     | **discarded** (one warning per source material)                 |
| `metallicRoughnessTexture`              | **discarded** (one warning per source material)                 |
| `normalTexture`                         | **discarded** (one warning per source material)                 |
| `occlusionTexture`, `emissiveTexture`   | **discarded** (one warning per source material)                 |

When the HDR pipeline lands, a follow-up change will add a PBR material
variant alongside Lambert without re-architecting the importer.

**Animation time-base:** glTF stores keyframes as float seconds; the engine's
`AnimationClip` uses integer ticks at fixed 24 fps. Channels are resampled at
24 Hz; rotations are slerped and stored as Euler radians (YXZ). CUBICSPLINE
samplers warn once per sampler and fall back to LINEAR.

For FBX input the converter passes `--anim-framerate bake24` to FBX2glTF so
the generated glTF samples already line up with the engine's tick rate
before the glTF importer resamples.

**Rejections (exit 1, with a clear stderr message naming the feature):**

- Morph targets (`primitive.targets` non-empty)
- Sparse accessors (`accessor.sparse.isSparse`)
- Non-default buffer-view `byteStride`
- Non-triangle primitives (`primitive.mode != 4`)
- Non-empty `extensionsRequired`

**Scale options** (apply to all kinds before save; `--auto-scale` only
triggers when the scene's largest world-AABB dimension is under 100 units,
i.e. looks authored in cm rather than the engine's m unit):

```
--scale N        multiply every top-level node's local scale by N
--auto-scale     smallest power-of-100 scaling that lifts max-dim to >= 100 units
```

When `--auto-scale` is *not* needed (the input already sits at engine
scale), the command prints `--auto-scale not needed (max-dim X units)`
and proceeds without scaling — so it's safe to leave in scripted batches.
`--scale` wins over `--auto-scale` when both are present.

**Examples:**

```bash
# Static glTF → engine .bin (LZ4-compressed)
./fury convert gltf model.gltf scene.bin

# Binary glTF → human-readable engine .json
./fury convert gltf character.glb character.json

# FBX → engine .bin (chained via FBX2glTF)
./fury convert fbx examples/Resource/Scene/james.fbx /tmp/james.bin

# FBX → glTF only (no engine chain)
./fury convert fbx in.fbx out.glb
```

### `fury info` — CPU-side summary of an asset file

```
fury info <path>
```

**Supported extensions:**

- `.json` — engine scene format (`FileUtil::LoadFile`)
- `.bin` — engine scene format (LZ4-compressed `FileUtil::LoadCompressedFile`)
- `.gltf` — glTF 2.0 ASCII (read via tinygltf directly; no full import)
- `.glb` — glTF 2.0 binary (same)
- `.fbx` — chained through FBX2glTF subprocess into a temp glb, then read

**Output fields (one per line, key/value):**

```
path:           <input path>
format:         gltf | gltf-binary | scene-json | scene-bin
nodes:          <count>
meshes:         <total>  (static: <n>, skinned: <n>)
submeshes:      <count>
vertices:       <total>
triangles:      <total>
materials:      <count>
animations:     <count>
joints:         <total across all skins>
particles:      <systems> systems, <renderers> renderers
aabb:           min=(x, y, z) max=(x, y, z)
```

(`aabb` is computed for engine-format inputs by walking the loaded SceneNode
tree with world transforms. For glTF inputs it prints "(not computed for
glTF inputs in v1)" — comparing node/mesh/joint counts is enough for the
round-trip-verification use case.)

`info` is the agent-friendly inspection tool. Pair it with `convert` to
verify that a glTF input and its converted engine output have matching
counts (modulo expected differences from PBR-discarded fields).

### `fury exec` — load a scene and run a Lua script against it (headless)

```
fury exec <scene> <script.lua> [args...]
```

Loads a scene, sets it as `Scene::Active`, opens a short-lived `sol::state`,
registers engine Lua bindings, and runs the user's script to completion —
then exits. Designed for batch scene surgery from AI agents or build scripts
without ever touching the GUI.

**Accepted scene extensions** (same dispatch as `info`):

- `.json` — engine scene (`FileUtil::LoadFile`)
- `.bin` — engine scene (`FileUtil::LoadCompressedFile`)
- `.gltf` — glTF 2.0 ASCII (`GltfImporter::Import`)
- `.glb` — glTF 2.0 binary (`GltfImporter::Import`)
- `.fbx` — chained via `FBX2glTF` subprocess into a temp `.glb`, then
  `GltfImporter::Import`. Temp `.glb` is cleaned up on success.

**Invariants.** The `exec` path is **headless**:

- No SFML window is opened.
- `Engine::Initialize` is not called.
- No OpenGL context is created.
- `MeshUtil::Reset()` is not called — no GL-backed primitives are touched
  on this path (all mesh operations are CPU-side; see design decision D2).

**Argument forwarding (`arg` table).** Trailing args after the script path
are forwarded to the script via the standard Lua `arg` table:

```
arg[0]    = <script.lua> path
arg[1..N] = trailing args (forwarded verbatim — no flag parsing)
```

`exec` does NOT parse `--screenshot` / `--screenshot-frame` (those are
launcher-path-only). Unknown trailing flags pass through to the script
unchanged; the script decides whether to error.

**Import chain for non-engine formats.** When the input is `.fbx` /
`.gltf` / `.glb`, the import runs **before** the script — the script
never sees the temp `.glb`, only the in-memory `Scene` produced by
`GltfImporter::Import`.

**Lua API surface.** See **[`docs/LUA_API.md`](LUA_API.md)** for the
canonical per-binding reference (auto-generated from
`engine/Fury/LuaBindings.cpp` at build time). For the C++ side, read
`engine/Fury/LuaBindings.cpp` directly — that's the source of truth.

**Limitations.** `RenderUtil`, `Gui`, `Window`, and `Editor.*` are
inaccessible from `exec` (the underlying singletons aren't initialized —
calling them is a clean error / no-op). Scripts that want rendering,
screenshots, or GUI should use the Lua launcher path with `--screenshot`
(see "Screenshot mode" above).

**Exit codes:**

- `0` — script ran to completion without raising.
- `1` — user error (missing scene file, missing script file, unsupported
  extension, Lua runtime error caught via `sol::protected_function`'s
  error handler). `Scene::Active` is reset to `nullptr` even on this path.
- `2` — internal C++ exception escaped `DoExec`. Also resets
  `Scene::Active` on exit.

**Examples:**

```bash
# Iterate scene contents from Lua (read-only).
./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_iterate.lua

# Round-trip a scene to .bin via the Lua save binding.
./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_save.lua /tmp/out.bin

# Generate LODs and save to a new scene file.
./fury exec examples/Resource/Scene/scene.json tests/lua/gen_lod.lua --all /tmp/out.json

# Run on an FBX (FBX2glTF chain runs first).
./fury exec model.fbx tests/lua/smoke_iterate.lua

# Help (no scene loaded, no sol::state created).
./fury exec --help
./fury help exec
```

### `fury kraut` — generate and import Kraut trees

```
fury kraut generate <descriptor.tree> [--seed N] [--out dir]
fury kraut import   <tree.glb> [output.json|.bin]
```

The Kraut-CLI toolchain (vendored `engine/ThirdParty/Kraut` submodule) is built
by the `kraut_tools` CMake target and the binaries land next to the fury
executables (the FBX2glTF distribution pattern). Configure with
`-DFURY_WITH_KRAUT=OFF` to skip the toolchain entirely, or
`-DFURY_WITH_KRAUT_PREVIEW=OFF` to build KrautCLI without KrautPreview (no SDL2
fetch; atlas baking and previews are skipped at runtime with a warning).

**generate** runs KrautCLI's glb export: `<stem>.glb` carries per-LOD meshes
(`<stem>_LOD<n>`, LOD 0 = full detail), a billboard quad (`<stem>_Billboard`),
`COLOR_0` wind weights (R=sway, G=flutter, B=phase, A=color variation), PBR
materials, and `asset.extras.kraut` (LOD thresholds, billboard atlas grid,
seed/descriptor provenance). Referenced textures are copied next to the glb —
`.dds` references resolve to `.tga`/`.png` siblings because the engine's
STB-based loader cannot read DDS. KrautPreview then bakes
`<stem>_BillboardAtlas.png` (cylindrical billboard atlas; cell *k* is the view
from azimuth `((k+0.5)/cols - 0.5) * 2pi` around the trunk's +Z) and
`<stem>_tier<n>.png` preview screenshots. Deterministic per descriptor + seed.

**import** reads a Kraut-exported glb into an engine scene fragment
(`.json`/`.bin`): the LOD chain keeps the extras' thresholds, the billboard
quad becomes the flagged terminal tier (atlas dims in the material's
`u_billboard_atlas` uniform), foliage materials get two-sided + MASK + wind
flags, and the tree's root nodes are scaled meters -> centimeters (glTF is
meters; the engine's world unit is cm). Default output: `<tree>.bin` next to
the glb. glbs without kraut extras fall back to the importer's synthesized
linear LOD thresholds and get no billboard tier.

```
./fury kraut generate engine/ThirdParty/Kraut/Data/Content/Trees/PalmTree2.tree --seed 7 --out /tmp/palm
./fury kraut import /tmp/palm/PalmTree2.glb /tmp/palm/PalmTree2.bin
```

Exit codes follow the global table; the tool's load failure (missing
descriptor) maps to 1, generation/export failures to 2.

### `fury help` — print help

```
fury help                    # same as fury --help / fury -h: top-level help
fury help <subcommand>       # detailed help for one subcommand
fury <subcommand> --help     # same as `fury help <subcommand>`
```

The same help strings printed here are mirrored in this document. If the two
ever drift, this doc is the source of truth.

### `fury version`

```
fury version
```

Prints `fury <version>` on stdout. Used by build scripts to confirm a binary.

## Exit codes

| Code | Meaning                                                         |
|------|-----------------------------------------------------------------|
| 0    | Success                                                         |
| 1    | User error — bad arguments, unsupported input, file not found, glTF feature rejected |
| 2    | Internal error — uncaught exception in a subcommand handler     |

Stderr carries diagnostic messages; stdout carries informational output and
the `wrote <path>` confirmation on successful conversion.

## FBX2glTF subprocess

`fury convert fbx` and `fury info <path>.fbx` shell out to the vendored
`FBX2glTF` binary. Layout:

```
engine/ThirdParty/FBX2glTF/
├── FBX2glTF-darwin-x64       # macOS (runs on arm64 via Rosetta)
├── FBX2glTF-linux-x64        # Linux
├── FBX2glTF-windows-x64.exe  # Windows
├── LICENSE.txt
└── README.md
```

The CMake build's `POST_BUILD` step copies the platform-appropriate binary
next to the built `fury` executable. At runtime, `FbxConverter::LocateBinary`
walks from the executable's own path (`_NSGetExecutablePath` /
`/proc/self/exe` / `GetModuleFileNameW`), so the converter finds the binary
regardless of the user's `cwd`.

If you're on macOS arm64 and don't have Rosetta installed, `fury convert fbx`
will return exit code 1 with a stderr message naming the issue and the fix:
`softwareupdate --install-rosetta`.

The converter passes `--binary` (so FBX2glTF writes a single `.glb` instead
of its default directory layout) and `--anim-framerate bake24` (to match the
engine's tick-based `AnimationClip` shape) to the subprocess.

If you discover that an FBX input trips FBX2glTF in an interesting way, you
can reproduce the failure outside `fury` by running the binary directly with
the same arguments:

```bash
./FBX2glTF-darwin-x64 --binary --anim-framerate bake24 \
  --input mymodel.fbx --output /tmp/mymodel
```

## Appendix: Engine scene format at a glance

The engine's runtime scene format is a JSON document (LZ4-compressed in the
`.bin` variant). It has three top-level arrays plus a root-node tree:

```jsonc
{
    "name": "main",
    "materials": [ ... ],   // engine Material records (Lambert uniforms + texture refs)
    "meshes":    [ ... ],   // engine Mesh records (vertex streams + submeshes + skin data)
    "nodes": {              // root SceneNode + recursive children
        "name": "RootNode",
        "transform": { ... },
        "components": { ... },
        "children": [ ... ]
    }
}
```

**Material** (one entry per `materials[i]`):

```jsonc
{
    "name": "Material_Body",
    "opaque": true,
    "texture_flags": 2,
    "shaders": [],
    "textures": [
        { "key": "diffuse_texture", "name": "body.jpg",
          "path": "Resource/Scene/body.jpg",
          "mipmap": true, "srgb": true,
          "filter": "linear_mipmap_linear", "wrap": "repeat" }
    ],
    "uniforms": [
        { "key": "diffuse_color",   "type": "Uniform3f", "data": [0.8, 0.8, 0.8] },
        { "key": "transparency",    "type": "Uniform1f", "data": [0.0] },
        { "key": "shininess",       "type": "Uniform1f", "data": [32.0] },
        // ...
    ]
}
```

**Mesh** (one entry per `meshes[i]`):

```jsonc
{
    "name": "T90",
    "cast_shadows": true,
    "positions":    [...],  // flat float array, 3 per vertex
    "normals":      [...],  // 3 per vertex
    "tangents":     [...],  // 3 per vertex (handedness dropped at import time)
    "uvs":          [...],  // 2 per vertex
    "bone_ids":     [...],  // optional: 4 uint per vertex (skinned meshes only)
    "bone_weights": [...],  // optional: 3 float per vertex; 4th is 1 - sum
    "indices":      [...],  // combined per-mesh index buffer
    "submeshes":    [ [ ...indices... ], ... ],  // one per material
    "aabb":         { "min": [...], "max": [...] },  // precomputed
    "joints":       [ ... ],  // optional: flat array; entries have name/local_matrix/offset_matrix/parent (index)
    "root_joint":   "..."     // optional: name of the root joint
}
```

**SceneNode** (the root + recursive children at `nodes`):

```jsonc
{
    "name": "tank_root",
    "local_position": [x, y, z],
    "local_rotation": [x, y, z, w],
    "local_scale":    [sx, sy, sz],
    "components": {
        "Transform":  { ... },
        "MeshRender": { "mesh": "T90", "materials": ["Material_Body", "Material_Wheel"] },
        "Light":      { ... },
        "Camera":     { ... }
    },
    "children": [ ...recursive SceneNode... ]
}
```

What's precomputed in this format that glTF doesn't natively carry:

- **Per-mesh AABB** — used by the octree for spatial culling. Recomputed at
  load time would be wasteful and break the static-octree assumption.
- **Submesh-per-material splits** — glTF has one primitive per material; the
  engine baked it into separate index buffers so the renderer can do one
  draw call per submesh without re-grouping.
- **Engine-shaped material uniforms** — `diffuse_color`, `transparency`,
  `shininess`, etc. The pipeline shaders bind these by name (see
  `Resource/Pipeline/DefferedLightingLambert.json`).
- **Texture sampler config** — `filter`, `wrap`, `srgb`, `mipmap` baked into
  each texture record so the runtime loader doesn't re-derive them.
- **LZ4 compression** — the `.bin` variant is the same JSON but
  LZ4-framed. 1.6 MB `.bin` vs 11 MB `.json` for the demo's tank scene.

## Future expansion

To add a new subcommand:

1. Add `static const char *kFooHelp = "...";` to `engine/Fury/Cli.cpp` with
   exhaustive help text matching this doc's tone.
2. Add `static int DoFoo(int argc, char **argv) { ... }` next to the other
   handlers. Return 0/1/2 per the exit-code convention.
3. Dispatch in `Cli::Run`: `if (sub == "foo") return DoFoo(argc, argv);`
4. Add `"foo"` to the `tokens[]` array in `Cli::LooksLikeSubcommand` so
   `examples/main.cpp`'s router knows to take the CLI path on
   `./fury foo ...`.
5. Add `if (topic == "foo") { std::cout << kFooHelp; return 0; }` to
   `DoHelp`.
6. Document the subcommand in this file (new `### fury foo` section).
7. Rebuild and confirm `./fury foo --help` and `./fury help foo` both work.

For follow-ups the existing scope deliberately excludes:

- **Scene → glTF export.** Reconstructing PBR metallic-roughness from the
  engine's Lambert shape is genuinely lossy; the conversion makes sense only
  once the HDR pipeline lands.
- **`scene.json ↔ scene.bin` direct conversion.** Trivial follow-up — same
  Serializable round-trip, just toggle the LZ4 envelope.
- **A `--dry-run` flag.** Defer until ergonomic motive emerges in testing.
- **Async / streaming progress.** FBX2glTF takes a few seconds on real
  models; v1 just blocks. A progress callback that emits ImGui toasts (for
  the runtime path) or `\r`-overwriting stderr lines (for the CLI path) is a
  reasonable follow-up.
