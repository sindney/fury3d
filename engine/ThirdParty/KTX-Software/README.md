# Vendored KTX-Software CLI

Pinned binaries of the Khronos `ktx` CLI, used by `furye-cli cook` to
block-compress textures (BC1/BC3/BC5/BC7/BC6H) into KTX2 files.

- Version: 4.4.2 (pin lives in engine/CMakeLists.txt as FURY_KTX_VERSION)
- Source: https://github.com/KhronosGroup/KTX-Software/releases/tag/v4.4.2
- License: Apache-2.0 (see upstream LICENSE)

## Layout

`<version>/<platform>/` holds the minimal runtime set:

- `mac-arm64/`: `ktx` + `libktx.4.dylib` (the exe links
  `@rpath/libktx.4.dylib`; its rpath includes `@executable_path`, so the
  dylib must sit next to the exe). Extracted from
  `KTX-Software-4.4.2-Darwin-arm64.pkg` via `pkgutil --expand-full`
  (tools pkg -> bin/ktx, library pkg -> lib/libktx.4.4.2.dylib renamed
  to libktx.4.dylib).
- `win-x64/`: `ktx.exe` + `ktx.dll`. The exe resolves `ktx.dll` from its
  own directory (Windows default DLL search order), so both must sit
  next to the exe. Extracted from the `KTX-Software-4.4.2-windows-x64`
  installer (bin/ktx.exe + bin/ktx.dll). The exe also imports the
  MSVC 2015+ redistributable (MSVCP140.dll, VCRUNTIME140.dll).
- `linux-x64/`, `mac-x64/`: committed on first need, same procedure as
  mac-arm64 above.

## Updating

1. Download the new release for the platform.
2. Replace the files under `<new-version>/<platform>/`.
3. Bump `FURY_KTX_VERSION` in engine/CMakeLists.txt.

The version string feeds DDC cache keys, so a bump re-cooks textures.
