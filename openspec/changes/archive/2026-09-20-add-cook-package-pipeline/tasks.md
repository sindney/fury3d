# add-cook-package-pipeline - tasks

## 1. Foundations

- [x] 1.1 Vendor a small single-file SHA-1 (engine/ThirdParty/) and add `HashUtil` (SHA-1 of bytes/file stream, hex encode) with a unit-style lua/exec smoke check
- [x] 1.2 Add `CanonicalPakKey()` helper (strip working-dir / `Engine/`-resource prefix -> canonical index key) shared by packager and runtime, with tests for the path forms in existing scenes (project-relative, `Engine/` prefix, absolute)
- [x] 1.3 Extend `TextureFormat` (Texture.h) with BC1/BC3/BC5/BC7/BC6H values and the GL internal-format mapping table

## 2. furye-cli binary

- [x] 2.1 Add `fury_add_target(furye-cli)` to engine/CMakeLists.txt with WITH_EDITOR sources + `FURY_HEADLESS_CLI` define, output to examples/
- [x] 2.2 Route furye-cli `main()` entirely through `Cli::Run` (no Lua launcher path; no-args prints help, exit 0)
- [x] 2.3 Verify existing subcommands (`convert`, `info`, `exec`) behave identically to `fury` counterparts (run one of each headless)

## 3. Cook step + DDC

- [x] 3.1 Implement DDC store (root resolution: `--ddc`/`FURY_DDC`/binary-adjacent `DDC/`; key = SHA1 of type+version+target+format+usage+source-hash; `DDC/<hex[0:2]>/<hex[2:4]>/<key>.ktx2` write/read/hit query)
- [x] 3.2 Implement cook asset discovery: headless scene load in furye-cli, enumerate path-keyed Texture entries + passthrough assets, unresolved-asset reporting + non-zero exit
- [x] 3.3 Implement texture usage inference (material slot -> normal map, .hdr/.exr -> HDR, sRGB+alpha -> color/alpha) with conflict warnings and optional `<scene>.cook.json` per-path overrides
- [x] 3.4 Vendor the ktx tool set: commit `ktx` + `libktx.4.dylib` (from KTX-Software 4.4.2 Darwin-arm64 pkg) into engine/ThirdParty/KTX-Software/4.4.2/mac-arm64/, CMake copies them next to furye-cli at build time, `FURY_KTX_CLI` env override
- [x] 3.5 Wire the ktx pipeline (`create --generate-mipmap --format <UNORM|SRGB>` -> `encode --codec uastc[-hdr-4x4]` -> `transcode --target <bcn>`), tool resolution (staged path, then env override), actionable error when missing; if ktx fails here STOP and consult the user before any basisu fallback
- [x] 3.6 Write cook manifest (canonical path -> ktx2/format/dims/mips, passthroughs, cook target) and add `cook --help`; host default legacy on macOS, modern elsewhere

## 4. Pak writer + package step

- [x] 4.1 Implement pak writer: data region append, 64 KB LZ4 block compression via vendored LZ4, store-if-not-smaller fallback, `--compression none`
- [x] 4.2 Implement sorted index + EOF footer (magic/version/index offset+size/index SHA-1), per-entry SHA-1, boot-entry record
- [x] 4.3 Implement `furye-cli package <scene>`: consume manifest (auto-cook if missing/stale), pack cooked + passthrough + scene boot entry, fail loudly + clean up partial output on missing artifacts

## 5. Pak reader + asset backend

- [x] 5.1 Implement `IAssetBackend` + `FileSystemBackend` (pass-through to current reads) and route `FileUtil::LoadString`/`LoadImage`/binary reads + `Texture::CreateFromImage` through it (canonical + resolved path pairs)
- [x] 5.2 Implement `PakBackend`: footer/index read + validation (magic, version, index hash), sorted-index lookup by `CanonicalPakKey`, block LZ4 decompress, per-entry SHA-1 integrity check
- [x] 5.3 Sibling-pak auto-mount when loading loose scene with `<scene>.pak` next to it; pak-wins logging when both forms present
- [x] 5.4 Loose-vs-pak equivalence test: same scene both ways, compare node/material/texture counts and resolved payload bytes

## 6. KTX2 runtime upload

- [x] 6.1 Implement the KTX2 reader (unsupercompressed subset: header, DFD vkFormat mapping, level index, smallest-first mip order) feeding `glCompressedTexImage2D` per mip inside `DispatchGL`; reject supercompressionScheme != 0 with a clear error; regular images keep existing path
- [x] 6.2 Unsupported-compressed-format fallback: log per-texture error, substitute missing-texture placeholder, scene load continues
- [x] 6.3 Headless guard: KTX2 parse works without GL (upload skipped), so `exec`/`info` on pak scenes stay GL-free
- [x] 6.4 Round-trip test: cook a texture, read the KTX2 back, assert format/dimensions/mip count before any GL work

## 7. fury pak boot

- [x] 7.1 `main()` `.pak` carve-out in fury (mount pak, load boot scene) per cli spec delta
- [x] 7.2 End-to-end: cook+package an example scene, `./fury scene.pak` renders it identically to loose (screenshot-diff via existing screenshot hooks)

## 8. Async loading thread

- [x] 8.1 Implement `AssetLoader`: bounded request queue {canonical path, priority, callback}, single worker thread doing backend read + decompress only (GL-thread guard clean)
- [x] 8.2 Main-thread completion pump wired into the frame loop and the exec/headless pump
- [x] 8.3 Route scene texture loads through AssetLoader with drain barrier before `Scene::Load` returns; tracy zones on queue depth + wait time
- [x] 8.4 Test: sync vs async byte-identical reads; scene load deterministic (existing lua tests pass unchanged)

## 9. Tests, ASAN, docs

- [x] 9.1 New tests/lua coverage: cook manifest shape, DDC hit/miss/recook-on-change, pak index round-trip, missing-artifact failure, sibling auto-mount, boot entry load
- [x] 9.2 Full tests/lua suite green via fury exec + furye-cli exec; build-asan run of cook/package/load cycle clean
- [x] 9.3 Update docs/CLI.md (furye-cli inventory, cook/package, pak boot) and regenerate docs/LUA_API.md if bindings were added
- [x] 9.4 USER visual verify: package ocean island scene, run `./fury ocean_island.pak`, confirm rendering matches editor

## 10. Post-verify follow-ups

- [x] 10.1 Move kraut + render-mesh subcommands from fury to furye-cli (dispatch gating, help texts, spec deltas, docs/CLI.md)
- [x] 10.2 furye GUI: File -> "Package..." menu item (after Save As, no shortcut) -> settings modal (compression/texture-target/output folder) -> furye-cli subprocess with modal progress dialog (dim overlay, scrolling log, cancel)
- [x] 10.3 USER visual verify of the GUI package flow end-to-end
