## Context

The engine currently has two ingest paths on paper: the runtime `Serializable` JSON/LZ4 loader (`FileUtil::LoadFile` / `LoadCompressedFile` against `scene.json` / `scene.bin`) and a stub glTF DOM (`GLTFDom`) that never got promoted to an importer. In practice only the runtime loader works — `GLTFDom` is gone from the tree (see ARCHITECTURE §15 "What's landed"), and `_FURY_FBXPARSER_IMP_` was removed during the FBX SDK drop. So today the engine can only consume hand-authored or previously-converted scene files; there is no way to bring a new asset in.

The held `replace-fbx-with-tinygltf` change planned to wire a runtime `GltfImporter` that an engine application would call directly. That plan made sense when the engine was its own application; with the sol2 + Lua direction (commit `286515f`) and the AI-native goal, *both* surfaces are now useful: an **offline converter** for build/dev workflows and AI-driven asset pipelines, and a **runtime importer** so an interactive script can drag a file in and see it. This change builds both around the same `GltfImporter` class.

Two pieces of the FBX story have settled since the previous design pass:
- The FBX SDK is **not** coming back. Linking it would re-introduce the Autodesk EULA + binary blob + SCons-driven build that motivated dropping it in the first place.
- A vendored `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` binary (Facebook Incubator's tool) provides a path from FBX → glTF without linking the SDK. We invoke it as a subprocess. This keeps the engine FBX-SDK-clean and lets us reuse the glTF importer for both formats.

The Mesh serialization gap (Mesh.cpp:120 `// TODO: no joints yet`) is the load-bearing prerequisite: without fixing it, skinned glTF inputs would convert but not round-trip — they'd lose joints + per-vertex skin data on save, and the resulting scene file would render as a t-pose with garbage skinning. Fixing the TODO is in scope here because skinned-glTF round-trip is part of v1.

Constraints already established by adjacent changes:
- `tinygltf v2.9.7` is vendored at `engine/ThirdParty/tinygltf/` and compiled into `fury` with `TINYGLTF_NO_STB_IMAGE` to avoid stb ODR collision (`gltf-loader` capability).
- `rapidjson v1.1.0` and `LZ4` are vendored; the engine uses `Serializable` against `rapidjson::Value` (via `void*`) for JSON shape and LZ4 for the `.bin` envelope.
- The engine ships a **Lambert deferred pipeline only** as of v1. HDR/PBR is a deliberately later step (user-confirmed). Until then, the importer's PBR → Lambert mapping is the right shape.
- The engine's `AnimationClip` is frame-tick-shaped at a fixed 24 fps (FBX heritage), with rotations stored as Euler radians (YXZ order, per `MathUtil`).
- `Joint`, `SubMesh`, `Material`, `Mesh` all already serialize via `Entity::Load/Save` and the rapidjson `void*` opacity. The missing piece is the per-vertex skin data + the mesh→joints registry on `Mesh`.
- `Gui` Lua bindings already cover `BeginMenu` / `MenuItem` / `EndMenu` / `SetMenuBarCallback` (see `engine/Fury/LuaBindings.cpp:256-260`). The File menu in Demo.lua composes from these primitives.

## Goals / Non-Goals

**Goals:**

- Provide a stable, exit-code-driven CLI surface on `fury` that AI agents can use to discover capabilities (`--help`) and convert assets (`convert gltf`, `convert fbx`).
- Translate glTF 2.0 scene graphs (nodes, meshes, submeshes, materials, joints, skin data, animation clips) into the engine's runtime form.
- Restore FBX support via a subprocess to the vendored FBX2glTF binary — no FBX SDK linked into the engine.
- Bind the same glTF importer to Lua so `Demo.lua` (and any future script) can import assets at runtime from a File menu, turning the demo into a minimum-viable scene viewer/editor.
- Make skinned-mesh inputs round-trip cleanly: glTF → scene.json → engine load → render with correct skinning.
- Preserve current Lua-launcher behavior bit-for-bit: anyone running `./fury` or `./fury Demo.lua` sees zero regression in the camera/render/profiler paths.
- Keep the CLI path zero-cost to non-CLI users: no engine boot, no window, no Lua VM on the CLI path.
- Ship docs (`docs/CLI.md`, additions to `docs/LUA.md`) good enough that an AI agent reading them alone can use both the CLI and the runtime importer without source access.

**Non-Goals:**

- Scene → glTF / FBX export. PBR reconstruction from Lambert + seconds-from-ticks conversion + extension authoring is a separate effort.
- A new PBR shader variant or HDR material shape. User-confirmed as a deferred follow-up. Lossy PBR → Lambert in v1.
- Switching `AnimationClip` from ticks to seconds.
- A real ImGui file-open dialog. The Demo's File menu enumerates `Resource/Scene/` and shows files as sub-menu items — sufficient to prove the round-trip and ergonomic enough for both human and agent use. A real dialog (`imgui-filebrowser` or SFML native dialogs) is a separate concern.
- Cross-platform FBX support. Only the macOS x86_64 binary is in the tree; Linux/Windows would need their own FBX2glTF binaries which are not vendored.
- Visual / GPU-side verification inside `info`. CPU counts only.
- Texture transcoding / image format conversion. The converter passes image URIs through; if a glTF has embedded image data we extract to a file alongside the output. KTX2/BasisU is out of scope.
- Pipeline (JSON) generation. The converter emits scene data; the user keeps the pipeline they had.
- Linking the FBX SDK. The whole point of the FBX2glTF subprocess approach is to keep the SDK out of the engine.

## Decisions

### Decision: Single binary with subcommand dispatch in `examples/main.cpp`

`main()` inspects `argv[1]` and routes:
- If `argv[1] ∈ {"convert", "info", "help", "--help", "-h", "version", "--version"}` → `return fury::Cli::Run(argc, argv);`
- Otherwise → existing Lua launcher path (SFML window → `Engine::Initialize` → `sol::state` → `safe_script_file(argv[1] ?? "Demo.lua")`).

Both paths share the same `fury` binary. The CLI path links the engine's CPU-side code but never touches GL or SFML at runtime; classes like `Shader`, `Pass`, `Texture::UpdateBuffer`, `BufferManager` are linked (we don't strip them) but their GL-touching methods are never called from the CLI code path. `Texture::CreateFromImage` would be problematic (it does GL upload), so the importer creates `Texture` records via the same Serializable-shaped path as `Material::Save` writes — name/path/sampling-config only; no `CreateFromImage` call. The runtime path (Demo.lua loading the converted scene) hits the GL upload as normal when textures are first sampled.

**Alternatives considered:**
- *Separate `fury-cli` (or `furyc`) binary.* Cleaner ABI boundary but doubles install surface, requires a new CMake target, and gives no real benefit — the converter would link the same engine code anyway. Rejected.
- *Lua-scriptable conversion.* Expose tinygltf bindings to Lua and ship `convert.lua`. Pays SFML+Lua startup on every invocation; `--help` becomes a Lua print, awkward for agent discovery. Rejected.
- *Use a library like CLI11 or cxxopts for argparse.* Tempting but the v1 surface is tiny (3 subcommands, ~5 flags total). Hand-rolling ~80 lines of argv parsing in `Cli.cpp` avoids a new submodule and keeps the help text fully in our hands (important for agent discoverability). Revisit if the CLI surface grows past ~6 subcommands.

### Decision: New `engine/Fury/GltfImporter.{h,cpp}` as a stateless translator, callable from both the CLI and Lua

Public surface:

```cpp
namespace fury {
    class GltfImporter {
    public:
        struct Options {
            float anim_ticks_per_second = 24.0f;   // engine convention
            bool  reject_unsupported    = true;    // morph targets, sparse, etc.
        };
        static std::shared_ptr<Scene> Import(
            const std::string &path,
            const std::string &scene_name,
            const std::string &working_dir,
            const Options &opts = {});
    };
}
```

Internals: `tinygltf::TinyGLTF::LoadASCIIFromFile` or `LoadBinaryFromFile` by extension; one pass to populate engine `Material` and `Texture` records; one pass per `tinygltf::Mesh` to emit `Mesh` + `SubMesh` records (compute or read AABBs; copy positions/normals/tangents/UVs/indices; copy `JOINTS_0` → `IDs` and `WEIGHTS_0` → `Weights` for skinned primitives); one pass per `tinygltf::Skin` to emit a `Joint` tree and wire `m_OffsetMatrix` from `inverseBindMatrices`; one pass to walk `tinygltf::Scene::nodes` and emit the `SceneNode` tree with `Transform` and `MeshRender` components; one pass per `tinygltf::Animation` to resample channels at 24 fps into `AnimationClip` records.

**Why stateless static**: the converter is invoked once per CLI call (or once per Lua call), has no lifetime to manage, and shouldn't accidentally be reused with state from a previous import. Matches the shape of `MeshUtil` / `AnimationUtil`.

**Why not a constructor + instance**: would imply incremental import or reusable state — neither is true. A free function would also work; we go with a class for namespacing the `Options` struct.

**CLI vs runtime context**: The same `GltfImporter::Import` is called from `Cli::DoConvert` and from the new `Importer.LoadGltf` Lua binding. In the CLI path the returned `Scene` is immediately serialized via `FileUtil::SaveFile` and discarded; no GL context exists, no GPU upload happens. In the runtime path the returned `Scene` is handed to Lua and rendered live, and GPU upload happens lazily on first sample (existing `Texture::CreateFromImage` path, triggered when the renderer first binds the texture). The importer itself does not call any GL function in either case.

### Decision: FBX support via subprocess to the vendored `FBX2glTF` binary, not via the FBX SDK

The engine deliberately does not link the FBX SDK (dropped in the FBX → tinygltf change). To restore FBX ingest without re-introducing the SDK, we invoke `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` as a subprocess. The CLI handler and the runtime Lua path both chain through the same `FbxConverter` class.

Public surface:

```cpp
namespace fury {
    class FbxConverter {
    public:
        struct Result {
            std::string output_path;    // path to produced .glb (or empty on error)
            std::string stdout_capture;
            std::string stderr_capture;
            int exit_code = -1;
            bool ok() const { return exit_code == 0 && !output_path.empty(); }
        };
        // input_path: absolute or cwd-relative FBX path.
        // output_dir: directory where FBX2glTF writes its output (tempdir for chained CLI calls).
        static Result Convert(const std::string &input_path, const std::string &output_dir);
        // Returns the resolved absolute path to the FBX2glTF binary, or empty if not found.
        static std::string LocateBinary();
    };
}
```

**Binary location**: resolved at runtime from the path to the running `fury` executable (`_NSGetExecutablePath` on macOS), not from `getcwd()`. The build system copies `FBX2glTF-darwin-x64` next to the built `fury` binary (CMake `add_custom_command(TARGET fury POST_BUILD ...)`). At runtime `LocateBinary()` walks up from `argv[0]` (resolved to absolute) looking for `FBX2glTF-darwin-x64` next to it, then falls back to `engine/ThirdParty/FBX2glTF/` relative to the engine source root if that's discoverable. The exact lookup path is documented in `docs/CLI.md`.

**Subprocess invocation**: `posix_spawn` with explicit `argv` and pipes for stdout/stderr capture. We block-wait on the child (`waitpid`) — FBX2glTF runs to completion in seconds for the assets in our tree (12 MB tank model takes ~2s on M1). Streaming progress is out of scope; we'll surface a "Converting FBX..." log line at the start and the captured stdout at the end.

**Why subprocess vs library**: the FBX SDK is what we'd link to write our own importer, and we already rejected that. Adopting `ufbx` (a permissively-licensed FBX library) would be cleaner long-term, but is a bigger change with its own design tradeoffs. The subprocess approach is the cheapest way to bring FBX back today; it keeps the FBX dependency strictly out-of-process and per-author-tool. If FBX ingest becomes hot-path (it shouldn't — converters run at edit time, not render time), revisit.

**Alternatives considered:**
- *Link the FBX SDK.* Rejected — already rejected by the FBX SDK drop.
- *Vendor `ufbx` (a permissive C library FBX loader).* Would add a translation unit to the engine and avoid the subprocess hop. Real value, but a larger change than this PR's scope. Note as a possible future move.
- *Skip FBX in v1 and only support glTF.* User explicitly added FBX2glTF to the tree and asked for FBX support. Reject.

### Decision: Lua bindings expose `Importer.LoadGltf` / `LoadFbx` / `LoadScene` / `MergeInto`; `Demo.lua` uses them for a File menu

`engine/Fury/LuaBindings.cpp` gains an `Importer` table:

```cpp
sol::table importer_tbl = lua.create_named_table("Importer");
importer_tbl["LoadGltf"]   = [](const std::string &p)  -> std::shared_ptr<Scene> {
    return GltfImporter::Import(p, /*name*/ p, /*working_dir*/ "", {});
};
importer_tbl["LoadFbx"]    = [](const std::string &p)  -> std::shared_ptr<Scene> {
    auto fbx_result = FbxConverter::Convert(p, /*tempdir*/);
    if (!fbx_result.ok()) { FURYE << fbx_result.stderr_capture; return nullptr; }
    auto scene = GltfImporter::Import(fbx_result.output_path, p, "", {});
    std::remove(fbx_result.output_path.c_str());
    return scene;
};
importer_tbl["LoadScene"]  = [](const std::string &p)  -> std::shared_ptr<Scene> { /* dispatch by ext */ };
importer_tbl["MergeInto"]  = [](const std::shared_ptr<Scene> &target, const std::shared_ptr<Scene> &source) -> int { /* see below */ };
```

`Scene:Clear()` is bound to the existing `Scene::Clear` method. `FileUtil.ListDirectory(path, extensions_array_or_nil)` is added as a free function that wraps `std::filesystem::directory_iterator` with an optional extension filter; returns a sol::table (Lua array).

`MergeInto(target, source)` walks `source->GetRootNode()->GetChildAt(...)`, reparents each child into `target->GetRootNode()`, transfers entities (materials, meshes, animation clips) into `target->GetEntityManager()` (rejecting duplicates by hashcode — first-wins, log a warning on collision), then calls `target->GetSceneManager()->AddSceneNodeRecursively(target->GetRootNode())`. `source` is left empty and can be discarded by Lua.

`Demo.lua`'s File menu composes this surface:

```lua
Gui.SetMenuBarCallback(function()
    if Gui.BeginMenu("File") then
        if Gui.MenuItem("New Scene") then Scene.GetActive():Clear() end
        if Gui.BeginMenu("Open Scene") then
            for _, name in ipairs(FileUtil.ListDirectory("Resource/Scene/",
                                                          {".json", ".bin", ".gltf", ".glb", ".fbx"})) do
                if Gui.MenuItem(name) then
                    local imported = Importer.LoadScene("Resource/Scene/" .. name)
                    if imported then
                        Scene.GetActive():Clear()
                        Importer.MergeInto(Scene.GetActive(), imported)
                    end
                end
            end
            Gui.EndMenu()
        end
        if Gui.BeginMenu("Import") then
            for _, name in ipairs(FileUtil.ListDirectory("Resource/Scene/", {...})) do
                if Gui.MenuItem(name) then
                    local imported = Importer.LoadScene("Resource/Scene/" .. name)
                    if imported then Importer.MergeInto(Scene.GetActive(), imported) end
                end
            end
            Gui.EndMenu()
        end
        if Gui.MenuItem("Save Scene As") then show_save_modal = true end
        -- ... (Save modal renders elsewhere in on_update via Gui.Begin / Gui.InputText / Gui.Button)
        Gui.EndMenu()
    end
    -- existing Camera menu continues to work, defined below
end)
```

**Why bind the importer rather than reuse the CLI binary**: binding it lets `Demo.lua` import inline without spawning a subprocess of itself. Same `GltfImporter` code, same `FbxConverter` code, one source of truth.

**Alternatives considered:**
- *Demo.lua shells out to `./fury convert` and reloads the produced scene file.* Two-process hop per import. Slow, indirect, doesn't compose well with Lua. Reject.
- *Pure-Lua scene editor in a separate file (`SceneEditor.lua`).* Reusable across scripts. Tempting, but the v1 surface is tiny (one File menu); building a reusable abstraction now is premature. Demo.lua is the one consumer; refactor when there are two.

### Decision: `examples/main.cpp` router — same as before, no change

Unchanged from the previous design pass: dispatch on `argv[1]`, CLI path for known subcommands, Lua path otherwise.

### Decision: New `engine/Fury/Cli.{h,cpp}` for argv parsing + subcommand dispatch

Public surface:

```cpp
namespace fury {
    class Cli {
    public:
        // Returns process exit code. 0=success, 1=user error, 2=internal error.
        static int Run(int argc, char **argv);
        // Returns true if argv[1] is a CLI subcommand token (used by main.cpp).
        static bool LooksLikeSubcommand(const char *arg0);
    };
}
```

`Run` dispatches on `argv[1]`, calls a per-subcommand handler (`DoConvert`, `DoInfo`, `DoHelp`, `DoVersion`), and catches exceptions at the boundary to return code 2. Help strings live as `static const char *` constants in `Cli.cpp` so the binary has them at compile time (no file IO for `--help`).

### Decision: Mesh.cpp:120 TODO fix — add skin keys to JSON, leave existing keys byte-stable

Mesh serialization gains four new optional JSON keys:
- `bone_ids` (array of uint, 4×vertex count) — mirror of `IDs.Data`
- `bone_weights` (array of float, 3×vertex count) — mirror of `Weights.Data`
- `joints` (array of joint objects) — each joint serialized via `Joint::Save`; ordering matches `m_Joints` index order
- `root_joint` (string) — name of `m_RootJoint`

On Load, these keys are read with `if (FindMember(...))` — absent keys leave the corresponding fields at default (empty array, null pointer), preserving today's static-mesh-only behavior. On Save, the keys are emitted *only when present*: a mesh with empty `IDs` writes no `bone_ids` key. This keeps existing static-mesh `scene.json` files byte-identical to today's output.

The `m_JointMap` is rebuilt on Load by walking the loaded `m_Joints` vector and indexing by joint name. The parent/child links on `Joint` survive via `Joint::Save/Load` already (joints are `Entity`-typed with name + matrix data).

**Alternatives considered:**
- *Bump scene format version*. Premature: the new keys are strict additions, and we have no versioning machinery today. If we ever need a version bump, do it then.
- *Always emit empty arrays for `bone_ids`/`bone_weights`*. Bloats static scenes (the existing `scene.json` is already 11 MB). Reject.

### Decision: Material mapping — lossy PBR → Phong/Lambert with one warning per source material

The engine's `Material` is structured around named uniforms (`diffuse_color`, `diffuse_factor`, `specular_color`, `shininess`, `transparency`, etc.) and named texture slots (`diffuse_texture`). glTF's PBR material has a different shape (`baseColorFactor`, `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`, `emissiveFactor`, `emissiveTexture`).

v1 mapping:
- `baseColorFactor` (rgba) → `diffuse_color` (rgb, drop alpha into `transparency = 1 - a`)
- `baseColorTexture` → `diffuse_texture` slot
- `emissiveFactor` → `emissive_color`
- `alphaMode == "OPAQUE"` → `opaque = true`; `BLEND` / `MASK` → `opaque = false`
- `doubleSided` → ignored in v1 (engine has no per-material double-sided flag)
- `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture` → **read** so we can warn about them, then dropped
- Default `shininess = 32`, default `specular_color = (0.2, 0.2, 0.2)` to match the Phong/Lambert defaults the existing pipeline shaders expect

One warning per source material listing the discarded fields by name. Subsequent identical-shaped materials emit nothing further (we de-dupe by the discarded field set). Material naming: glTF `material.name` if present, else `Material_<index>`.

**Alternatives considered:**
- *Add a PBR shader variant alongside Phong/Lambert*. Listed as undecided in ARCHITECTURE.md §15. Out of scope; would balloon this change.
- *Reject glTF inputs with non-trivial PBR fields*. Too strict — most glTF in the wild has metallic/roughness even on assets that look fine under Phong. Lossy import with a warning is the pragmatic choice.

### Decision: Animation resampling at 24 fps to match `AnimationClip` shape

glTF stores time as float seconds with optional `LINEAR` / `STEP` / `CUBICSPLINE` interpolation per sampler. The engine's `AnimationClip` is integer-tick at fixed `ticksPerSecond = 24` (FBX-shaped), with rotations stored as Euler radians.

v1 strategy: for each channel, resample the source sampler at 24 Hz over the channel's time range. Translation and scale use linear interpolation (matching `AnimationPlayer`'s linear interp); rotation samples are slerped (matching `AnimationPlayer`'s slerp); each resampled rotation is then converted to Euler radians via `MathUtil::QuatToEulerRad` for storage. CUBICSPLINE → linear with a one-warning-per-sampler note.

**Alternatives considered:**
- *Switch the engine to seconds-based timing.* Listed as undecided in §15; would require touching `AnimationClip`, `AnimationPlayer`, and `Joint::Update`. Out of scope.
- *Store glTF quaternions directly as quaternions.* Would require a parallel quaternion-keyframe path in `AnimationChannel`. Reject; resample-to-Euler keeps the on-disk shape identical.
- *Bake keyframes only at the original sample times.* Possible but mixes time-domains; harder to mix-and-match channels at playback.

### Decision: `info` reads CPU-side counts only; works against both source and engine formats

`fury info <path>` is the agent-friendly inspection tool. The path extension determines the loader:
- `.json` → `FileUtil::LoadFile` into a temporary `Scene`, walk the loaded entities
- `.bin` → `FileUtil::LoadCompressedFile`, same walk
- `.gltf` / `.glb` → `tinygltf::TinyGLTF::Load*FromFile`, walk `tinygltf::Model` directly (no full conversion)

Output is a plain-text key-value summary (no JSON output in v1). Fields:
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
joints:         <count across all skins>
aabb:           min=(x,y,z) max=(x,y,z)   [scene-wide; computed by union of mesh AABBs in world space]
```

For glTF inputs, the world-space AABB is computed by walking nodes with their accumulated transforms; for engine-format inputs, the existing AABB on `Mesh` is summed with the loaded `SceneNode` world transforms.

### Decision: Importer rejects unsupported glTF features with a clear error rather than silently skipping

Morph targets (`primitive.targets`), sparse accessors (`accessor.sparse`), buffer views with non-default `byteStride`, and any `extensionsRequired` entry not on an empty allow-list cause the importer to exit with code 1 and stderr message like:

```
gltf-importer: rejected — <feature> is not supported in v1
  input: <path>
  see docs/CLI.md §Limitations
```

This matches the existing GLTFDom rejection set (per ARCHITECTURE.md §8.2). Explicit failure is better than silent data loss for an AI-discoverable tool.

### Decision: docs/CLI.md is the agent contract; help strings mirror it

Two surfaces document the CLI: `docs/CLI.md` and the binary's own `--help` output. We hold them deliberately in sync — the docs file is the single source of truth for what the help strings say (about supported extensions, limitations, exit codes). The same content is repeated in the help strings because agents may or may not have file access; the doc adds the longer "Scene format appendix" and "Future expansion" sections that wouldn't fit in `--help`.

If the two ever drift, the doc wins. A future tasks item could autogenerate help strings from the doc (or vice versa); not in v1.

## Risks / Trade-offs

- **[Risk] Lossy material mapping looks wrong on PBR-authored content.** A glTF asset designed for metallic-roughness shading will look flat under Lambert. → Mitigation: warning per material clearly lists the discarded fields. The user can either (a) accept the visual loss, (b) edit the converted scene.json to tune `diffuse_color` per material, or (c) wait for the HDR/PBR pipeline (deferred follow-up — user-confirmed).

- **[Risk] FBX2glTF binary is x86_64-only; macOS arm64 users need Rosetta.** → Mitigation: `FbxConverter::Convert` checks the binary's architecture (or attempts to run and detects the "Bad CPU type" error) and surfaces an actionable message ("Install Rosetta via `softwareupdate --install-rosetta`"). When a universal or arm64-native FBX2glTF binary becomes available, vendor it as a replacement.

- **[Risk] FBX2glTF is itself maintained by Facebook Incubator and could see unexpected behavior on edge-case FBX files.** → Mitigation: we capture stderr and propagate it verbatim. If a specific FBX trips it, the user can run FBX2glTF directly with the same arguments to debug. Documented in `docs/CLI.md`.

- **[Risk] Subprocess invocation is platform-specific.** `posix_spawn` works on macOS and Linux; Windows uses `CreateProcess`. → Mitigation: v1 only ships the macOS FBX2glTF binary, so the Windows path is gated off entirely with a clear error. When a Windows binary is added later, we'll need a `CreateProcess`-based subprocess wrapper; the wrapper lives in one file (`FbxConverter.cpp`) so the platform-specific code is contained.

- **[Risk] Resampling at 24 fps loses precision on assets authored at 30/60 fps.** A 0.5s clip authored at 60 Hz has 30 keyframes; resampled at 24 fps it's 12 keyframes. Visible as slightly less smooth motion. → Mitigation: the source-rate keyframes were FBX-shaped at 24 fps when the engine was designed for that workflow. Acceptable for v1. Real fix is the engine moving to seconds-based timing, listed as open in §15.

- **[Risk] Mesh.cpp TODO fix changes scene.json files for skinned scenes — existing skinned scene.bin files won't load skin data.** Old `scene.json` / `scene.bin` files have no `bone_ids` / `bone_weights` / `joints` keys. → Mitigation: load is tolerant — missing keys → empty arrays → treated as static mesh (which they are, today). No existing file breaks. New skinned converts produce the new shape; this is the intended behavior.

- **[Risk] glTF model has features the importer rejects (morph targets, sparse accessors, etc.) but the user expected it to "just work".** → Mitigation: rejection messages are explicit, name the feature, and point at `docs/CLI.md §Limitations`. Agents reading the error can decide whether to (a) ask the user, (b) try a different asset, or (c) flag the limitation.

- **[Risk] The CLI path links engine code but never calls its GL functions; if any constructor or static initializer touches GL, the converter will crash before main runs.** → Mitigation: there is no GL touch at static-init time in the current engine (we verified by reading `Engine::Initialize` — GL bring-up is explicit, in `GLLoader::LoadGLFunctions()` called from `Initialize`). The CLI never calls `Initialize`. If a future change adds a GL touch in a static initializer, it breaks the CLI path and tests should catch it.

- **[Trade-off] Single binary vs. separate CLI binary.** Single binary means CLI invocations link the entire engine (~26 MB binary). On the upside, no new build target, no install-surface duplication, and no risk of the CLI and runtime drifting. Accept the binary size cost.

- **[Trade-off] Hand-rolled argv parser vs. library.** Saves a submodule but means help-text formatting is manual. For 3 subcommands and 5 flags it's fine. Reevaluate at 6+ subcommands.

- **[Trade-off] Resample-to-Euler vs. quaternion-keyframe path.** Resampling loses some precision near gimbal-prone orientations but keeps the engine's `KeyFrame` shape identical. Adding a quaternion-keyframe variant would touch `AnimationChannel`, `AnimationPlayer`, and `MathUtil`. Reject for v1.

- **[Risk] Runtime importer crashes on malformed input could take the demo down.** `GltfImporter::Import` and `FbxConverter::Convert` are called inside `on_init` or a menu callback in Demo.lua. An uncaught C++ exception inside a sol2-bound lambda will propagate up through the Lua VM and either crash the process or surface as a Lua error that the engine's existing `safe_script_file` catches. → Mitigation: importer code uses early-return + logged errors rather than exceptions; the Lua bindings wrap the import in a try/catch that returns `nullptr` on any caught exception. Demo.lua's File menu treats `nil` returns as "user-visible failure, keep previous scene."

- **[Risk] Merging scenes accumulates duplicate entity names across imports.** `EntityManager` is keyed by `(type_index, hashcode)` where hashcode is derived from the name string. If two glTFs both contain a material named `"default"`, the second import's material would silently collide and be skipped — meshes from the second import end up referencing the first import's material. → Mitigation: `MergeInto` logs a warning per collision so the user sees what happened. A future iteration could rename-on-collision (`"default_2"`) but that breaks any other entity referencing the colliding name; deferred.

- **[Risk] `Demo.lua` File-menu enumeration of `Resource/Scene/` shows scene_saved.json that the user just produced — clicking it reloads what they just had.** Harmless but confusing if the user expects the menu to refresh dynamically. → Mitigation: the enumeration is run every frame inside the menu callback (cheap — directory_iterator + extension filter), so saved files appear in real time. Document the behavior in `docs/LUA.md`.

## Migration Plan

This change is additive. No existing build, file, or workflow needs migration:
- Existing `scene.json` / `scene.bin` files load unchanged (new keys are optional).
- `./fury` and `./fury Demo.lua` behave identically in the camera / render / profiler paths.
- `Demo.lua` gains a File menu but the rest of its behavior (flythrough camera, profiler) is unchanged. Users who don't open the File menu see no change.
- No new submodules. `FBX2glTF-darwin-x64` is a checked-in binary in `engine/ThirdParty/FBX2glTF/` (12 MB, vendored at user request).
- The CMake build gets an extra `add_custom_command(TARGET fury POST_BUILD ...)` step that copies the FBX2glTF binary next to the built `fury`. No CMake option changes; `file(GLOB)` picks up the new `.cpp` files automatically.

Rollback: revert the commit. No persistent state, no schema migration. The FBX2glTF binary remains in the tree but is unused.

## Open Questions

- **Should `convert` support direct `.json` ↔ `.bin` conversion in v1?** Trivial to add (`fury convert scene in.json out.bin`) — same Serializable round-trip. Could ship as a follow-up. Recommend: defer, add when actually needed.
- **Should `info` support `--json` output for machine parseability?** Plain text is fine for v1; JSON output would help agents that want to read the inspection result programmatically. Not blocking v1.
- **Should the importer write extracted textures alongside `out.json` for `.glb` inputs with embedded image data?** v1: yes, side-by-side files with names like `<output-basename>_<image-index>.png`. Confirm in task list.
- **Where do extracted textures land for `.glb` inputs whose target output is `.bin`?** Same directory, same naming. The `.bin` references them by relative URI in the same way `.json` would.
- **Should `Mesh::Save` emit `joints` inline or by reference to the EntityManager?** Inline (each Mesh owns its joints in the engine's data model — see `Mesh.h:70-74`). Confirm during implementation.
- **Should we add a `--dry-run` flag to `convert`?** Useful for testing config without writing files. Defer to v2 unless ergonomic motive emerges in testing.
- **What's the right FBX2glTF arguments set?** FBX2glTF has many flags (`--khr-materials-unlit`, `--no-animation`, `--draco`, etc.). v1 invokes it with defaults (`--input <path> --output <stem>`). If we discover that specific flags improve the engine-side import quality (e.g. forcing no-animation when the user just wants geometry, or controlling axis convention), add them as `Cli` flags on `convert fbx`. Document any flag we settle on in `docs/CLI.md`.
- **Should `MergeInto` deduplicate entities by content-hash instead of name-hash?** Today's `EntityManager` keys on `name + hashcode`; two imports with the same material name collide on the name. A content-hash would dedupe identical materials across imports. Defer — premature optimization for v1.
- **Should `Demo.lua`'s File menu remember the most recently opened file?** Convenience for human users; agents don't care. Defer; trivial to add with a Lua-side `last_opened` global.
- **Should the runtime importer surface progress to the UI?** FBX conversion takes ~2s for the tank model; the demo currently freezes for that duration. A "Converting..." ImGui toast would help, but requires async invocation (FBX2glTF runs on a worker thread, importer runs on main). Defer; v1 just blocks. Document the freeze behavior in `docs/LUA.md`.
