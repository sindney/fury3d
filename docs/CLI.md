# Fury3D — CLI

> Status (2026-06-22): `fury` is a single binary that hosts both the Lua
> launcher (runtime path) and an offline asset CLI. AI agents and authoring
> tools can drive asset workflows through the CLI without ever touching the
> Lua runtime.

## How it works

The `fury` binary's `main()` inspects `argv[1]` before any window or engine
initialization:

- If `argv[1]` is a known CLI subcommand token (`convert`, `info`, `help`,
  `--help`, `-h`, `version`, `--version`), `main()` dispatches to
  `fury::Cli::Run`. The CLI path is **pure C++ asset workflows**: no SFML
  window opens, `Engine::Initialize` is never called, no Lua VM is created.
- Otherwise, `argv[1]` is treated as a Lua script path (current behavior;
  defaults to `Demo.lua` if no arg). See `docs/LUA.md` for that surface.

This means a CLI invocation like `./fury convert gltf in.gltf out.json` is
fast and predictable: no graphics state, no UI, no script VM. The CLI is
exactly what an AI agent or build system wants.

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

**Examples:**

```bash
# Static glTF → engine .bin (LZ4-compressed)
./fury convert gltf model.gltf scene.bin

# Binary glTF → human-readable engine .json
./fury convert gltf character.glb character.json

# FBX → engine .bin (chained via FBX2glTF)
./fury convert fbx examples/bin/Resource/Scene/james.fbx /tmp/james.bin

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
aabb:           min=(x, y, z) max=(x, y, z)
```

(`aabb` is computed for engine-format inputs by walking the loaded SceneNode
tree with world transforms. For glTF inputs it prints "(not computed for
glTF inputs in v1)" — comparing node/mesh/joint counts is enough for the
round-trip-verification use case.)

`info` is the agent-friendly inspection tool. Pair it with `convert` to
verify that a glTF input and its converted engine output have matching
counts (modulo expected differences from PBR-discarded fields).

### `fury help` — print help

```
fury help                    # same as fury --help / fury -h: top-level help
fury help <subcommand>       # detailed help for one subcommand
fury convert --help          # equivalent to "fury help convert"
fury info --help             # equivalent to "fury help info"
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
