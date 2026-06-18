## ADDED Requirements

### Requirement: tinygltf SHALL be vendored as a pinned git submodule under engine/ThirdParty

The engine SHALL include `tinygltf` (single-header glTF 2.0 loader) at `engine/ThirdParty/tinygltf/`, registered as a git submodule pointing at the upstream `syoyo/tinygltf` repository, pinned to release tag `v2.9.7`. The submodule pin MUST be recorded in a top-level `.gitmodules` file. Version `v3.0.0` is a major API rewrite and SHALL NOT be selected.

#### Scenario: Fresh clone resolves the submodule at the pinned tag
- **WHEN** a developer clones the fury3d repository and runs `git submodule update --init --recursive`
- **THEN** `engine/ThirdParty/tinygltf/tiny_gltf.h` exists on disk
- **AND** `git -C engine/ThirdParty/tinygltf describe --tags --exact-match` resolves to `v2.9.7`

#### Scenario: .gitmodules captures the submodule
- **WHEN** the repository is inspected
- **THEN** `.gitmodules` at the repo root contains an entry whose `path` is `engine/ThirdParty/tinygltf` and whose `url` points at the upstream `tinygltf` repository

### Requirement: Engine CMake SHALL compile tinygltf into the fury library

The engine `CMakeLists.txt` SHALL add `engine/ThirdParty/tinygltf/` to the include path and SHALL include `engine/ThirdParty/tinygltf/tiny_gltf.cc` in the `fury` library's source list, so that `tinygltf` symbols are linked into `libfury` and available to engine code that includes `tiny_gltf.h`.

#### Scenario: Engine library exposes tinygltf symbols
- **WHEN** `libfury` is built and a translation unit in the engine includes `<tiny_gltf.h>` and references `tinygltf::TinyGLTF`
- **THEN** the build links without unresolved-symbol errors against `tinygltf::TinyGLTF::LoadASCIIFromFile` and `tinygltf::TinyGLTF::LoadBinaryFromFile`

#### Scenario: Demo executable links tinygltf transitively via fury
- **WHEN** `examples/Demo.cpp` is compiled and linked against `libfury`
- **THEN** the link succeeds without requiring `examples/CMakeLists.txt` to add tinygltf paths directly

### Requirement: The engine SHALL NOT depend on the FBX SDK

The engine MUST build, link, and run without any FBX SDK headers or libraries present on the host system. The `FBXPARSER_IMP` CMake option, the `_FURY_FBXPARSER_IMP_` preprocessor define, all `FBXSDK_*` CMake cache variables, all `${FBXSDK_LIB}` link entries, the `FbxParser.{h,cpp}` source files, and every reference to the `FbxParser` symbol SHALL be removed from `engine/CMakeLists.txt`, `examples/CMakeLists.txt`, `engine/Fury/Engine.cpp`, `engine/Fury/Fury.h`, and `engine/Fury/Mesh.h`.

#### Scenario: CMake configure succeeds with no FBX SDK installed
- **WHEN** a developer runs `cmake -S engine -B build` on a host that has no FBX SDK installed
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

The partial in-tree glTF 2.0 DOM (`engine/Fury/GLTFDom.{h,cpp}`) SHALL be deleted. The two helpers in `engine/Fury/FileUtil` that depended on it (`PrivateLoadGLTFFile` and the `AccessBuffer` template) SHALL be removed. No engine source file SHALL retain a `#include "Fury/GLTFDom.h"` or a forward declaration of class `GLTFDom`.

#### Scenario: GLTFDom files are gone
- **WHEN** the working tree is inspected
- **THEN** `engine/Fury/GLTFDom.h` and `engine/Fury/GLTFDom.cpp` do not exist
- **AND** grep for `GLTFDom` across `engine/Fury/` returns no matches

#### Scenario: FileUtil compiles without GLTFDom helpers
- **WHEN** `engine/Fury/FileUtil.cpp` is compiled
- **THEN** the build succeeds with `PrivateLoadGLTFFile` and `AccessBuffer` removed from the public and private interfaces of `FileUtil`

### Requirement: Demo SHALL compile and run unchanged after FBX removal

`examples/Demo.cpp` SHALL continue to load `Resource/Scene/scene.bin` via `FileUtil::LoadCompressedFile` and execute the existing `PrelightPipeline` deferred renderer without source-level modification. The runtime behavior SHALL be observably identical to the pre-change baseline (same scene visible, same FPS envelope, no new log errors at startup).

#### Scenario: Demo source untouched
- **WHEN** the working tree is diffed against the pre-change baseline
- **THEN** `examples/Demo.cpp` shows zero modifications

#### Scenario: Demo compiles via existing CMake on macOS
- **WHEN** a developer runs `cmake -S engine -B build-engine && cmake --build build-engine` followed by `cmake -S examples -B build-demo -DFURY3D_INCLUDE=<engine src> -DFURY3D_LIB=<lib path> && cmake --build build-demo` on macOS Darwin 24.x
- **THEN** both `libfury` and the `demo` executable build successfully without warnings escalating to errors

#### Scenario: Demo runs and renders the existing scene
- **WHEN** the built `demo` binary is launched from `examples/bin/`
- **THEN** the SFML window opens, `scene.bin` loads, the deferred lighting pipeline executes, and no `EROR`-level entries appear in `Log.txt` originating from FBX or `GLTFDom` paths

- **THEN** zero `error: cannot take the address of an rvalue` errors are emitted from `Shader.cpp` or `Pipeline.cpp`
