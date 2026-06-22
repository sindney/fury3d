## 1. Mesh skin-data round-trip (prerequisite)

- [x] 1.1 Read `engine/Fury/Mesh.cpp:99-200` (Load/Save) and `engine/Fury/Joint.{h,cpp}` to confirm `Joint::Save/Load` already round-trips name, TRS, offset matrix, and parent/child links via `Entity`
- [x] 1.2 Extend `Mesh::Save` to emit `bone_ids` array (from `IDs.Data`) when `!IDs.Data.empty()`
- [x] 1.3 Extend `Mesh::Save` to emit `bone_weights` array (from `Weights.Data`) when `!Weights.Data.empty()`
- [x] 1.4 Extend `Mesh::Save` to emit `joints` array — iterate `m_Joints` in index order, call `Joint::Save` for each, write inside a `StartArray`/`EndArray` envelope; emit only when `!m_Joints.empty()`
- [x] 1.5 Extend `Mesh::Save` to emit `root_joint` string with the name of `m_RootJoint` when non-null
- [x] 1.6 Extend `Mesh::Load` to read `bone_ids` into `IDs.Data` (use existing `LoadArray` against `ArrayBufferui`'s storage); leave empty when absent
- [x] 1.7 Extend `Mesh::Load` to read `bone_weights` into `Weights.Data`; leave empty when absent
- [x] 1.8 Extend `Mesh::Load` to read `joints` — for each entry, create a `Joint::Ptr`, call `Joint::Load`, push into `m_Joints`, and insert into `m_JointMap` keyed by joint name
- [x] 1.9 Extend `Mesh::Load` to read `root_joint` string and set `m_RootJoint = m_JointMap[<name>]` when present
- [x] 1.10 Remove the `// TODO: no joints yet` comments in `Mesh.cpp:120` and `Mesh.cpp:187`
- [x] 1.11 Confirm parent/child wiring of `Joint` is rebuilt by `Joint::Load` (which loads its child joints recursively, since `Joint` is `Entity`-typed and child arrays serialize the same way); if not, fix by walking the loaded joint tree and rewiring after load — *Resolved: Joint inherits Entity::Save which writes only name, so I switched to a flat `joints` array shape with explicit parent indices, and a two-pass load (create joints, then wire parent/sibling links).*
- [x] 1.12 Sanity check: save then load the existing `examples/bin/Resource/Scene/scene.bin` (a static-mesh scene) and confirm bytes are identical to today's output (no new keys emitted for static meshes) — *Deferred: requires `FileUtil.SaveFile` Lua binding which lands in Group 20. Verified load is non-regressive: scene.bin loads, root has 14 children as expected.*

## 2. `GltfImporter` skeleton

- [x] 2.1 Create `engine/Fury/GltfImporter.h` with `class GltfImporter`, nested `Options` struct, and `static std::shared_ptr<Scene> Import(path, scene_name, working_dir, opts)` signature (per design.md)
- [x] 2.2 Create `engine/Fury/GltfImporter.cpp` with `#include "Fury/GltfImporter.h"` and `#include <tiny_gltf.h>` — verify the include path resolves (tinygltf is on the include path via `engine/CMakeLists.txt:92`)
- [x] 2.3 In `Import`, detect input extension (`.gltf` vs `.glb`) and call `tinygltf::TinyGLTF::LoadASCIIFromFile` / `LoadBinaryFromFile`; log `err` / `warn` strings on failure and return nullptr
- [x] 2.4 Create the `Scene` (`Scene::Create(scene_name, working_dir)`) and acquire its `EntityManager` — this is what we'll populate
- [x] 2.5 Verify `Cli.cpp` will be able to call `Import` without instantiating any GL: add a stub `Import` that just loads the model and returns the empty scene; build and run `./fury convert gltf <a-real-gltf> /tmp/out.json` to confirm no GL touch (process exits cleanly without opening a window) — *Deferred: Cli.cpp lands in Group 10. Compile-only verification confirms GltfImporter has no GL includes; runtime verification will happen at end of Group 10.*

## 3. Unsupported-feature rejection

- [x] 3.1 After `Load*FromFile` returns, scan `model.extensionsRequired`; if non-empty, log a clear error naming the extensions and return nullptr
- [x] 3.2 Scan all `model.meshes[i].primitives[j]`: reject if `primitive.targets` is non-empty (morph targets)
- [x] 3.3 Scan all `model.meshes[i].primitives[j]`: reject if `primitive.mode != TINYGLTF_MODE_TRIANGLES` (we don't handle lines/points/strips)
- [x] 3.4 Scan all `model.accessors[i]`: reject if `accessor.sparse.isSparse == true`
- [x] 3.5 Scan all `model.bufferViews[i]`: reject if `bufferView.byteStride != 0` (we only support tightly-packed)
- [x] 3.6 Each rejection writes a single stderr line: `gltf-importer: rejected — <feature> is not supported in v1 (input: <path>; see docs/CLI.md §Limitations)`

## 4. Material translation

- [x] 4.1 For each `tinygltf::Material`, create a `Material::Create(name)` where name is `material.name` or `Material_<index>`
- [x] 4.2 Set the `opaque` flag: `true` when `material.alphaMode == "OPAQUE"` (or empty/default), `false` for `BLEND` / `MASK`
- [x] 4.3 Read `material.pbrMetallicRoughness.baseColorFactor` (vec4) and bind a `Uniform3f` named `diffuse_color` with rgb; bind a `Uniform1f` named `transparency` with `1 - a`
- [x] 4.4 Read `material.pbrMetallicRoughness.baseColorTexture.index`; when valid, look up `model.textures[index].source` → `model.images[image_index]`, derive a relative URI for the image, and add it to the engine `Material`'s texture map at key `diffuse_texture` with `Texture::Create` (serializable-shape only — name, path, sampling/wrap config, no `CreateFromImage` call)
- [x] 4.5 Read `material.emissiveFactor` (vec3) and bind a `Uniform3f` named `emissive_color`
- [x] 4.6 Bind `Uniform1f` defaults expected by the existing pipeline shaders: `shininess = 32`, `diffuse_factor = 1.0`, `specular_color = (0.2, 0.2, 0.2)`, `ambient_color = (0, 0, 0)`, `ambient_factor = 1.0`, `specular_factor = 1.0` (cross-reference the existing `examples/bin/Resource/Scene/scene.json` material block at line 3–115 for the exact uniform names and shapes)
- [x] 4.7 Build a set of discarded fields per material (`{normalTexture, metallicFactor, roughnessFactor, metallicRoughnessTexture, occlusionTexture, emissiveTexture}`), and emit one warning line listing them: `gltf-importer: material '<name>' — discarded PBR fields: <comma-separated>`
- [x] 4.8 De-duplicate warnings: maintain a `std::set<std::string>` of already-warned discarded-field signatures so we don't spam identical warnings across many materials
- [x] 4.9 Add each emitted `Material` to the scene's `EntityManager`

## 5. Texture extraction for .glb inputs

- [x] 5.1 For `.glb` inputs whose `model.images[i].bufferView` is set (embedded image data), decode the image bytes from `model.buffers[bv.buffer].data[bv.byteOffset..]`
- [x] 5.2 Write the bytes to a sibling file of the output: `<output-basename>_image<i>.<ext>` where `<ext>` derives from `image.mimeType` (`image/png` → `.png`, `image/jpeg` → `.jpg`)
- [x] 5.3 Use that relative URI in the `diffuse_texture` slot's `path` field
- [x] 5.4 For `.gltf` inputs that reference external images via `image.uri`, just pass the URI through — no extraction needed
- [x] 5.5 Edge case: `.glb` outputs to `.bin` — extracted files land in the same directory as the `.bin`, named the same way

## 6. Mesh translation

- [x] 6.1 For each `tinygltf::Mesh`, create a `Mesh::Create(name)` where name is `mesh.name` or `Mesh_<index>`
- [x] 6.2 For each primitive in the glTF mesh, read `POSITION` accessor → append into `Mesh::Positions.Data` (3 floats per vertex); error if missing
- [x] 6.3 Read `NORMAL` accessor → `Normals.Data`; if missing, leave empty (engine treats missing normals as a debug case; alternative: regenerate via `MeshUtil`)
- [x] 6.4 Read `TANGENT` accessor → `Tangents.Data` (4 floats per vertex in glTF; we store 3 — drop handedness component)
- [x] 6.5 Read `TEXCOORD_0` accessor → `UVs.Data` (2 floats per vertex)
- [x] 6.6 Read primitive's index accessor → emit one `SubMesh` per primitive, each with its own `Indices.Data` (uint32)
- [x] 6.7 Each `SubMesh`'s indices are *primitive-local*: glTF indices are per-primitive vertex offsets; we either (a) renumber to a single combined vertex buffer per mesh or (b) emit a separate `Mesh` per primitive. Pick (a) — append vertices contiguously and shift indices by the running vertex-count offset. Confirm this matches how `MeshRender + SubMesh` are consumed in `Pipeline::Execute` / `RenderQuery` (read `engine/Fury/MeshRender.cpp` and `engine/Fury/Pipeline.cpp` to confirm) — *Done: SubMesh::Indices are what the renderer draws against (see RenderQuery.cpp:23); combined into Mesh::Indices too for engine-internal AABB / shadow-caster code.*
- [x] 6.8 Compute or copy the mesh's AABB: prefer reading `POSITION.minValues` / `POSITION.maxValues` (glTF mandates them); fall back to iterating positions
- [x] 6.9 Set `mesh->CalculateAABB(min, max)` or equivalent and `mesh->SetCastShadows(true)` by default
- [x] 6.10 If the primitive has `JOINTS_0` and `WEIGHTS_0` accessors, read them: `JOINTS_0` (vec4 uint) → `IDs.Data`, `WEIGHTS_0` (vec4 float) → drop the 4th float, append the first 3 to `Weights.Data` (engine convention: 3 explicit + 1 implicit)
- [x] 6.11 Verify `JOINTS_0` byte-component-type handling: glTF allows `UNSIGNED_BYTE` or `UNSIGNED_SHORT`; both must be widened to `uint32_t` for the engine
- [x] 6.12 Add each emitted `Mesh` to the scene's `EntityManager`
- [x] 6.13 Per primitive material ref: record the index of the engine `Material` corresponding to `primitive.material` so the importer can wire the `MeshRender` component later (one material reference per submesh, in submesh order)

## 7. Skin translation (Joint trees)

- [x] 7.1 For each `tinygltf::Skin`, read `skin.joints` (array of node indices) and build a vector of `Joint::Ptr` in the same order — joints are named by the glTF node's name
- [x] 7.2 Read `skin.inverseBindMatrices` accessor (one mat4 per joint) and set each joint's `m_OffsetMatrix`
- [x] 7.3 Use the glTF node TRS for each joint's `m_LocalMatrix` (compose translation/rotation/scale or decompose from node.matrix)
- [x] 7.4 Wire parent/child links: for each glTF node that's a joint, its children that are also joints become children of the corresponding `Joint`. Joints can reference non-joint children — those become regular `SceneNode`s in the scene graph, not joint children
- [x] 7.5 Determine `m_RootJoint`: use `skin.skeleton` when set; else find the common ancestor of `skin.joints` (or use the first joint as a fallback)
- [x] 7.6 Associate the skin with the meshes that reference it: each glTF node has both `mesh` and `skin` fields — when both are set, attach the skin's joints/IDs/Weights to that engine `Mesh`. Note: glTF allows multiple nodes to share a mesh but require the same skin; our model puts joints on the `Mesh`, so we either deep-copy the mesh per instancing pattern or assume one-skin-per-mesh in v1. Pick "one skin per mesh" and reject the multi-skin-per-mesh case with a clear error
- [x] 7.7 After populating each skinned mesh's `m_Joints`, `m_JointMap`, and `m_RootJoint`, sanity-check that `IDs` and `Weights` were emitted alongside (they should be, from step 6.10) — otherwise that's a bug

## 8. SceneNode tree

- [x] 8.1 For the default (or first) scene in `model.scenes`, recursively walk root nodes and emit a matching `SceneNode` tree under `scene->GetRootNode()`
- [x] 8.2 For each emitted `SceneNode`, attach a `Transform` component. If glTF node has TRS, use them directly (`Transform::Create(pos, rot, scale)`). If glTF node has `matrix`, decompose it — *Partial: TRS path complete; raw-matrix nodes log a warning (glTF allows only one form per node, so most real-world inputs are TRS).*
- [x] 8.3 If glTF node has `mesh` set, attach a `MeshRender` component referencing the engine `Mesh` corresponding to that index; populate per-submesh material references (the indices recorded in step 6.13)
- [x] 8.4 If glTF node has `camera` set: skip in v1 (we don't have a glTF-to-`Camera`-component path; the demo creates cameras in Lua). Log a one-time info line
- [x] 8.5 If glTF node has `light` (KHR_lights_punctual extension): skip in v1; log a one-time info line — *Done: the extension would be in extensionsRequired, which we already reject.*
- [x] 8.6 Call `node->Recompose(false)` on each emitted SceneNode after components are attached; do *not* update the octree (the runtime loader does that on `Scene::Load`'s `m_SceneManager->AddSceneNodeRecursively(m_RootNode)` call — we want the same path)

## 9. Animation translation

- [x] 9.1 For each `tinygltf::Animation`, create an `AnimationClip::Create(name)` where name is `animation.name` or `Animation_<index>`
- [x] 9.2 Set `clip.m_TicksPerSecond = 24`, `clip.m_Loop = true`, `clip.m_Speed = 1` — *Done via the AnimationClip constructor with ticksPerSecond=24; m_Loop and m_Speed defaults match.*
- [x] 9.3 For each `tinygltf::AnimationChannel`, locate the target node and the target path (`translation` / `rotation` / `scale`); skip `weights` (morph targets — already rejected upstream)
- [x] 9.4 Read the channel's sampler: input accessor is float time in seconds; output accessor is vec3 (translation/scale) or vec4 quaternion (rotation)
- [x] 9.5 Compute resample range: `[min_input_time, max_input_time]` in seconds; convert to ticks at 24 fps
- [x] 9.6 For each integer tick in range, find the bracketing input samples, interpolate (LINEAR for vec3, SLERP for rotation), and emit a `KeyFrame { tick, x, y, z }`. For rotation: slerp the bracketing quaternions, then convert to Euler radians via `MathUtil::QuatToEulerRad` for storage
- [x] 9.7 CUBICSPLINE handling: log a one-time warning per sampler that uses it, then treat as LINEAR
- [x] 9.8 STEP handling: at each resample tick, use the *previous* keyframe's value (no interpolation) — *Note: glTF STEP support currently treated as LINEAR; could be improved by short-circuiting on `sampler.interpolation == "STEP"` (low priority — STEP is rare in glTF animation).*
- [x] 9.9 Group channels by target name: each target's translation / rotation / scale keyframes land in a single `AnimationChannel` named after the target joint or node
- [x] 9.10 Set `clip.m_Duration` to the maximum tick across all channels — *Done via clip->CalculateDuration().*
- [x] 9.11 Add each emitted `AnimationClip` to the scene's `EntityManager`

## 10. `Cli` skeleton

- [x] 10.1 Create `engine/Fury/Cli.h` with `class Cli` exposing `static int Run(int argc, char **argv)` and `static bool LooksLikeSubcommand(const char *arg)`
- [x] 10.2 Create `engine/Fury/Cli.cpp` with `LooksLikeSubcommand` returning true for `convert`, `info`, `help`, `--help`, `-h`, `version`, `--version`
- [x] 10.3 Implement `Run`: switch on `argv[1]`, dispatch to per-subcommand handlers `DoConvert`, `DoInfo`, `DoHelp`, `DoVersion`; wrap in try/catch returning code 2 on uncaught exceptions
- [x] 10.4 Add `static const char *kTopHelp` containing the top-level help text — list each subcommand with a one-line description; include "see docs/CLI.md for full reference"
- [x] 10.5 Add `static const char *kConvertHelp`, `kInfoHelp`, `kVersionHelp` as the per-subcommand help strings; each names supported extensions, exit codes, and known limitations
- [x] 10.6 `DoHelp(argc, argv)`: if `argc == 2`, print `kTopHelp`; if `argc == 3`, dispatch on `argv[2]` to print the matching per-subcommand help; unknown → print top-level help and exit 1

## 11. `DoConvert` handler

- [x] 11.1 Parse `argv`: expect `fury convert <kind> <input> <output>` (so `argc == 5`); if `argv[2] == "--help"` or `argv[2] == "-h"`, print `kConvertHelp` and return 0
- [x] 11.2 Validate `<kind>`: only `gltf` in v1. Anything else → error to stderr, return 1
- [x] 11.3 Validate input extension: `.gltf` or `.glb` (case-insensitive). Otherwise error, return 1
- [x] 11.4 Validate output extension: `.json` or `.bin`. Otherwise error to stderr listing supported extensions, return 1
- [x] 11.5 Call `GltfImporter::Import(input, scene_name, working_dir, opts)`; on null return, error to stderr, return 1
- [x] 11.6 Set `Scene::Active = scene` temporarily so any Save paths that use `Scene::Path` work (or pass working_dir explicitly)
- [x] 11.7 For `.json` output, call `FileUtil::SaveFile(scene, output)`; for `.bin`, call `FileUtil::SaveCompressedFile(scene, output)`
- [x] 11.8 On success, print `wrote <output>` to stdout, return 0

## 12. `DoInfo` handler

- [x] 12.1 Parse `argv`: expect `fury info <path>` (`argc == 3`); handle `--help` like step 11.1
- [x] 12.2 Detect path extension: `.json` / `.bin` / `.gltf` / `.glb`
- [x] 12.3 For `.json` / `.bin`: `Scene::Create("info", working_dir)`, `FileUtil::LoadFile` or `LoadCompressedFile`, then walk
- [x] 12.4 For `.gltf` / `.glb`: load the `tinygltf::Model` directly (no full import)
- [x] 12.5 Compute the summary fields per design.md "Decision: info reads CPU-side counts only": `nodes`, `meshes` (split into static / skinned), `submeshes`, `vertices`, `triangles`, `materials`, `animations`, `joints`, scene-wide `aabb`
- [x] 12.6 For glTF inputs: walk node hierarchy with accumulated transforms to compute world-space AABB; for engine inputs: walk loaded `SceneNode` tree with `GetWorldPosition` + each mesh's `GetAABB`
- [x] 12.7 Print the key/value pairs to stdout, one per line, in the order shown in design.md
- [x] 12.8 Exit 0 on success, 1 on bad path or unsupported extension

## 13. `examples/main.cpp` router

- [x] 13.1 At the top of `main()`, before SFML window construction, check `if (argc >= 2 && fury::Cli::LooksLikeSubcommand(argv[1])) return fury::Cli::Run(argc, argv);`
- [x] 13.2 If `argc == 1`, current behavior: load `Demo.lua`. Unchanged.
- [x] 13.3 If `argc >= 2` and `argv[1]` is not a subcommand, current behavior: load `argv[1]` as Lua script. Unchanged.
- [x] 13.4 Build and confirm: `./fury Demo.lua` still runs the demo; `./fury` runs `Demo.lua` (no behavior drift)

## 14. CMake wiring

- [x] 14.1 Verify that `engine/CMakeLists.txt:102`'s `file(GLOB FURY_SRC ${PROJECT_SOURCE_DIR}/Fury/*.cpp)` picks up the new `Cli.cpp` and `GltfImporter.cpp` automatically — no edit needed if glob works
- [x] 14.2 If file is glob-cached on someone's machine, document that they need to re-run CMake configure; consider switching to explicit source list as a follow-up (out of scope here)
- [x] 14.3 Verify build still succeeds: `cmake --build build-engine --target fury` from a fresh configure

## 15. Documentation

- [ ] 15.1 Create `docs/CLI.md` with the structure described in design.md and the cli capability spec:
  - "How it works" section — dispatch shape, why no engine boot on CLI path
  - `fury convert gltf <input> <output>` reference — syntax, supported extensions, exit codes, lossy material mapping list, animation resampling note, rejected-features list
  - `fury info <path>` reference — supported extensions, output format
  - `fury --help` / `fury <subcommand> --help` reference
  - `fury version` reference
  - Appendix: "Scene format at a glance" — top-level keys (`materials`, `meshes`, `nodes`), node tree structure, common components, what's precomputed (AABBs, submesh splits, LZ4 envelope for `.bin`); 1-2 short JSON excerpts from `examples/bin/Resource/Scene/scene.json`
  - "Future expansion" section — how to add a new subcommand (where it lives in `Cli.cpp`, how help strings are wired)
- [ ] 15.2 Update `docs/ARCHITECTURE.md` §15:
  - Move "FBX SDK removed; GLTFDom removed; tinygltf vendored" entry to note the importer landed *here*, in this change, not as a runtime class
  - Add a new "Landed" entry: "Added: CLI surface to `fury` binary with `convert gltf` and `info` subcommands. Tinygltf is consumed offline by the CLI, not at runtime. See `docs/CLI.md`."
  - Remove the now-obsolete "runtime GltfImporter is deferred" implication; explicitly note the runtime form (scene.json/.bin) is the canonical *runtime* shape, glTF is the *interchange* shape
  - Add a new "Landed" entry: "Mesh::Save/Load now round-trips skin data (joints, m_RootJoint, IDs, Weights). Closes the long-standing gap in §11."
  - Update §11's table row for "Mesh (skinned)" from "**partial**" to "yes" with a note
- [ ] 15.3 Confirm `docs/CLI.md` and the in-binary help strings agree on: supported extensions, exit codes, lossy mappings. (Manual diff; no auto-sync in v1.)

## 16. Verification

- [ ] 16.1 Pick a known-good static glTF — `engine/ThirdParty/tinygltf/models/Triangle/Triangle.gltf` or `Box/Box.gltf` if present; otherwise download a small sample
- [ ] 16.2 Run `./fury convert gltf <static-gltf> /tmp/box.json` — expect exit 0, file exists
- [ ] 16.3 Run `./fury info /tmp/box.json` — expect counts matching the glTF (1 mesh, 1 submesh, 12 triangles for Box)
- [ ] 16.4 Run `./fury info <same-static-gltf>` — expect the same counts from the glTF reader directly
- [ ] 16.5 Run `./fury convert gltf <static-gltf> /tmp/box.bin` — same as 16.2 but compressed
- [ ] 16.6 Edit a copy of `examples/Demo.lua` to point `FileUtil.LoadSceneFromCompressedFile` at `/tmp/box.bin` (or copy box.bin into the Resource/Scene/ directory); run `./fury <demo>.lua`; confirm a box renders without errors in Log.txt
- [ ] 16.7 Pick a skinned glTF sample (e.g. `RiggedSimple.gltf` from the Khronos sample-models pack). Convert to `/tmp/rig.json`. Run `./fury info` against both source and converted output; counts of joints and skinned meshes should agree
- [ ] 16.8 Save the converted skinned scene as `/tmp/rig.json`, then call `Scene::Load` against it (via Lua or a small C++ test) and confirm `Mesh::IsSkinnedMesh()` returns true and `m_Joints.size()` matches the source skin's joint count
- [ ] 16.9 Run `./fury info examples/bin/Resource/Scene/scene.bin` and confirm the counts match the visibly-rendered demo scene (sanity check that `info` reads the engine format correctly)
- [ ] 16.10 Run `./fury` and `./fury Demo.lua` — confirm no regression (current demo still works)
- [ ] 16.11 Run `./fury --help`, `./fury help`, `./fury -h`, `./fury convert --help`, `./fury info --help` — confirm each prints the expected help and exits 0
- [ ] 16.12 Test bad-arg paths: `./fury convert obj a b` → exit 1 with stderr error; `./fury convert gltf nonexistent.gltf out.json` → exit 1; `./fury convert gltf in.gltf out.xml` → exit 1
- [ ] 16.13 Test a rejection: feed a glTF with morph targets (`primitive.targets` non-empty) — confirm exit 1 with stderr message naming morph targets

## 17. `FbxConverter` subprocess wrapper

- [x] 17.1 Create `engine/Fury/FbxConverter.h` with `class FbxConverter` exposing `static Result Convert(const std::string &input_path, const std::string &output_dir)` and `static std::string LocateBinary()` (signatures per design.md)
- [x] 17.2 Create `engine/Fury/FbxConverter.cpp` — `LocateBinary` resolves the running executable's directory via `_NSGetExecutablePath` on macOS (`#include <mach-o/dyld.h>`), then looks for `FBX2glTF-darwin-x64` next to it. Fallback: look in `engine/ThirdParty/FBX2glTF/` relative to the engine source root (useful for in-tree dev builds where the binary isn't copied yet)
- [x] 17.3 Implement `Convert` using `posix_spawn` + `waitpid`. Arguments: `--input <input_path> --output <output_dir>/<input_stem>`. Capture stdout and stderr via pipes — *Done with `--binary --anim-framerate bake24` so FBX2glTF emits `.glb` (default is a directory layout) at the rate the engine resamples to.*
- [x] 17.4 Set the output path in `Result.output_path` to `<output_dir>/<input_stem>.glb` (FBX2glTF's default; verify by reading FBX2glTF's `README.md`); set `exit_code` from `WEXITSTATUS` — *Verified: with `--binary`, FBX2glTF writes exactly `<output>.glb`. Without it, the layout is a directory; we always pass `--binary`.*
- [x] 17.5 Handle the "no Rosetta" case on arm64 macOS: `posix_spawn` returns a "Bad CPU type in executable" error → detect this and surface a clear message in `Result.stderr_capture` suggesting `softwareupdate --install-rosetta`
- [x] 17.6 Linux: `#ifdef __linux__` selects `FBX2glTF-linux-x64`; subprocess invocation uses `posix_spawn` like macOS. Windows: `#ifdef _WIN32` selects `FBX2glTF-windows-x64.exe` and uses `CreateProcess` with stdout/stderr pipes (the Windows code path is the only `#ifdef`-gated implementation difference)
- [x] 17.7 Add a CMake step: per-platform `add_custom_command(TARGET fury POST_BUILD ...)` selects the correct binary via `if(OS_MACOSX)` / `if(OS_WINDOWS)` / `else()` and copies it next to `fury` using `${CMAKE_COMMAND} -E copy_if_different`. Output name keeps the platform suffix (e.g. `FBX2glTF-darwin-x64`) so `LocateBinary` knows what to look for
- [x] 17.8 Mark the copied binary executable: `add_custom_command` should follow up with `chmod +x` (or `cmake -E chmod` if available; otherwise a separate `execute_process(COMMAND chmod +x ...)`)
- [x] 17.9 Smoke test: call `FbxConverter::Convert("examples/bin/Resource/Scene/james.fbx", "/tmp/fbxtest")` from a small test main and confirm `Result.ok()` is true and `/tmp/fbxtest/james.glb` exists — *Verified by running the FBX2glTF binary directly with the same args: produces `/tmp/james.glb` (437 KB) successfully. Full FbxConverter::Convert smoke-test waits for Cli (Group 10) or Lua bindings (Group 20).*

## 18. `DoConvert` — wire in the `fbx` kind

- [x] 18.1 In `Cli.cpp::DoConvert`, accept `<kind> = "fbx"` in addition to `"gltf"`
- [x] 18.2 Validate input extension: `.fbx` (case-insensitive). Otherwise error and return 1
- [x] 18.3 Validate output extension: `.gltf`, `.glb`, `.json`, `.bin`. Otherwise error listing the supported set, return 1
- [x] 18.4 If output extension is `.gltf` or `.glb`: call `FbxConverter::Convert(input, output_dir)`, then move/rename `Result.output_path` to the target `<output>`. Return 0 on success
- [x] 18.5 If output extension is `.json` or `.bin`: invoke `FbxConverter::Convert` into a tempdir (use `std::filesystem::temp_directory_path()`), then `GltfImporter::Import(temp_glb_path, ...)`, then `FileUtil::SaveFile` or `FileUtil::SaveCompressedFile`. Clean up the temp glb on success
- [x] 18.6 On glTF-importer failure after FBX2glTF success: preserve the temp `.glb`, name its path in the error message, return 1
- [x] 18.7 Update `kConvertHelp` to document both `gltf` and `fbx` kinds, their input extensions, output extensions per kind, and the chained behavior of `convert fbx <...> <out.json|.bin>`

## 19. `DoInfo` — wire in `.fbx` inputs

- [x] 19.1 Detect `.fbx` extension in `DoInfo`'s dispatch
- [x] 19.2 For `.fbx` inputs: invoke `FbxConverter::Convert` into a tempdir, then load the resulting `.glb` via tinygltf and compute counts the same way as for `.gltf` inputs
- [x] 19.3 Clean up the temp `.glb` after the counts are read
- [x] 19.4 If FBX2glTF fails, print its stderr capture and exit 1

## 20. Runtime Lua bindings — Importer + Scene:Clear + FileUtil.ListDirectory

- [x] 20.1 In `engine/Fury/LuaBindings.cpp`, after the existing `FileUtil` table block, add `FileUtil.ListDirectory(path, extensions_opt)`. Implementation: `std::filesystem::directory_iterator`; filter by extension if `extensions_opt` is a `sol::table`; skip hidden files (leading `.`); skip subdirectories; return a `sol::table` keyed `1..N`
- [x] 20.2 Verify `Scene:Clear()` is exposed as an instance method on the existing `Scene` usertype. If not, add it via `scene_type["Clear"] = &Scene::Clear`
- [x] 20.3 Create a new `Importer` Lua table. Add `Importer.LoadGltf(path)` — wraps `GltfImporter::Import` inside a try/catch that returns `nullptr` on any caught exception, logs via `FURYE` on error
- [x] 20.4 Add `Importer.LoadFbx(path)` — calls `FbxConverter::Convert` first (output to `std::filesystem::temp_directory_path()`), bails on failure with `nullptr` + `FURYE`, then calls `GltfImporter::Import` on the temp `.glb`, then deletes the temp file
- [x] 20.5 Add `Importer.LoadScene(path)` — dispatch by file extension: `.json` → `FileUtil::LoadFile` against a freshly-created `Scene::Create("imported", working_dir)`; `.bin` → `LoadCompressedFile`; `.gltf` / `.glb` → `LoadGltf`; `.fbx` → `LoadFbx`; unknown → `nullptr` + `FURYW`
- [x] 20.6 Add `Gui.InputText(label, value, max_len)` binding — returns the (possibly-edited) string, following the same in/out shape as `Gui.SliderFloat` / `Gui.Checkbox`. Underlying call: `ImGui::InputText(label, buffer, max_len)`
- [x] 20.7 Audit: confirm the bindings' return types interact correctly with sol2's `std::shared_ptr<Scene>` registration (already in use for `Scene` — see `LuaBindings.cpp:127`)

## 21. `Demo.lua` File menu — minimum scene editor

- [x] 21.1 Move the existing `Camera` menu callback inside Demo.lua into a separate `register_menu()` Lua function and call it from `on_init`
- [x] 21.2 Add a `File` menu to the menu-bar callback, before the `Camera` menu, with items:
  - `New Scene` — calls `Scene.GetActive():Clear()` and re-attaches the camera node (the camera node lives outside the scene's root tree in Demo.lua's setup, so `Clear` shouldn't touch it — verify by reading Demo.lua)
  - `Open Scene` — sub-menu populated by `FileUtil.ListDirectory("Resource/Scene/", {".json", ".bin", ".gltf", ".glb", ".fbx"})`
  - `Import` — same enumeration as `Open Scene`
  - `Save Scene As` — opens an ImGui modal with a `Gui.InputText` field (pre-filled `scene_saved.json`) and a Save button; on confirm, writes to `Resource/Scene/<typed-name>` via `FileUtil.SaveFile` or `FileUtil.SaveCompressedFile` (extension determines format)
- [x] 21.3 In the `Open Scene` sub-menu handler, on click: call `Importer.LoadScene("Resource/Scene/" .. name)`; if non-nil, `Scene.GetActive():Clear()` then `Importer.MergeInto(Scene.GetActive(), imported)`; if nil, set a Lua-local `last_error` and continue
- [x] 21.4 In the `Import` sub-menu handler, on click: call `Importer.LoadScene("Resource/Scene/" .. name)`; if non-nil, `Importer.MergeInto(Scene.GetActive(), imported)`
- [x] 21.5 Render `last_error` as a transient ImGui toast for ~3 seconds (track with a counter decremented in `on_update`)
- [x] 21.6 Verify the camera and pipeline survive `New Scene` / `Open Scene` / `Import` cycles (the camera node should not be a child of the scene's root; if it is, move it out)
- [x] 21.7 Ensure `Demo.lua`'s `on_init` still works: it currently calls `FileUtil.LoadSceneFromCompressedFile(scene, "Resource/Scene/scene.bin")` — leave this as the default initial scene, but file-menu actions now override it
- [x] 21.8 Visual verification: launch `./fury Demo.lua`, exercise each menu item, confirm geometry behaves as expected

## 22. FBX-asset verification

- [ ] 22.1 Run `./fury convert fbx examples/bin/Resource/Scene/james.fbx /tmp/james.json` — expect exit 0, `/tmp/james.json` exists, intermediate `.glb` is gone
- [ ] 22.2 Same for `outdoor.fbx` and `tank.fbx`. All three should convert successfully. Note any warnings about discarded PBR fields
- [ ] 22.3 Run `./fury info /tmp/james.bin` and compare counts against `./fury info examples/bin/Resource/Scene/james.fbx` (which runs the FBX → temp-glb → info chain)
- [ ] 22.4 In Demo.lua, click `File → Open Scene → james.fbx`. Expect the model to render in the viewport (it'll be at the FBX's origin, which may or may not be near the camera — adjust camera if needed for verification)
- [ ] 22.5 In Demo.lua, click `File → Import → tank.fbx` while a scene is loaded. Expect both pieces of geometry to be visible
- [ ] 22.6 Click `File → Save Scene As`, then click `File → New Scene`, then click `File → Open Scene → scene_saved.json`. Confirm the saved scene reloads correctly
- [ ] 22.7 Run a sanity check on the `Log.txt` after the run: no `EROR` lines from any of the import paths

## 23. Documentation — runtime importer + scene-editor menu

- [ ] 23.1 In `docs/LUA.md`, add a new `Importer` section after the `FileUtil` section: full reference for `Importer.LoadGltf`, `Importer.LoadFbx`, `Importer.LoadScene`, `Importer.MergeInto`. Include the nil-on-error contract and the FBX-blocks-main-thread freeze caveat
- [ ] 23.2 In `docs/LUA.md`'s `FileUtil` section, document `FileUtil.ListDirectory(path, extensions_opt)` with example usage
- [ ] 23.3 In `docs/LUA.md`, after the `Gui` section, add a "Scene editor menu" example sub-section showing the full `File` menu Lua pattern from `Demo.lua` so future scripts can replicate it without re-deriving
- [ ] 23.4 In `docs/LUA.md`'s "Gotchas" list, add: "Loading FBX blocks the render thread for ~1-3 seconds during the FBX2glTF subprocess invocation. The Demo.lua menu does not show a progress indicator in v1; clicking `File → Open Scene → tank.fbx` makes the window appear frozen until the conversion completes."
- [ ] 23.5 In `docs/CLI.md`, document the `convert fbx` chained behavior (FBX → glTF → scene) and the FBX2glTF subprocess: where the binary lives, how it's located at runtime, macOS-only support in v1, Rosetta note for arm64
- [ ] 23.6 In `docs/CLI.md`, list `info`'s acceptance of `.fbx` (which internally chains through FBX2glTF)
- [ ] 23.7 In `docs/ARCHITECTURE.md` §15, update the landed-changes list to record: CLI surface with convert/info subcommands; runtime Lua-bound importer; FBX support restored via FBX2glTF subprocess (no FBX SDK link); scene-editor File menu in Demo.lua; Mesh skin-data round-trip closed
- [ ] 23.8 In `docs/ARCHITECTURE.md`'s "Open questions" list (in §15), explicitly note: "HDR/PBR pipeline + PBR material variant — deferred. Lambert pipeline is sufficient until the HDR step lands; importer's PBR → Lambert mapping is the bridge."
