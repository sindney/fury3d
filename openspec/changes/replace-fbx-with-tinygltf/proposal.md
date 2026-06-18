## Why

The Fury3D engine carries a hard dependency on the Autodesk FBX SDK for model import (`FbxParser`, gated by `_FURY_FBXPARSER_IMP_`), plus an in-tree partial glTF 2.0 JSON DOM (`GLTFDom`) that never reached scene-graph build-out. The architecture doc (§1, §15) commits to dropping FBX and adopting glTF 2.0 via the vendored `tinygltf` as the canonical asset format. This change executes the **first half** of that commitment — remove the FBX SDK coupling, vendor `tinygltf`, retire `GLTFDom` — and verifies the engine still compiles, links, and runs the existing JSON/LZ4 demo scene end-to-end. The actual `GltfImporter` (walking `tinygltf::Model` into engine types) is **out of scope here** and lands in a follow-up change.

**Scope expanded mid-implementation**: when the build was attempted on a fresh Apple Silicon host (Darwin 24.6, AppleClang 16), it failed because the previously-system-installed dependencies (SFML 2.x at `/usr/local/lib`, rapidjson at `/usr/local/include`) were not present, and the master branch carried several pre-existing source bugs that newer compilers reject. To meet the user's stated acceptance bar ("compile the project to make sure compiling is working"), the change now also vendors **SFML 3.1.0** and **rapidjson v1.1.0** as submodules, ports the engine from SFML 2 to SFML 3 (89 call sites across 5 files), bumps to C++17, and fixes 5 pre-existing rvalue-address bugs plus one CMake typo. Section 10 of `tasks.md` records each of these for the archive.

## What Changes

- **BREAKING**: Remove `FbxParser.{h,cpp}` and the `_FURY_FBXPARSER_IMP_` build option. The engine no longer imports FBX directly.
- **BREAKING**: Remove `GLTFDom.{h,cpp}` and its `FileUtil::PrivateLoadGLTFFile` / `FileUtil::AccessBuffer` glue. Superseded by `tinygltf`.
- **BREAKING**: Drop the `friend class FbxParser` declaration in `Mesh.h` and the `FbxParser::Initialize()` call in `Engine.cpp`.
- **BREAKING**: Drop `Fury.h` umbrella include of `FbxParser.h`.
- **BREAKING**: Engine and demo now require **C++17** (was C++11). SFML 3 mandates this.
- **BREAKING**: Engine no longer accepts system-installed SFML/rapidjson paths via `SFML_INCLUDE`/`SFML_LIB`/`RAPIDJSON_INCLUDE` cache variables. All three deps are now vendored as submodules.
- **BREAKING**: SFML public API surface changed from 2.x to 3.x — downstream code that uses `Engine::HandleEvent`, `InputUtil`, or the `sf::Keyboard::Key`/`sf::Mouse::Button` signals must update their enum spellings (`sf::Keyboard::A` → `sf::Keyboard::Key::A`) and event handling (variant-based `event.is<T>()` instead of `switch (event.type)`).
- **BREAKING**: `examples/CMakeLists.txt` no longer takes `FURY3D_INCLUDE` / `FURY3D_LIB` cache vars — it `add_subdirectory`'s the engine directly.
- Remove `FBXSDK_INCLUDE` / `FBXSDK_LIB_*` CMake variables and `${FBXSDK_LIB}` link entries from both `engine/CMakeLists.txt` and `examples/CMakeLists.txt`.
- Vendor `tinygltf` as a **git submodule** under `engine/ThirdParty/tinygltf/`, pinned to release tag `v2.9.7`. v3.0.0 is a major API rewrite and is **not** adopted in this change.
- Vendor `SFML` as a git submodule under `engine/ThirdParty/SFML/`, pinned to tag `3.1.0`. Built from source via `add_subdirectory`; only `SFML::Window`/`SFML::System`/`SFML::Graphics` modules are enabled (audio + network are off).
- Vendor `rapidjson` as a git submodule under `engine/ThirdParty/rapidjson/`, pinned to tag `v1.1.0`. Header-only.
- Add `engine/ThirdParty/tinygltf/` to the engine include path. Compile `tiny_gltf.cc` into the `fury` library (with `TINYGLTF_NO_STB_IMAGE` / `TINYGLTF_NO_STB_IMAGE_WRITE` to avoid stb_image ODR collision with the engine's existing STB usage).
- Bump `cmake_minimum_required` to 3.22 (SFML 3 requires it). Replace `-std=c++11` with `CMAKE_CXX_STANDARD 17`.
- Port the engine event-handling and input layers from SFML 2 to SFML 3 (variant-based `sf::Event`, scoped enums, `sf::State::Windowed` separate from `sf::Style::Default`, `pollEvent` returning `std::optional<sf::Event>`, `sf::Int32` removal, etc.). Affects `Engine.{h,cpp}`, `InputUtil.{h,cpp}`, `Gui.cpp`, `Demo.cpp`.
- Fix pre-existing rvalue-address bugs at `Shader.cpp:372-373` and `Pipeline.cpp:430,509,598,672` (taking `&` of a member of an rvalue-returned `Matrix4`). AppleClang 16 with C++17 enforces this; older compilers tolerated it.
- Fix pre-existing CMake bug: `-NDEBUG` → `-DNDEBUG` in the engine's release-flags line.
- Verify the build: `examples/Demo.cpp` continues to load `Resource/Scene/scene.bin` via `FileUtil::LoadCompressedFile`, the project compiles via the existing CMake on macOS, links cleanly without the FBX SDK installed, and runs end-to-end with zero `EROR`-level log entries.

Non-goals (deferred):
- Implementing `GltfImporter` (walking `tinygltf::Model` → engine `SceneNode` / `Mesh` / `Material` / `Joint` / `AnimationClip`).
- Switching the demo to load a `.glb` asset.
- ANGLE / GLES retargeting (separate change).
- PBR material variant, >4 bone influences, seconds-based animation timing — all called out as open questions in ARCHITECTURE.md §15.
- Cleaning up the `-Wdelete-non-abstract-non-virtual-dtor` warnings in `Uniform<T,N>` (pre-existing, runtime-harmless).

## Capabilities

### New Capabilities
- `gltf-loader`: Vendoring of `tinygltf` (single-header glTF 2.0 loader) into the engine ThirdParty tree, exposed as a header dependency that future engine code (the deferred `GltfImporter`) can link against. This capability is the **integration**, not the importer — it covers what `tinygltf` is, where it lives, how it's pinned, and how it's wired into the build. The same spec also covers the FBX SDK / `GLTFDom` removals and the supporting build-system work (SFML 3 submodule, rapidjson submodule, C++17 bump) that became necessary to make the change verifiable.

### Modified Capabilities
<!-- None. There are no pre-existing specs in openspec/specs/. The current FBX/GLTFDom code is not spec-described, so its removal is not a spec delta — it's covered by the proposal's "What Changes" and tracked in tasks. -->

## Impact

- **Code removed**: `engine/Fury/FbxParser.{h,cpp}`, `engine/Fury/GLTFDom.{h,cpp}`, plus include / friend / init references in `engine/Fury/Engine.cpp`, `engine/Fury/Fury.h`, `engine/Fury/Mesh.h`, `engine/Fury/FileUtil.{h,cpp}`.
- **Code modified**: `engine/Fury/Engine.{h,cpp}`, `Gui.cpp`, `InputUtil.{h,cpp}`, `Pipeline.cpp`, `Shader.cpp`, `examples/Demo.cpp` — all touched by the SFML 2 → SFML 3 port and/or the rvalue-address fixes.
- **Build system**: `engine/CMakeLists.txt` and `examples/CMakeLists.txt` rewritten — drop the FBX SDK option/include/link blocks, drop the system-path cache vars, gain `add_subdirectory` of vendored SFML and tinygltf source compilation, gain `cmake_minimum_required(3.22)` and C++17.
- **Repo layout**: New submodules at `engine/ThirdParty/tinygltf` (v2.9.7), `engine/ThirdParty/SFML` (3.1.0), `engine/ThirdParty/rapidjson` (v1.1.0); new `.gitmodules` at repo root.
- **External dependencies**: FBX SDK is no longer required at build or run time. SFML and rapidjson no longer need to be installed system-wide. No new system libraries.
- **Demo runtime**: Functionally unchanged. `Demo.cpp` continues to load `scene.bin` via the existing JSON/LZ4 path; the rendering pipeline produces the same scene.
- **Downstream**: Anyone who was building with `FBXPARSER_IMP=ON` must stop setting it; anyone cloning the repo must run `git submodule update --init --recursive` (now pulls 3 submodules). Anyone using the engine's public SFML-typed API must port to SFML 3 enum spellings and event API.
- **Architecture doc**: ARCHITECTURE.md §1, §8.1, §8.2, §13.3, §15 already anticipate this removal — no doc update needed in this change beyond a note that the FBX/GLTFDom code is now gone (deferred to the importer change so docs reflect end-state).
