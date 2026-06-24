## ADDED Requirements

### Requirement: tinygltf SHALL be vendored as a pinned git submodule under engine/ThirdParty

The engine SHALL include `tinygltf` (single-header glTF 2.0 loader) at `engine/ThirdParty/tinygltf/`, registered as a git submodule pointing at the upstream `syoyo/tinygltf` repository, pinned to release tag `v2.9.7`. The submodule pin MUST be recorded in a top-level `.gitmodules` file. Version `v3.0.0` is a major API rewrite and SHALL NOT be selected.

#### Scenario: Fresh clone resolves the submodule at the pinned tag
- **WHEN** a developer clones the fury3d repository and runs `git submodule update --init --recursive`
- **THEN** `engine/ThirdParty/tinygltf/tiny_gltf.h` exists on disk
- **AND** `git -C engine/ThirdParty/tinygltf rev-parse HEAD` resolves to the same SHA as `git -C engine/ThirdParty/tinygltf rev-parse v2.9.7`

#### Scenario: .gitmodules captures the submodule
- **WHEN** the repository is inspected
- **THEN** `.gitmodules` at the repo root contains an entry whose `path` is `engine/ThirdParty/tinygltf` and whose `url` points at the upstream `tinygltf` repository

### Requirement: Engine CMake SHALL compile tinygltf into the fury library

The engine `CMakeLists.txt` SHALL add `engine/ThirdParty/tinygltf/` to the include path and SHALL include `engine/ThirdParty/tinygltf/tiny_gltf.cc` in the `fury` library's source list, so that `tinygltf` symbols are linked into `libfury` and available to engine code that includes `tiny_gltf.h`. The compilation of `tiny_gltf.cc` SHALL set `TINYGLTF_NO_STB_IMAGE` and `TINYGLTF_NO_STB_IMAGE_WRITE` to avoid an ODR collision with the engine's existing vendored STB at `engine/ThirdParty/STB/`.

#### Scenario: Engine library exposes tinygltf symbols
- **WHEN** `libfury` is built
- **THEN** symbols of the form `tinygltf::TinyGLTF::Load*` appear in the resulting library (verifiable via `nm libfury.dylib | c++filt | grep tinygltf::TinyGLTF`)

#### Scenario: No duplicate stb_image symbols
- **WHEN** `libfury` is linked
- **THEN** `nm libfury.dylib | grep stbi_load` reports at most one definition per symbol — i.e. tinygltf does not embed its own copy

### Requirement: The engine SHALL NOT depend on the FBX SDK

The engine MUST build, link, and run without any FBX SDK headers or libraries present on the host system. The `FBXPARSER_IMP` CMake option, the `_FURY_FBXPARSER_IMP_` preprocessor define, all `FBXSDK_*` CMake cache variables, all `${FBXSDK_LIB}` link entries, the `FbxParser.{h,cpp}` source files, and every reference to the `FbxParser` symbol SHALL be removed from `engine/CMakeLists.txt`, `examples/CMakeLists.txt`, `engine/Fury/Engine.cpp`, `engine/Fury/Fury.h`, and `engine/Fury/Mesh.h`.

#### Scenario: CMake configure succeeds with no FBX SDK installed
- **WHEN** a developer runs `cmake -S engine -B build-engine` on a host that has no FBX SDK installed
- **THEN** configure completes without error
- **AND** the generated build files contain no reference to FBX SDK headers or libraries

#### Scenario: Build option no longer exists
- **WHEN** the project is configured
- **THEN** the `FBXPARSER_IMP` CMake option is not declared in `engine/CMakeLists.txt` or `examples/CMakeLists.txt`
- **AND** grep for `_FURY_FBXPARSER_IMP_` across `engine/Fury/`, `engine/CMakeLists.txt`, and `examples/CMakeLists.txt` returns no matches

#### Scenario: FBX symbols absent from compiled engine
- **WHEN** `libfury` is built
- **THEN** `nm libfury.{dylib,so,a} | grep -i fbx` produces no engine-defined `FbxParser` symbols (FBX-named symbols originating in `tinygltf` itself, if any, are out of scope)

### Requirement: GLTFDom SHALL be removed in favor of tinygltf

The partial in-tree glTF 2.0 DOM (`engine/Fury/GLTFDom.{h,cpp}`) SHALL be deleted. The helpers in `engine/Fury/FileUtil` that depended on it (`LoadGLTFFile`, `PrivateLoadGLTFFile`, the `AccessBuffer` template, `ResizeDataBuffer`, `DeleteDataBuffers`, and the static buffer fields `m_UIntBuffer`/`m_UShortBuffer`/`m_UCharBuffer`/`m_FloatBuffer`/`m_ShortBuffer`/`m_CharBuffer` and their length counterparts) SHALL be removed. No engine source file SHALL retain a `#include "Fury/GLTFDom.h"` or a forward declaration of class `GLTFDom`.

#### Scenario: GLTFDom files are gone
- **WHEN** the working tree is inspected
- **THEN** `engine/Fury/GLTFDom.h` and `engine/Fury/GLTFDom.cpp` do not exist
- **AND** grep for `GLTFDom` across `engine/Fury/` returns no matches

#### Scenario: FileUtil compiles without GLTFDom helpers
- **WHEN** `engine/Fury/FileUtil.cpp` is compiled
- **THEN** the build succeeds with `LoadGLTFFile`/`PrivateLoadGLTFFile`/`AccessBuffer` removed from the public and private interfaces of `FileUtil`

### Requirement: Demo SHALL load scene.bin via the existing JSON/LZ4 path

`examples/Demo.cpp` SHALL continue to load `Resource/Scene/scene.bin` via `FileUtil::LoadCompressedFile` and execute the existing `PrelightPipeline` deferred renderer. The runtime behavior at the asset-loading layer SHALL be observably identical to the pre-change baseline (same scene visible, no new log errors originating from FBX or `GLTFDom` paths). The demo SHALL NOT call any `tinygltf` APIs in this change — that is reserved for the follow-up `GltfImporter` change. (Note: `Demo.cpp` source itself IS modified in this change for SFML 3 API differences — this requirement scopes the demo's *runtime asset path*, not its source-level invariance.)

#### Scenario: Demo loads scene.bin via FileUtil::LoadCompressedFile
- **WHEN** the built `demo` binary is launched from `examples/bin/`
- **THEN** the SFML window opens, `Resource/Scene/scene.bin` deserializes successfully, the deferred lighting pipeline executes, and no `EROR`-level entries appear in `Log.txt` originating from FBX or `GLTFDom` paths

#### Scenario: Demo does not call tinygltf
- **WHEN** the demo binary's symbol table is inspected
- **THEN** no call sites in `Demo.cpp` reference `tinygltf::*` symbols

### Requirement: SFML SHALL be vendored as a pinned git submodule

The engine SHALL include `SFML` at `engine/ThirdParty/SFML/`, registered as a git submodule pointing at the upstream `SFML/SFML` repository, pinned to release tag `3.1.0`. SFML SHALL be built from source via `add_subdirectory`. Only the `Window`, `System`, and `Graphics` modules SHALL be enabled (`Audio` and `Network` are explicitly disabled to keep the build small). The engine SHALL no longer accept a `SFML_INCLUDE` or `SFML_LIB` cache variable pointing at a system path.

#### Scenario: Fresh clone resolves the SFML submodule at the pinned tag
- **WHEN** a developer clones the fury3d repository and runs `git submodule update --init --recursive`
- **THEN** `engine/ThirdParty/SFML/CMakeLists.txt` exists on disk
- **AND** `git -C engine/ThirdParty/SFML rev-parse HEAD` resolves to the same SHA as `git -C engine/ThirdParty/SFML rev-parse 3.1.0`

#### Scenario: Engine links against modern CMake SFML targets
- **WHEN** the `fury` library is built
- **THEN** its `target_link_libraries` references `SFML::Window` and `SFML::System` (not the legacy `sfml-window`/`sfml-system` names)

### Requirement: rapidjson SHALL be vendored as a pinned git submodule

The engine SHALL include `rapidjson` at `engine/ThirdParty/rapidjson/`, registered as a git submodule pointing at the upstream `Tencent/rapidjson` repository, pinned to release tag `v1.1.0`. rapidjson is header-only; the engine SHALL add `engine/ThirdParty/rapidjson/include` to its include path. The engine SHALL no longer accept a `RAPIDJSON_INCLUDE` cache variable pointing at a system path.

#### Scenario: Fresh clone resolves the rapidjson submodule at the pinned tag
- **WHEN** a developer clones the fury3d repository and runs `git submodule update --init --recursive`
- **THEN** `engine/ThirdParty/rapidjson/include/rapidjson/document.h` exists on disk

#### Scenario: Engine compiles against vendored rapidjson with no system rapidjson installed
- **WHEN** the engine is built on a host with no rapidjson installed at `/usr/local/include` or `/opt/homebrew/include`
- **THEN** every `#include <rapidjson/...>` in `Serializable.cpp`, `FileUtil.cpp`, etc. resolves to the vendored copy

### Requirement: Engine and demo SHALL build against SFML 3.x

The engine SHALL compile cleanly against the SFML 3 API. Source code SHALL use the SFML 3 spellings: scoped `sf::Keyboard::Key::A` enums, scoped `sf::Mouse::Button::Left` enums, the namespace-scoped constants `sf::Keyboard::KeyCount` and `sf::Mouse::ButtonCount` (instead of the SFML-2 enum-member `Key::KeyCount` / `Button::ButtonCount`), `sf::State` for window state, `std::optional<sf::Event>`-returning `pollEvent`, and the `event.is<T>()` / `event.getIf<T>()` variant API for event dispatch. `Demo.cpp` SHALL use `std::int32_t` instead of the removed `sf::Int32`.

#### Scenario: Engine compiles with SFML 3 API
- **WHEN** the engine source is compiled
- **THEN** zero `error: no member named 'type' in 'sf::Event'` or similar SFML-2-style errors are emitted

#### Scenario: HandleEvent dispatches SFML 3 event variants
- **WHEN** `Engine::HandleEvent` is called with each of `sf::Event::Closed`, `Resized`, `FocusLost`, `FocusGained`, `TextEntered`, `KeyPressed`, `KeyReleased`, `MouseWheelScrolled`, `MouseButtonPressed`, `MouseButtonReleased`, `MouseMoved`, `MouseEntered`, `MouseLeft`
- **THEN** the corresponding `InputUtil` signal fires with the correct payload values extracted via `event.getIf<T>()`

### Requirement: Engine and demo SHALL build with C++17

The engine and demo CMake SHALL set `CMAKE_CXX_STANDARD 17` (was C++11). The minimum required CMake version SHALL be 3.22 (SFML 3 requires it). Compiler flags SHALL NOT contain the typo'd `-NDEBUG` form; the correct `-DNDEBUG` form SHALL be used in Release builds.

#### Scenario: CMake configure succeeds at C++17
- **WHEN** the project is configured on AppleClang 16 / GCC 9+ / MSVC 16+
- **THEN** the CMake configuration sets the compiler to C++17 mode (verifiable via `CMAKE_CXX_STANDARD 17` + `CMAKE_CXX_STANDARD_REQUIRED ON` in the engine `CMakeLists.txt`)

#### Scenario: Release build does not pass -NDEBUG to the compiler
- **WHEN** a Release build runs through Apple Clang
- **THEN** the compile commands contain `-DNDEBUG` (or no NDEBUG flag), never the malformed `-NDEBUG` that AppleClang rejects

### Requirement: Engine SHALL avoid taking the address of rvalue Matrix4 members

Engine code SHALL NOT use the pattern `&someExpressionReturningMatrix4ByValue.Raw[0]` (taking the address of a member of an rvalue), because AppleClang 16 with C++17 rejects it as ill-formed. Affected sites in `Shader.cpp` and `Pipeline.cpp` SHALL store the matrix in a named local first, then take the address of that local's `.Raw[0]`.

#### Scenario: Compile-time check
- **WHEN** the engine is compiled with AppleClang 16 (or any compiler enforcing C++17 rvalue address-of restrictions)
- **THEN** zero `error: cannot take the address of an rvalue` errors are emitted from `Shader.cpp` or `Pipeline.cpp`
