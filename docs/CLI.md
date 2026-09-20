# Fury3D — CLI

> Status (2026-09-18): three binaries ship from this build. `fury` is the
> player/launcher: it hosts the Lua runtime path, boots packaged `.pak`
> games, and carries an offline asset CLI. `furye` is the editor GUI.
> `furye-cli` is the headless editor-side CLI: same dispatch and exit-code
> conventions as `fury`, plus `cook` / `package` / `exec-script` for the
> cook-package-pak pipeline. AI agents and authoring tools can drive asset
> workflows through either CLI without ever touching a window.

## How it works

All three binaries are built from one `main()` (`examples/main.cpp`) and
share the same router; the binary decides what `argv[1]` may do.

`fury` inspects `argv[1]` before any window or engine initialization:

1. If `argv[1]` is a known CLI subcommand token (`convert`, `info`,
   `exec`, `help`, `--help`, `-h`, `version`, `--version`),
   `main()` dispatches to `fury::Cli::Run`. The CLI path is **pure C++
   asset workflows**: no SFML window opens, `Engine::Initialize` is never
   called, no Lua VM is created on the `convert` / `info` paths. The
   `exec` path is a special case - it creates a short-lived `sol::state`
   and runs a Lua script against a loaded scene, but it still does NOT
   open a window, NOT call `Engine::Initialize`, and NOT create an
   OpenGL context. See the `fury exec` section below.
2. If `argv[1]` names an existing `.pak` file, `main()` mounts the pak
   and boots its boot scene (a windowed engine boot, not a CLI
   dispatch). See "Playing a pak" below.
3. Otherwise, `argv[1]` is treated as a Lua script path (current
   behavior; defaults to `Editor.lua` if no arg). See `docs/LUA.md` for
   that surface. Any remaining `argv[2..]` is forwarded to the script as
   a standard Lua `arg` table - `Editor.lua` honors `arg[1]` as an
   optional startup scene (e.g. `./fury Editor.lua outdoor.fbx`). See
   LUA.md for the convention. Two **runtime flags** (`--screenshot`,
   `--screenshot-frame`) are recognized on the launcher path and
   stripped from `arg` before the script sees it. See "Screenshot mode"
   below.

On the launcher path, a loose scene argument (`argv[1]` or `argv[2]`,
`.json` / `.bin`) with a sibling `<scene>.pak` auto-mounts that pak, so
asset reads resolve from it without extra arguments.

`furye` (the editor GUI) takes the launcher path into `Editor.lua` and
the editor UI; it never mounts paks - the editor works on loose files
and spawns a `fury` child process for play sessions. Known subcommand
tokens route to `Cli::Run` just the same, so batch conversions work from
the editor binary too.

`furye-cli` (headless editor tooling, built with `FURY_HEADLESS_CLI=1`)
never takes the launcher path: it routes everything through `Cli::Run`,
adding `cook`, `package`, and `exec-script` to the token list. No-args
prints the top-level help and exits 0; unknown `argv[1]` exits 1 with an
error. See "furye-cli" below.

This means a CLI invocation like `./fury convert gltf in.gltf out.json`
(or `./furye-cli cook scene.bin`) is fast and predictable: no graphics
state, no UI, no script VM. The CLI is exactly what an AI agent or build
system wants.

## Binaries

| Binary      | Role                                                                                                              | Headless |
|-------------|-------------------------------------------------------------------------------------------------------------------|----------|
| `fury`      | Player/launcher: Lua scripts, pak boot, gameplay; hosts the offline asset CLI                                     | per subcommand |
| `furye`     | Editor GUI: `Editor.lua` shell + editor UI; works on loose files; play mode spawns a `fury` child                 | no |
| `furye-cli` | Headless editor tooling: the cook/package pipeline and editor-script batch runs over the same editor-side sources | yes (except `render-mesh`'s short-lived hidden window) |

## Playing a pak

`fury` plays packaged scenes in two shapes, plus a headless third:

```
fury game.pak                 # mount, load the boot scene, run it
fury scene.bin                # sibling scene.pak auto-mounts
fury exec game.pak test.lua   # headless: script runs against the boot scene
```

- `fury <game.pak>` mounts the pak, reads the boot entry from the index,
  and loads that scene with `Player.lua` as the entry script. The script
  is read through the asset backend, so a packed `Player.lua` wins over
  a loose one; when the pak carries none, the loose `Player.lua` in the
  working directory is used.
- `fury <scene.json|.bin>` checks for a sibling `<scene>.pak`
  (`argv[1]`, else `argv[2]`) and mounts it when present, so a deployed
  folder of `scene.bin` + `scene.pak` plays without extra arguments. No
  sibling pak: assets load from the working directory exactly as before.
- `fury exec <scene.pak> <script.lua>` mounts the pak headlessly and runs
  the script against the boot scene (same 0/1/2 exit codes as `fury exec`
  on a loose scene).

**Asset paths.** Pak keys are the same canonical, working-directory-relative
paths the scene already references; the scene file inside the pak is the
packaged file byte-for-byte. Cooked textures are stored under their
original source paths too - the texture loader sniffs content (KTX2
magic), not the filename extension - so scenes need no modification to
play from a pak. The engine `Resource/` folder still ships loose next to
the binary for engine defaults the pak does not carry.

**Legacy GL caveat.** A pak cooked with `--texture-target modern` (BC7
payloads) loaded on a GL context without BPTC support (macOS caps at GL
4.1) logs a per-texture "compressed format not supported" error and
renders that texture as the missing-texture placeholder; the scene
otherwise loads. Cook with `--texture-target legacy` (the macOS default)
to avoid this.

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
fury convert gltf  <input.gltf|.glb> <output.json|.bin>
fury convert fbx   <input.fbx>       <output.gltf|.glb|.json|.bin>
fury convert scene <input.json|.bin> <output.json|.bin>
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
- `scene` - engine scene -> engine scene: `.bin` <-> `.json` round-trip
  (same Serializable data, LZ4 envelope toggled).

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
- `.pak` — mounts the pak headlessly and loads its boot scene; asset
  reads resolve from the pak.

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

### `furye-cli kraut` - generate and import Kraut trees

```
furye-cli kraut generate <descriptor.tree> [--seed N] [--out dir]
furye-cli kraut import   <tree.glb> [output.json|.bin]
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
./furye-cli kraut generate engine/ThirdParty/Kraut/Data/Content/Trees/PalmTree2.tree --seed 7 --out /tmp/palm
./furye-cli kraut import /tmp/palm/PalmTree2.glb /tmp/palm/PalmTree2.bin
```

Exit codes follow the global table; the tool's load failure (missing
descriptor) maps to 1, generation/export failures to 2.

### `furye-cli render-mesh` - render a single mesh to a PNG

```
furye-cli render-mesh <scene> <mesh_name> <output.png> [--lod N]
```

Renders one mesh from a scene to a 256x256 PNG through the same camera +
shader as the editor's asset thumbnails (`RenderMeshLambert`). This path
NEEDS a GL context: `main()` opens a short-lived hidden window,
initializes the engine, renders, and exits - it is not part of the
headless CLI and won't run on a display-less host. `--lod N` renders a
generated LOD instead of the base mesh (default 0). Available on `fury`
and `furye-cli` alike. Exit codes follow the global table (1 on bad
args, missing scene, or missing mesh).

```
./fury render-mesh Resource/Scene/scene.json T90 /tmp/t90.png
./fury render-mesh Resource/Scene/scene.json T90 /tmp/t90_lod1.png --lod 1
```

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

## furye-cli - headless editor-side CLI

`furye-cli` is the third binary, built from the editor sources with
`FURY_HEADLESS_CLI=1`. It reuses `fury`'s CLI dispatch and exit-code
conventions and adds the cook/package pipeline plus editor-script
execution. It never falls through to the Lua launcher: no-args prints
the top-level help and exits 0; unknown `argv[1]` exits 1 with an error.

Subcommand inventory: `cook`, `package`, `convert`, `info`, `exec`,
`exec-script`, `kraut`, `render-mesh`, `help`, `version`. `convert`,
`info`, `exec`, `kraut`, and `version` behave exactly as on `fury` (see
the sections above); `exec` additionally accepts a `.pak` scene (mount,
load the boot entry). `render-mesh` is the exception to "headless": it
needs a GL context and opens a short-lived hidden window.

### `furye-cli cook` - cook a scene's textures to BCn KTX2

```
furye-cli cook <scene.json|.bin> [--texture-target legacy|modern]
                 [--ddc <path>] [--ktx <path>] [--manifest <path>] [--verbose]
```

**Options:**

- `--texture-target legacy|modern` - BC format set (default: `legacy`
  on macOS, `modern` elsewhere). An explicit value always overrides the
  host default.
- `--ddc <path>` - DDC root (default: `FURY_DDC` env, else `<exe
  dir>/DDC`).
- `--ktx <path>` - ktx CLI binary (default: `FURY_KTX_CLI` env, else
  `<exe dir>/ktx`; see "KTX-Software tooling" below).
- `--manifest <path>` - manifest output (default:
  `<scene-stem>.cookmanifest.json`).
- `--verbose` / `-v` - echo each ktx invocation.

**What it does.** Loads the scene headlessly and enumerates every
referenced asset: unique canonical texture paths from the scene's
texture registry - plus terrain splat/layer textures, which bind at
render-setup time and never appear in the headless registry, so cook
collects them from the node tree - and non-texture file assets as
passthrough entries. Referenced files missing on disk are named per path
and fail the cook with exit 1; nothing is silently skipped.

**Format mapping.** Texture usage derives from the texture's cook
settings (sRGB flag, normal-map binding, HDR source format):

| Texture usage   | `legacy` target | `modern` target |
|-----------------|-----------------|-----------------|
| Color, no alpha | BC1             | BC7             |
| Color + alpha   | BC3 (DXT5)      | BC7             |
| Normal map      | BC5             | BC5             |
| HDR             | uncompressed    | uncompressed    |

Default target is `legacy` on macOS (GL 4.1 has no BPTC support) and
`modern` on other hosts. HDR sources pass through uncompressed: the
pinned ktx 4.4.2 CLI has no uastc-hdr codec, so BC6H lands when the
toolchain gains one.

**Compression pipeline.** Each texture needing compression runs the
vendored `ktx` CLI in three steps: `ktx create --generate-mipmap` (->
KTX2 with a full mip chain), `ktx encode --codec uastc` (`--normal-mode`
for normal maps), `ktx transcode --target <bcN>`. The artifact is a
plain KTX2 with an unsupercompressed BCn payload. TGA/BMP sources are
decoded and re-encoded to PNG first (`ktx create` ingests png/jpg/exr/
hdr/ktx only). A failure at any stage fails the cook with the failing
command and the log tail quoted in the error.

**Overrides.** An optional `<scene-stem>.cook.json` next to the scene
maps canonical texture path -> usage and overrides the inferred usage:

```json
{
    "TerrainIsland/rock.png":  { "usage": "normal" },
    "TerrainIsland/splat.png": { "usage": "color" }
}
```

Usage strings: `color`, `color_alpha`, `normal`, `hdr`.

**DDC.** Cooked textures cache in a Derived Data Cache so recooks are
incremental. The key is a SHA-1 over the source file's content hash,
the cook settings (target, format, sRGB), and the ktx tool version -
a source edit, a settings change, or a `FURY_KTX_VERSION` bump changes
the key and recooks that texture only. Entries live in hash-prefix
subfolders: `DDC/<hex[0:2]>/<hex[2:4]>/<full-hash>.ktx2`, so no folder
holds a large number of files. On a hit the cached KTX2 is reused and
no ktx process runs; a miss compresses and then stores the entry.

**Manifest.** On success cook writes `<scene-stem>.cookmanifest.json`
(or `--manifest <path>`): the cook target and tool version, a
`textures` array mapping each canonical path to its DDC file, BC format,
dimensions, mip count, and sRGB flag, a `passthrough` array mapping
canonical paths to source files (with a reason), and an `unresolved`
array. `furye-cli package` consumes this manifest instead of
rediscovering assets.

**Exit codes:**

- `0` - cook succeeded (textures may be all DDC hits).
- `1` - bad args, scene unloadable, unresolved assets, ktx tool missing
  or failing.
- `2` - internal error.

```bash
# First cook: every texture compresses (BC1/BC3/BC5 on macOS).
./furye-cli cook Projects/ocean/ocean_island.bin

# Second cook: all DDC hits, zero ktx invocations.
./furye-cli cook Projects/ocean/ocean_island.bin --verbose

# Force the modern set on macOS (the pak then needs BPTC to render).
./furye-cli cook Projects/ocean/ocean_island.bin --texture-target modern
```

### `furye-cli package` - cook + pack a scene into a deployable .pak

```
furye-cli package <scene.json|.bin> [--compression none|lz4]
                    [--output <path>] [--no-cook] [cook passthrough opts]
```

**Options:** `--compression none|lz4` (default `lz4`), `--output <path>`
(default `<scene-stem>.pak` next to the scene), `--no-cook` (reuse the
existing manifest; requires one to be present), plus the cook
passthrough options (`--texture-target`, `--ddc`, `--ktx`, `--verbose`).

**Flow.** Runs `cook` first - incremental through the DDC, so a fresh
manifest normally costs no ktx invocations - then packs per the
manifest into the output pak:

- every cooked texture, keyed by its canonical source path, payload read
  from the DDC entry the manifest names;
- every passthrough asset by source file: heightmaps plus their `.json`
  sidecar, ocean wave json plus the baked payload files it references,
  and other external file assets;
- the scene file itself, stored byte-for-byte as the pak's **boot
  entry**, keyed by its canonical path;
- `Player.lua` when a loose one exists in the working directory, so
  `fury <game.pak>` runs it from the pak.

**Compression.** `lz4` stores each entry as LZ4 in 64 KB blocks; an entry
whose compressed blocks total >= its source size is stored raw instead
(the index records what actually happened). `none` stores everything
uncompressed for maximum read speed. See "Pak format v1" below.

**Failure mode.** A manifest entry whose DDC file or source file is
missing fails the package with exit 1, naming both the artifact path and
the asset key it belongs to. No partial pak is left behind: an
unfinished output file is removed on failure.

**Exit codes:**

- `0` - pak written (`wrote <path>` on stdout).
- `1` - bad args, cook failure, missing manifest, missing manifest
  artifact.
- `2` - internal error.

```bash
./furye-cli package Projects/ocean/ocean_island.bin
./furye-cli package Projects/ocean/ocean_island.bin --compression none --output /tmp/ocean-dev.pak
./fury /tmp/ocean-dev.pak
```

### `furye-cli exec-script` - run an editor Lua script headlessly

```
furye-cli exec-script <script.lua> [args...]
```

Runs a Lua script with the full engine + editor binding surface
registered (`Editor.*` included — furye-cli is built from the editor
sources) - without loading a scene, opening a window, or entering the
`Editor.lua` shell. This is the batch entry point for editor-side
tooling that today runs inside `furye`. Trailing args land in the
standard Lua `arg` table (`arg[0]` = script path, `arg[1..]` forwarded
verbatim, no flag parsing). Load/runtime errors exit 1 with the Lua
message; an escaped C++ exception exits 2.

```bash
./furye-cli exec-script my_tool.lua Projects/ocean/ocean_island.bin
```

## Pak format v1

A `.pak` is one self-contained file. Layout: a data region of
sequentially stored entry payloads, an index region, and a fixed-size
footer at end of file. Readers locate the index via the footer (never
by scanning).

**Footer (fixed size):** magic `FURYPAK1` (8 bytes), format version
`u32` (currently 1), index offset `u64`, index size `u64`, SHA-1 of the
index bytes (20 bytes), reserved `u32`. Mounting validates magic,
version, and index hash up front: a bad magic, an unsupported version,
or a tampered index fails with an error naming the problem - no crash,
no partial mount.

**Index:** entries in sorted (byte-wise) path order, keys normalized to
forward slashes, so pack/load lookups are deterministic. Per entry: data
offset, uncompressed size, codec (`none` or `lz4`), compression block
size, per-block compressed sizes, and a SHA-1 of the uncompressed
payload. The index also records the boot entry's path. Block-level
compression means a reader can decompress a single 64 KB slice without
touching the rest of the entry.

**Reads.** A lookup miss fails exactly like a missing loose file (same
error surface to callers). LZ4 entries decompress to their recorded
uncompressed size; the returned bytes are verified against the entry's
SHA-1, so on-disk corruption is detected with an error naming the entry.

## Exit codes

Both `fury` and `furye-cli` follow this table.

| Code | Meaning                                                         |
|------|-----------------------------------------------------------------|
| 0    | Success                                                         |
| 1    | User error — bad arguments, unsupported input, file not found, glTF feature rejected, unresolved cook asset, missing manifest artifact |
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

## KTX-Software tooling

`furye-cli cook` compresses textures through the vendored `ktx` CLI
(pinned KTX-Software 4.4.2, committed under the third-party tree per
platform). Layout:

```
engine/ThirdParty/KTX-Software/
+-- 4.4.2/
|   `-- mac-arm64/
|       +-- ktx             # the CLI
|       `-- libktx.4.dylib  # its runtime library (rpath @executable_path)
`-- README.md
```

One platform dir per supported host; payloads are a few MB and committed
as-is (no git LFS). CMake's `POST_BUILD` step copies the host
platform's tool files next to the built binaries, and cook resolves the
tool in this order:

1. `--ktx <path>` (the cook/package flag)
2. `FURY_KTX_CLI` env
3. `<exe dir>/ktx` (the staged vendored binary)

A missing tool fails the cook with an error naming the expected path and
the override knobs. The pinned version string (`FURY_KTX_VERSION` in
`engine/CMakeLists.txt`) feeds the DDC keys, so bumping the pin recooks
every texture on the next run.

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
   `./fury foo ...`. (Editor-only tokens such as `cook` / `package` /
   `exec-script` also sit behind the `FURY_HEADLESS_CLI` gate so `fury`
   and `furye` never see them.)
5. Add `if (topic == "foo") { std::cout << kFooHelp; return 0; }` to
   `DoHelp`.
6. Document the subcommand in this file (new `### fury foo` section).
7. Rebuild and confirm `./fury foo --help` and `./fury help foo` both work.

For follow-ups the existing scope deliberately excludes:

- **Scene → glTF export.** Reconstructing PBR metallic-roughness from the
  engine's Lambert shape is genuinely lossy; the conversion makes sense only
  once the HDR pipeline lands.
- **BC6H for HDR cook sources.** Blocked on the pinned ktx 4.4.2 CLI,
  which has no uastc-hdr codec; the cook mapping gains BC6H-unsigned
  when the toolchain does.
- **A `--dry-run` flag.** Defer until ergonomic motive emerges in testing.
- **Async / streaming progress.** FBX2glTF takes a few seconds on real
  models; v1 just blocks. A progress callback that emits ImGui toasts (for
  the runtime path) or `\r`-overwriting stderr lines (for the CLI path) is a
  reasonable follow-up.
