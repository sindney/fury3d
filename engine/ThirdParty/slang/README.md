# Slang shader compiler

Pinned prebuilt binaries of the Slang shader compiler, used by the engine's
shader compile pipeline at runtime.

- Version: v2026.19 (pin is documented here; bump all platforms together)
- Source: https://github.com/shader-slang/slang/releases/tag/v2026.19
  - `slang-2026.19-macos-aarch64.tar.gz`
  - `slang-2026.19-windows-x86_64.zip`
  - `slang-2026.19-linux-x86_64-glibc-2.27.tar.gz`
- License: MIT (see upstream LICENSE)

## Fetching

The platform payloads are not in git. CMake downloads the pinned release at
configure time when the local copy is missing or holds another version:

```
cmake -S . -B build          # fetches ThirdParty/slang/<platform>/ if needed
```

`tools/cmake/FetchPinnedArchive.cmake` does the work: it writes a
`.pinned-version` marker into the payload dir, and re-fetches whenever that
marker is missing or names a different version. A matching marker means no
network access. `-DFURY_WITH_SLANG=OFF` skips the fetch and the staging
entirely (the Metal/Vulkan shader cross-compile then has no front end).

## Layout

`<platform>/lib/` holds the runtime shared libraries, `<platform>/include/`
the public headers:

- `macos-arm64/`: dylibs + headers, extracted as-is from the macOS tarball.
- `windows-x86_64/`: DLLs + import libs (.lib) + headers. Upstream ships
  DLLs in bin/ and import libs in lib/.
- `linux-x86_64/`: shared objects + headers. The glibc-2.27 package ships
  no libslang-llvm (LLVM is not built into this variant).

libslang loads its siblings (compiler, llvm, glslang, glsl-module, rt, gfx)
at runtime by name, so every file in a platform's lib/ must be staged
together next to the engine binary. The macOS/Linux `libslang.so`/`.dylib`
symlink points at the versioned `libslang-compiler` file; do not thin the
set.

## Updating

1. Bump `FURY_SLANG_VERSION` and the archive names in `engine/CMakeLists.txt`.
2. Update the version pin and the upstream URLs above.
3. Delete the local payload dir (or its `.pinned-version` marker) and
   re-configure to refetch.
