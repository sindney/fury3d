## Context

Fury3D today imports models through the Autodesk FBX SDK via `FbxParser.{h,cpp}`, gated by the `_FURY_FBXPARSER_IMP_` preprocessor flag and the `FBXPARSER_IMP` CMake option. A second, partial glTF 2.0 attempt sits beside it as `GLTFDom.{h,cpp}` — JSON DOM only, no scene-graph build, dead-ends at `FileUtil::PrivateLoadGLTFFile` (a stub). The architecture document (§1, §8.1, §8.2, §13.3, §15) commits to dropping FBX and adopting glTF 2.0 via the vendored `tinygltf` (which already lives at `furyengine/tinygltf` as a sibling clone), and explicitly retires `GLTFDom` in the same breath.

`tinygltf` upstream (`syoyo/tinygltf`) is a single-header C++11 library plus one `.cc` translation unit. The clone at `furyengine/tinygltf` carries tags up to `v3.0.0`; `v2.9.7` is the latest 2.x release. v3.0.0 is a major API rewrite and we are deliberately staying on the 2.x line for stability — the engine has zero tinygltf calls today, so no migration cost yet, but the importer follow-up will benefit from the more widely-deployed 2.x API and the larger pool of community examples.

The engine ships **no working glTF importer today** — the FBX path is what `Demo.cpp` formerly used to author `Resource/Scene/scene.bin`, but at runtime `Demo.cpp` reads that file via `FileUtil::LoadCompressedFile` (JSON+LZ4), which has no FBX or glTF dependency. That decoupling is what makes a compile-only milestone feasible: we can rip out the FBX path without breaking the demo.

Existing ThirdParty layout (`engine/ThirdParty/{ImGui,LZ4,STB}`) is **vendored by direct copy**, not by submodule. The repo currently has no `.gitmodules`. The user explicitly asked whether submodules are the open-source norm — yes, they are the standard pattern when the dependency is itself a live git repo with semantic-versioned tags, which is exactly the case here. Submodules give us:
- A pinned, auditable upstream commit recorded in tree.
- One-command updates (`git -C engine/ThirdParty/tinygltf fetch --tags && git checkout vX.Y.Z`).
- Upstream's license stays with upstream — no risk of license drift.

The trade-off is the `git submodule update --init --recursive` step on first clone. We accept this because tinygltf is large enough (~200KB header, plus `json.hpp` ~900KB) that copy-vendoring would noticeably bloat the engine repo and obscure the upstream pin.

## Goals / Non-Goals

**Goals:**
- Remove every code, build, and runtime coupling to the Autodesk FBX SDK.
- Vendor `tinygltf v2.9.7` as a git submodule at `engine/ThirdParty/tinygltf/`.
- Wire `tinygltf` into the engine CMake build so future engine code can `#include <tiny_gltf.h>` and link against `libfury` without extra setup.
- Delete `GLTFDom.{h,cpp}` and the dead `FileUtil` glue around it.
- Verify `examples/Demo.cpp` compiles and links via the existing CMake setup on macOS (the user's host: Darwin 24.6.0). The runtime path through `scene.bin` is unchanged, so successful link + first-frame render is the acceptance signal.

**Non-Goals:**
- Implementing `GltfImporter` (a separate change). The C++ that walks `tinygltf::Model` into engine `SceneNode` / `Mesh` / `Material` / `Joint` / `AnimationClip` is out of scope. We add tinygltf to the build but call zero tinygltf APIs in this change.
- Authoring or committing a sample `.glb` asset.
- ANGLE / GLES retargeting, sol2 bindings, pytest scaffolding (separate refactor strands per ARCHITECTURE.md §15).
- Changing `Mesh`'s 4-bone limit, `Material`'s Phong/Lambert shape, `AnimationClip`'s frame-tick timing, or `Serializable`'s `void*` rapidjson layer (open questions in §15).
- Updating ARCHITECTURE.md sections that describe FBX/`GLTFDom` as present — those will be revised in the importer change so the doc lands at the end-state in one revision rather than two.

## Decisions

### D1. Vendor tinygltf as a git submodule pinned to v2.9.7

**Choice**: `git submodule add https://github.com/syoyo/tinygltf.git engine/ThirdParty/tinygltf && cd engine/ThirdParty/tinygltf && git checkout v2.9.7 && cd - && git add .gitmodules engine/ThirdParty/tinygltf`.

**Rationale**: Submodules are the canonical open-source pattern for vendoring an active, tagged C++ dependency. They make the upstream pin explicit (one SHA, one tag), keep the engine repo small, and let consumers update with a single `git submodule update --remote --merge`. v2.9.7 is the latest tag in the 2.x line; v3.0.0 is a major API rewrite that we have no reason to opt into when there's no existing tinygltf code to migrate.

**Alternatives considered**:
- **Copy `tiny_gltf.h` + `tiny_gltf.cc` + `json.hpp` in-tree** (matches ImGui/LZ4/STB). Rejected: bloats the repo by ~1MB of generated header text, hides the upstream version, and complicates updates. The existing copy-vendored deps are smaller and were copied long before the project committed to a glTF future.
- **CMake `FetchContent_Declare`**. Rejected: makes the build network-dependent at configure time, which is a worse failure mode than the one-time `submodule update` step (especially on CI hosts with restricted egress). The existing CMake makes no use of FetchContent, so this would also introduce a new pattern.

### D2. tinygltf entered into the build but not yet *called*

**Choice**: `engine/CMakeLists.txt` adds `engine/ThirdParty/tinygltf` to the include path and appends `engine/ThirdParty/tinygltf/tiny_gltf.cc` to the `fury` library source list. We do not include `<tiny_gltf.h>` from any engine `.h`/`.cpp` in this change — the wiring exists so the importer change can land on top with zero CMake churn.

**Rationale**: Validates the build wiring (compile of `tiny_gltf.cc`, include path, linkage) at this milestone instead of dumping that risk on the importer change. `tiny_gltf.cc` is the upstream-recommended approach to put `TINYGLTF_IMPLEMENTATION` in exactly one TU; we just adopt it as-is.

**Alternatives considered**:
- **Defer all CMake changes to the importer change**. Rejected: the user's explicit acceptance criterion is "compile the project to make sure compiling is working." Wiring tinygltf as part of the same milestone is what *makes* that compile a meaningful test.

### D3. Demo loads scene.bin unchanged; no .glb asset in this change

**Choice**: `examples/Demo.cpp` is not edited. It continues to invoke `FileUtil::LoadCompressedFile(Scene::Active, FileUtil::GetAbsPath("Resource/Scene/scene.bin"))`, which goes through the existing `Serializable` / rapidjson / LZ4 path — none of which depends on FBX or `GLTFDom`.

**Rationale**: Honoring the user's explicit guidance ("compile the project to make sure compiling is working"). Switching to a `.glb` requires a working `GltfImporter` plus a committed sample asset, which is out of scope. The compile-and-render-existing-scene milestone is small, verifiable, and bounded.

**Alternatives considered**:
- **Switch demo to load a sample .glb**. Rejected via user-confirmed scope choice. Would entangle this change with importer development and asset authoring.

### D4. Surgical removal of FBX SDK + GLTFDom; Mesh data layout untouched

**Choice**: We delete `FbxParser.{h,cpp}` and `GLTFDom.{h,cpp}` in their entirety, plus:
- `engine/Fury/Engine.cpp:5` — `#include "Fury/FbxParser.h"`.
- `engine/Fury/Engine.cpp:41-42` — `#ifdef _FURY_FBXPARSER_IMP_ / FbxParser::Initialize()` block.
- `engine/Fury/Fury.h:19` — `#include "Fury/FbxParser.h"`.
- `engine/Fury/Mesh.h:58` — `friend class FbxParser`.
- `engine/Fury/FileUtil.{h,cpp}` — the `class GLTFDom;` forward decl, `PrivateLoadGLTFFile`, and `AccessBuffer` template.
- `engine/CMakeLists.txt` and `examples/CMakeLists.txt` — every `FBXPARSER_IMP` / `FBXSDK_*` / `${FBXSDK_LIB}` reference.

We do **not** modify `Mesh`'s field layout, `Joint`'s offset-matrix convention, or `AnimationClip`'s `m_TicksPerSecond = 24` default. Those are FBX-shaped but they are also engine-internal contracts that the JSON/LZ4 round-trip and the future `GltfImporter` will both interact with — changing them is out of scope per ARCHITECTURE.md §15 open questions.

**Rationale**: Smallest blast radius that achieves the goal. Engine-internal types stay frozen so the `Serializable` JSON path keeps round-tripping `scene.bin` byte-for-byte.

**Alternatives considered**:
- **Leave the FBX option behind a default-OFF flag**. Rejected: the architecture doc commits to dropping FBX, and we'd be carrying dead code and a misleading build flag forever. Clean removal now.
- **Remove `m_TicksPerSecond` and the 4-bone limit at the same time**. Rejected: those land in the importer change so we can shape them around glTF's actual data model rather than guessing.

### D5. Header-include scope of tiny_gltf.h kept narrow

**Choice**: `tiny_gltf.h` is **not** added to any public engine header in this change. Only `tiny_gltf.cc` (which `#define`s `TINYGLTF_IMPLEMENTATION` and includes the header) is compiled into `libfury`. The follow-up importer change will introduce a new `engine/Fury/GltfImporter.{h,cpp}` whose `.cpp` includes `<tiny_gltf.h>` privately.

**Rationale**: Upstream `tiny_gltf.h` is large (~6,000 lines) and pulls in `json.hpp` (~25,000 lines). Keeping it out of public engine headers prevents a global compile-time bloat for every engine consumer. The narrow scope also means this change cannot accidentally couple unrelated engine modules to tinygltf.

## Risks / Trade-offs

- **Risk**: `tiny_gltf.cc` upstream `#define`s control which image decoders are pulled in (`STB_IMAGE_IMPLEMENTATION`, `STB_IMAGE_WRITE_IMPLEMENTATION`). If left at upstream defaults, this could ODR-collide with the engine's existing `engine/ThirdParty/STB` usage in `Texture.cpp`. → **Mitigation**: Wrap our compilation of `tiny_gltf.cc` with `-DTINYGLTF_NO_STB_IMAGE -DTINYGLTF_NO_STB_IMAGE_WRITE` so tinygltf uses image-byte passthrough; the importer change will load images through the engine's existing `Texture::CreateFromImage` (stb-based). Verify by inspecting the link map for duplicate `stbi_*` symbols.
- **Risk**: macOS link step previously listed `${FBXSDK_LIB}` even when `FBXPARSER_IMP=OFF`. → **Mitigation**: When `FBXPARSER_IMP` is OFF, `FBXSDK_LIB` expands to the empty string, so removing it is a no-op behaviorally; verify via `cmake --build build --verbose` that no `-l` entry mentions FBX after the change.
- **Risk**: Submodule URL drift (upstream renames the repo). → **Mitigation**: Pin to the SHA of `v2.9.7` in addition to the tag, recorded by the submodule entry in `.gitmodules` plus the gitlink in tree. If upstream disappears, the submodule still resolves to a specific SHA; we'd need to redirect the URL but lose no source.
- **Risk**: Future PRs that rely on `FBXPARSER_IMP=ON` to build the engine. → **Mitigation**: Mark this change as **BREAKING** in the proposal and call out the migration ("stop setting `FBXPARSER_IMP`; it no longer exists") in the eventual archive note.
- **Risk**: A developer pulls the change without `git submodule update --init` and gets a baffling missing-header error from `tiny_gltf.cc`. → **Mitigation**: Add a one-line README pointer in the engine README under the existing "Building" section, and surface the same hint in `engine/CMakeLists.txt` as a CMake `message(FATAL_ERROR …)` if `engine/ThirdParty/tinygltf/tiny_gltf.h` is missing.
- **Trade-off**: tinygltf at v2.9.7 (not v3.0.0). v3 has API improvements but lacks the long-tail of community example code. We can re-evaluate at the importer change or later; the cost of the upgrade is bounded because we have no callers yet.
- **Trade-off**: We are validating "the project compiles and links" but not "every code path runs end-to-end." Coverage of the FBX-removal change is bounded by what `Demo.cpp` exercises (which is the JSON/LZ4 path, not asset import). Acceptable because the importer change will exercise the new code under its own tests.

## Migration Plan

This change is BREAKING for downstream consumers who have been building with `FBXPARSER_IMP=ON`. The migration is purely a build-config delta:

1. Pull the change.
2. Run `git submodule update --init --recursive` to materialize `engine/ThirdParty/tinygltf`.
3. Remove any `-DFBXPARSER_IMP=ON` from local CMake invocation; remove any `-DFBXSDK_INCLUDE=…` and `-DFBXSDK_LIB_*=…` cache overrides.
4. Reconfigure (`cmake -S engine -B build`) and rebuild.

No source-level migration is required for downstream code that consumes the public engine API: `FbxParser` and `GLTFDom` were both engine-internal types used only via their `Initialize()` and `LoadScene` entry points respectively. Any code that called those directly was running off-spec already and is the caller's responsibility to migrate (likely waiting for the importer change anyway).

**Rollback strategy**: This change is a single PR with a discrete diff. To revert, `git revert` the merge commit and `git submodule deinit engine/ThirdParty/tinygltf` to remove the working-tree submodule. The `FBXPARSER_IMP` build path returns intact.

## Open Questions

None blocking this change. ARCHITECTURE.md §15 enumerates the deferred decisions (PBR vs Phong, >4 bones, seconds-vs-ticks animation timing, `Serializable` future); they all live in the importer change, not here.
