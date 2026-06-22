## Why

The engine is moving toward an "AI-native" asset workflow: text-described scenes go in, runnable scenes come out — and a tight loop for a human operator to drag a model in and see it. Today the only path into the engine is the custom Serializable JSON/LZ4 (`scene.json` / `scene.bin`) format. There is no path from glTF — the industry-standard, AI-friendly format that `tinygltf` was vendored to support — and no path from FBX since `_FURY_FBXPARSER_IMP_` was removed. The original plan (`replace-fbx-with-tinygltf` in ARCHITECTURE.md §15) only got as far as wiring tinygltf into the build; the actual `GltfImporter` never landed. A separate, smaller problem: skinned meshes silently don't round-trip through `Mesh::Save` (Mesh.cpp:120 `// TODO: no joints yet`), which would block any skinned glTF input.

This change closes those gaps by:
1. Adding a CLI surface to the `fury` binary that converts assets *offline* (glTF → scene.json/.bin, FBX → glTF → scene.json/.bin via the vendored `FBX2glTF-darwin-x64` binary at `engine/ThirdParty/FBX2glTF/`).
2. Binding the same glTF importer to Lua so `Demo.lua` (and any future script) can import assets *at runtime* from a File menu — "New Scene", "Open Scene", "Import (glTF / FBX / scene.json)". This turns the demo into a minimum-viable scene viewer / inspector.
3. Fixing the skin round-trip TODO as a prerequisite.

The architecture question raised before this change started — *should we keep scene.json / scene.bin given that we have glTF?* — was investigated. **Decision: keep it as the runtime form, treat glTF as the interchange form.** scene.json/.bin encodes engine-specific data that glTF doesn't natively carry (precomputed AABBs the octree depends on, the engine's named-uniform Lambert/Phong material schema, submesh-per-material splits, `cast_shadows` flags, LZ4 compression — 1.6 MB .bin vs 11 MB .json vs raw glTF). Replacing the runtime loader is materially larger work than writing an importer *and* would lose that precomputed engine-shaped data. The importer is the bridge: it can be invoked offline (CLI) or at runtime (Lua-bound), and produces the engine's optimized form.

**LDR-only is fine for now.** The engine currently ships a Lambert deferred pipeline (no PBR shader variant). Per the user's confirmation, HDR/PBR is a deliberately later step; until then, the importer's lossy PBR → Lambert mapping is the right shape. When the HDR pipeline lands, a follow-up change can add a PBR material variant alongside the existing Lambert one without re-architecting the importer.

## What Changes

### Asset ingestion — offline (CLI) and runtime (Lua-bound)

- **Add a CLI subcommand surface to the `fury` binary.** `examples/main.cpp` becomes a small router: if `argv[1]` matches a known subcommand (`convert`, `info`, `help`, `--help`, `-h`, `version`, `--version`), take the CLI path — no SFML window, no Lua VM, no `Engine::Initialize`. Otherwise, treat `argv[1]` as a Lua script path (current behavior, defaulting to `Demo.lua`). Single binary, no new CMake target, no new dependencies.
- **`fury convert gltf <input.gltf|.glb> <output.json|.bin>`** — load via tinygltf, walk `tinygltf::Model` into engine types, serialize via `FileUtil::SaveFile` or `SaveCompressedFile`. Output format inferred from extension.
- **`fury convert fbx <input.fbx> <output.gltf|.glb|.json|.bin>`** — invoke the vendored `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` binary as a subprocess to convert FBX → glTF first, then (if the output extension is `.json`/`.bin`) chain into the glTF importer to produce the engine's runtime form. If the output extension is `.gltf`/`.glb`, stop after the FBX2glTF step. Intermediate glTF files for the chained path land in a tempdir and are cleaned up on success.
- **`fury info <path>`** — print a single-asset summary (node count, mesh count split into static / skinned, submesh count, total triangle / vertex count, material count, animation clip count, joint count, scene-wide AABB). Accepts `.json` / `.bin` / `.gltf` / `.glb` / `.fbx` (FBX path converts to a temp glTF and inspects that).
- **`fury --help` / `fury <subcommand> --help`** — exhaustive, agent-friendly help text. Same content mirrored in `docs/CLI.md`.
- **Add `engine/Fury/GltfImporter.{h,cpp}`** — stateless class owning the tinygltf → engine-types translation. Returns a `Scene::Ptr`. CPU-only path; the same class is invoked by the CLI and by the runtime Lua binding. It does not upload to GPU on its own; runtime upload happens when the loaded scene is first rendered, via the existing `Texture::CreateFromImage` / VAO-upload paths.
- **Add `engine/Fury/FbxConverter.{h,cpp}`** — thin subprocess wrapper around the vendored `FBX2glTF` binary. Resolves the binary's path relative to the running executable's location (so it works whether `fury` is run from `examples/bin/` or installed). Returns the path to the produced `.gltf`/`.glb` file or an error.
- **Add `engine/Fury/Cli.{h,cpp}`** — argv parser, subcommand dispatch, help strings. Pure C++, no Lua, no SFML.
- **Fix Mesh.cpp:120 TODO** — extend `Mesh::Save` / `Mesh::Load` to round-trip `IDs` (4 bone indices/vertex), `Weights` (3 explicit floats/vertex with implicit `w = 1 - sum`), and the `m_JointMap` + `m_RootJoint` + `m_Joints` registry. Closes the long-standing gap in ARCHITECTURE.md §11.

### Lua surface — turn the demo into a minimum-viable scene editor

- **Add Lua bindings for the importer** so `Demo.lua` can call it without leaving the Lua VM:
  - `Importer.LoadGltf(path)` — returns a new `Scene` populated from a glTF file, or nil on error
  - `Importer.LoadFbx(path)` — same but first converts FBX → temp glTF via the `FbxConverter` subprocess wrapper, then loads it. Returns a new `Scene` or nil
  - `Importer.LoadScene(path)` — convenience wrapper that dispatches by extension (`.json` / `.bin` use the existing FileUtil loaders; `.gltf` / `.glb` use `LoadGltf`; `.fbx` uses `LoadFbx`)
- **Add a small `Importer.MergeInto(target_scene, source_scene)` helper** for "Import" (vs "Open Scene"): appends `source_scene`'s root children, materials, meshes, animations, and joints into `target_scene`'s `EntityManager`, then re-runs `m_SceneManager->AddSceneNodeRecursively(target_scene->GetRootNode())` to register newly-added subtrees with the octree.
- **Update `examples/Demo.lua` to use the bindings to provide a minimum scene-editor surface in its File menu**:
  - "New Scene" — clear the active scene (`Scene::Clear`-equivalent through bindings), keep the camera and pipeline alive
  - "Open Scene…" — pick from a curated list of files under `Resource/Scene/` (the menu enumerates the directory and shows each `.json` / `.bin` / `.gltf` / `.glb` / `.fbx` as a sub-menu item). On click: `Scene.Clear()`, then `Importer.LoadScene(path)`, then reattach the scene to the active pipeline.
  - "Import…" — same enumerator but applies `Importer.MergeInto` rather than replacing the scene
  - "Save Scene As…" — opens an ImGui modal with a text input pre-filled `scene_saved.json`; on confirm, writes to `Resource/Scene/<typed-name>` via `FileUtil.SaveFile` or `FileUtil.SaveCompressedFile` (chosen by extension). Adds a `Gui.InputText` Lua binding.
  - The previous static `FileUtil.LoadSceneFromCompressedFile(...)` call in `on_init` becomes the default initial "Open" target
- **Add a minimal `Gui` binding for directory enumeration** so the menu can list `Resource/Scene/`: `FileUtil.ListDirectory(path, extensions_filter)` returns a Lua array of relative filenames. This lives on `FileUtil` (not `Gui`) — it's a filesystem concern, and `FileUtil` already exposes path resolution.

### Material mapping and animation time-base

- **Material mapping (lossy in v1)**: glTF PBR metallic-roughness → engine Lambert. `baseColorFactor` → `diffuse_color`, `baseColorTexture` → `diffuse_texture`, `emissiveFactor` → `emissive_color`, `alphaMode` → `opaque`. `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture` are read but warn-and-ignored (one warning per material). HDR/PBR pipeline is a deliberately later step (user-confirmed); when it lands, a follow-up adds a PBR material variant alongside Lambert without re-architecting the importer.
- **Animation time-base (resample in v1)**: glTF stores keyframe times as float seconds; engine `AnimationClip` uses integer ticks at fixed 24 fps. Resample glTF samplers at 24 Hz; CUBICSPLINE → LINEAR with one warning per sampler. Switching the engine to seconds-based timing is a separate, larger decision.

### Documentation

- **Add `docs/CLI.md`** — same style as `docs/LUA.md`. Sections: dispatch shape (router in `main.cpp`), subcommand reference (full `--help`-equivalent per subcommand), the FBX → glTF → scene chain explanation, scene-format appendix (top-level keys, node tree shape, what's precomputed, the LZ4 envelope), known limitations / lossy mappings, "Future expansion" section.
- **Update `docs/LUA.md`** — add new sections for the `Importer` bindings (`LoadGltf` / `LoadFbx` / `LoadScene` / `MergeInto`) and the `FileUtil.ListDirectory` accessor; add a "Scene editor menu" sub-section under the existing `Gui` reference showing how `Demo.lua` builds the File menu, so future scripts can replicate the pattern.
- **Update `docs/ARCHITECTURE.md` §15** — record: (a) the originally-planned runtime `GltfImporter` is now both CLI-invoked and Lua-bound; (b) tinygltf is consumed at runtime *and* offline; (c) the FBX path is restored via a subprocess to FBX2glTF (no FBX SDK linked into the engine — see [Decision: FBX support via subprocess]); (d) the skinned-mesh round-trip TODO is fixed; (e) scene.json/.bin format is confirmed as the runtime form, with HDR/PBR materials called out as a known future addition.

## Capabilities

### New Capabilities

- `gltf-importer`: The CPU-only translator from `tinygltf::Model` to engine `Scene` / `SceneNode` / `Mesh` / `Material` / `Joint` / `AnimationClip`. Covers what gets mapped, the lossy choices (PBR → Lambert, seconds → ticks), and what's explicitly rejected (morph targets, sparse accessors, byte-stride buffer views). Used by both the CLI and the Lua bindings.
- `fbx-converter`: A thin subprocess wrapper around the vendored `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` binary. Covers how the binary is located, how it's invoked, how its stdout/stderr are captured, how temp files are managed, and the platform support story (macOS x64 only in v1; Apple Silicon runs via Rosetta).
- `cli`: The argv-driven entry point on the `fury` binary. Covers the dispatch contract (subcommand vs Lua-script path), the subcommand inventory (`convert`, `info`, `help`, `version`), the multi-step pipelines (`convert fbx` → FBX2glTF → glTF importer → SaveFile), exit-code conventions, and the help-text contract that `docs/CLI.md` mirrors.
- `scene-editor`: The runtime Lua surface that turns `Demo.lua` into a minimum-viable scene editor: importer bindings (`LoadGltf` / `LoadFbx` / `LoadScene` / `MergeInto`), scene-clearing semantics, the File-menu pattern (`New Scene` / `Open Scene` / `Import` / `Save Scene As`), and directory enumeration via `FileUtil.ListDirectory`. Covers the contract Demo.lua relies on so future scripts can replicate the editor pattern.

### Modified Capabilities

<!-- None — `gltf-loader` covers the build wiring (still accurate) and `lua-input` covers Lua input bindings. Neither's REQUIREMENTS change here. The new capabilities run *alongside* the existing surfaces. -->

## Impact

- **New files**:
  - `engine/Fury/GltfImporter.{h,cpp}` — tinygltf → engine-types translator (CPU-only).
  - `engine/Fury/FbxConverter.{h,cpp}` — subprocess wrapper around FBX2glTF.
  - `engine/Fury/Cli.{h,cpp}` — argv parser, subcommand dispatch, help strings.
  - `docs/CLI.md` — agent-facing CLI reference, mirrored in `--help` output.
- **Modified files**:
  - `examples/main.cpp` — top of `main()` dispatches to `fury::Cli::Run` when `argv[1]` is a known subcommand; otherwise falls through to the existing Lua launcher.
  - `engine/Fury/Mesh.{h,cpp}` — `Save` / `Load` write/read `IDs`, `Weights`, joints registry. Existing static-mesh files load unchanged; new keys are emitted only when present.
  - `engine/Fury/FileUtil.{h,cpp}` — add `ListDirectory(path, extensions)` helper.
  - `engine/Fury/LuaBindings.{h,cpp}` — add `Importer` table with `LoadGltf` / `LoadFbx` / `LoadScene` / `MergeInto`; add `FileUtil.ListDirectory`; add `Scene:Clear()` if not already bound.
  - `examples/Demo.lua` — File-menu enumeration, scene-clearing, import/merge flows.
  - `engine/CMakeLists.txt` — `file(GLOB)` should pick up the new `.cpp` files; verify. Also copy `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` to the build's output directory next to the `fury` binary at build time (so `FbxConverter` can find it via a path relative to the executable).
  - `docs/LUA.md` — Importer / ListDirectory / scene-editor menu sections.
  - `docs/ARCHITECTURE.md` §15 — record what landed.
- **Vendored binary**:
  - `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` (12 MB x86_64 Mach-O) is checked in. License (`LICENSE.txt`) and `README.md` from FBX2glTF accompany it.
- **Dependencies**: No new submodules. tinygltf, rapidjson, LZ4, sol2, Lua, SFML — all already vendored. FBX2glTF is a prebuilt binary, not a library — we don't link it, we exec it.
- **Platform support**: macOS x86_64 (native) and macOS arm64 (via Rosetta) in v1. Linux and Windows FBX support requires a corresponding FBX2glTF binary which is not in the tree; the `fbx-converter` capability documents this as a v1 limitation and the CLI emits a clear error when invoked on an unsupported platform.
- **Runtime behavior for non-CLI users**: Unchanged for `./fury` and `./fury Demo.lua`. The CLI path is only entered when `argv[1]` matches a subcommand. Demo.lua gains File-menu features; everything else (camera, rendering, profiler) works the same.
- **Skin-data round-trip on disk**: scene.json files produced by this change include new keys (`bone_ids`, `bone_weights`, `joints`, `root_joint`) on Mesh records that have skin data. Existing static-mesh scene files remain readable unchanged. New static-mesh scene files remain byte-identical to today's output (the new keys are emitted only when the mesh has skin data).
- **Out of scope**:
  - Scene → glTF / FBX export. PBR reconstruction is genuinely lossy in the other direction; separate change.
  - A PBR material shape / HDR pipeline. Confirmed by the user as a later step. The lossy PBR → Lambert mapping in v1 is sufficient until the HDR pipeline lands.
  - Switching `AnimationClip` to seconds-based timing.
  - glTF morph targets, sparse accessors, byte-stride buffer views. Importer rejects with a clear error.
  - A real ImGui file-open dialog. Demo's File menu enumerates `Resource/Scene/` and shows files as sub-menu items — sufficient to prove the round-trip and ergonomic enough for an AI agent to inspect. A real file dialog would require either an ImGui add-on (`imgui-filebrowser`) or SFML native dialogs; defer.
  - Cross-platform FBX support. Only macOS in v1, matching the prebuilt binary in the tree.
  - GPU-side validation of converted scenes inside the CLI. The `info` subcommand reads CPU-side counts only.
- **Verification plan**:
  - CLI: convert each FBX in `examples/bin/Resource/Scene/` (`james.fbx`, `outdoor.fbx`, `tank.fbx`) via `fury convert fbx <in> <out.json>` and via `fury convert fbx <in> <out.bin>`. Verify exit 0, files exist, `fury info` counts are sensible. Load each into Demo.lua and confirm it renders without errors in `Log.txt`.
  - CLI: convert at least one tinygltf sample model (`Box.gltf`, `Triangle.gltf`, `RiggedSimple.gltf`) via `fury convert gltf` and verify round-trip.
  - Runtime/editor: launch Demo.lua, exercise the File menu — `New Scene`, `Open Scene → scene.bin`, `Open Scene → james.fbx`, `Import → tank.fbx`, `Save Scene As`. Verify each works without crash, geometry shows, no `EROR` lines in `Log.txt`.
  - Skinned glTF round-trip: `fury convert gltf RiggedSimple.gltf rig.json`, then load `rig.json` in Demo.lua, confirm joint count via `fury info` matches the source.
  - Regression: `./fury` and `./fury Demo.lua` still launch the same demo behavior (camera flythrough, profiler).
