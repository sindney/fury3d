![](https://img.shields.io/badge/dev-v0.2.1-green.svg) ![](https://img.shields.io/badge/build-passing-green.svg) ![](https://img.shields.io/badge/license-MIT-blue.svg)

# Fury3D

[中文简体](README.ZH-CN.md)

## Introduction

Fury3D is a cross-platform rendering engine written in C++17 and OpenGL, with a Lua-driven editor, JSON-configurable rendering pipelines, and a data-driven post-process chain.

Picked up the development with the help of Kimi K3, MiniMax M3, GLM 5.2 with Claude Code. 

> Start with M3 and GLM, then mainly K3, since it's multimodal and easier to buy than GLM and is really powerful.

Works on Windows and macOS. Designed for study — the editor, importers, and shaders are all small enough to read end-to-end.

### Highlights

* **OpenGL** (3.3+ core profile) with explicit per-draw state (Vulkan-PSO-shaped) so chain + debug draws never leak `GL_BLEND` into unrelated passes.
* **C++17** smart pointers, **`sol2 + Lua 5.4`** scripting bridge.
* **JSON-configurable pipelines** — every pass, every shader variant, every uniform lives in a `.json` next to the runtime.
* **Editor (ImGui + ImGuizmo)** — Scene Inspector, Content Browser, Node Properties, viewport gizmo, native file dialogs, configurable dock layout, persisted across launches.
* **CLI** — `fury convert gltf|fbx|scene` translates assets to the runtime form (json or LZ4-compressed bin) with `--auto-scale` for centimetre-native meshes.
* **glTF 2.0 importer** — vendored `tinygltf`, with alpha-mode (OPAQUE/MASK/BLEND) and KHR_materials_transmission mapping. FBX input is chained through `FBX2glTF` + the glTF importer.
* **HDR rendering pipeline** — `RGBA16F` intermediate composite, ACES tonemap, automatic chain ordering.
* **Data-driven post-process chain** — SSAO, SSR, FXAA, CRT, ACES. Effects declare a `stage` (`pre_tonemap`/`tonemap`/`post_tonemap`); chain runs in canonical order with auto-tonemap injection. Per-effect uniform overrides via the Settings panel.
* **Transparent rendering** — back-to-front alpha-blended base + per-light additive draws, depth-write off, premultiplied-alpha forward shader.
* **Skinned meshes + animations** — glTF skinning, joint TRS-driven bone matrices; `Animator` component with state weights / speeds / wraps / cross-fades; Console overlay for joint debugging.
* **Cascaded shadow maps** for the sun, plus standard spot/point shadow maps.
* **OcTree spatial** with default-cube auto-wrap and Profiler Spatial section.
* **LZ4-compressed scene format** (`.bin`) — the editor's default save form; `.json` available for human-readable debugging.

## Screenshots

![HDR PBR tank fleet with postprocess chain (SSAO, SSR, ACES, FXAA, CRT) — Settings panel on the right showing per-effect toggles and Edit dialog; Profiler GBuffer tabs below.](screenshots/tank.png)

*Tank fleet rendered with HDR PBR + cascaded shadow maps + the full postprocess chain. Settings panel toggles each effect and opens per-effect uniform-override dialogs; Profiler window shows the GBuffer's normal + diffuse blits.*

![SSR reflection of trees and a wooden fence on a wet outdoor ground — postprocess chain visible to the right (SSAO, SSR, ACES, FXAA, CRT).](screenshots/reflection.png)

*SSR screen-space reflections: water-like ground roughness 0.05 reflects trees + fence in real time. Visible in the same Profiler/Chain layout.*

![Sponza HDR scene rendered with HDR prelight pipeline + full postprocess chain (SSAO, SSR, ACES, FXAA, CRT).](screenshots/sponza.png)

*Khronos Sponza at HDR — the canonical chain runs across the large texture variety. Composite pre-tonemap HDR panel previews the un-tonemapped accum + the sRGB-encode terminal.*

![James skinned character with skeleton overlay (joints labelled) and Animator state inspector on the right (States, Play / Stop / Rewind / CrossFade).](screenshots/skin.png)

*Skinned mesh + Animator component: skin deformation driven by joint TRS, with the joint-debug overlay drawing bones from the selected mesh's joint hierarchy and the Animator inspector exposing state, weight, speed, layer, wrap mode, scrub, cross-fade.*

## Compatibility

Tested compilers:

* AppleClang 16 (macOS Darwin 24.x)
* GCC 9+ / Clang 10+ on Linux
* MSVC 16+ (Visual Studio 2019+) on Windows

Should work with any GPU that supports OpenGL 3.3+.

Vendored dependencies (git submodules under `engine/ThirdParty/`):

* SFML 3.1.0 — window/input/context.
* rapidjson 1.1.0 — JSON serialisation.
* Lua 5.4.7 + sol2 v3.5.0 — scripting bridge.
* tinygltf v2.9.7 — glTF 2.0 loader.
* meshoptimizer — mesh optimisation / simplification / LOD chain.
* nativefiledialog-extended (nfd) — platform-native file dialogs (editor-only).
* LZ4, STB image, ImGui, ImGuizmo, ImReflect — bundled in-tree.

No FBX SDK requirement.

## Building

Fury3D uses CMake (≥ 3.22) and vendors every third-party dependency as a git submodule under `engine/ThirdParty/`. After cloning, initialise the submodules once:

```sh
git clone https://github.com/sindney/fury3d
cd fury3d
git submodule update --init --recursive
```

`--recursive` is required because some submodules have their own submodules (nfd vendors `wayland-protocols` for its Linux path, which is inert on macOS/Windows). For an existing clone, the same command updates submodules to the pinned commits.

Then configure and build:

```sh
cmake -S engine -B build-engine
cmake --build build-engine -j
```

The `fury` (runtime) and `furye` (runtime + editor) executables are emitted directly into `examples/` next to `Editor.lua` and `Resource/`, so launching is just:

```sh
cd examples && ./furye Editor.lua
```

A non-editor build (smaller binary, no ImGui editor windows, no nfd dependency) builds `fury` only:

```sh
cmake -S engine -B build-engine -DWITH_EDITOR=OFF
cmake --build build-engine -j
```

### Build options

| Option            | Default | Effect                                                                 |
| ----------------- | ------- | ---------------------------------------------------------------------- |
| `WITH_EDITOR`     | `ON`    | Builds the C++ editor shell (Scene Inspector, Content Browser, gizmo, native file dialogs). Requires `GUI_IMP=ON`. With `OFF`, only `fury` is built. |
| `GUI_IMP`         | `ON`    | Compiles the ImGui overlay (HUD, debug widgets). Required by `WITH_EDITOR`. |
| `BUILD_SHARED_LIBS` | `OFF` | Builds `fury` / `furye` as executables (default). `ON` produces `libfury.{dylib,so,dll}` for embedding — the Lua launcher `main` is then excluded. |
| `EXPORT_DLL` (Windows) | `ON` | Defines `FURY_API_EXPORT` so the shared lib exports symbols. Ignored for static builds. |
| `FURY_WITH_TRACY` | `ON` for Debug / RelWithDebInfo / multi-config, `OFF` for Release / MinSizeRel | Instruments the engine with the Tracy profiler (vendored at `engine/ThirdParty/tracy`). Also builds `tracy-profiler` + `tracy-capture` into `examples/`. See [docs/TRACY.md](docs/TRACY.md). |
| `FURY_WITH_TRACY_GPU` | `ON` when Tracy is on | Adds OpenGL timer-query GPU zones. Unreliable on Apple/TBDR drivers — turn off per machine if so. |

### Adding a vendored dependency

Each submodule under `engine/ThirdParty/` is wired the same way: a configure-time presence check that fails with the right git command, then either `add_subdirectory(...)` (if upstream ships CMake) or a flat `file(GLOB)` static lib (if it doesn't). Editor-only deps sit behind `if(WITH_EDITOR)`. nfd's macOS backend compiles `nfd_cocoa.m`, so `engine/CMakeLists.txt` declares `LANGUAGES C CXX OBJC` on Apple.

## Run the demo / editor

```sh
cd examples && ./furye Editor.lua
```

Editor.lua picks the startup scene by argv:

* No argv → `Projects/tank/scene.bin` (the bundled tank scene).
* `./furye Editor.lua outdoor.fbx` → loads `outdoor.fbx` (or its `Projects/`, `Resource/Scene/` prefixed variants) via the glTF importer.

If you only built `fury`, the launch is the same (`./fury Editor.lua`) but the ImGui editor windows are absent — drop the `.lua` to run your own script.

### Controls

* **WASD** / **arrows** — move along the camera's forward / strafe.
* **Hold left mouse button + drag** — yaw and pitch.
* **Mouse wheel** — adjust move speed.
* **LShift** — 5× speed multiplier.
* **F2** or right-click → **Rename** — rename a selected node.
* **Double-click a node** in the Scene Inspector — frame the camera on that node (matches the framed whole-scene pose used for auto-focus on File→Open).
* **File → Open...** / **Ctrl+O** — native single-select open dialog (any engine-loadable format: `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`). On open, the editor auto-scales the import (cm→m), runs auto-focus, and the camera is at the framed pose before the next input event fires.
* **File → Import...** / **Ctrl+Shift+I** — native multi-select open dialog (merges into the active scene).
* **File → Save As...** / **Ctrl+Shift+S** — native OS save dialog.
* **Menu bar** — `File`, `Edit`, `Window`, plus the project-emitted `Camera` menu.
* **Settings → Editor** — `Snap Enabled`, snap-step sizes, **`Auto-Focus on Open`** (default ON; toggled via the `auto_focus` editor setting in `imgui.ini`). Camera controls (Move Speed, Mouse Sensitivity).

### Editor windows

| Window              | What it does                                                                   |
| ------------------- | ------------------------------------------------------------------------------ |
| **Scene Inspector** | Tree of nodes, drag-drop reparent, leaf double-click frames camera.            |
| **Viewport**        | Render surface + toolbar (gizmo mode, debug-view, overlay toggles).            |
| **Node Properties** | Mode switch (Local/World), transform (the root node is locked to identity — non-identity there shrinks the gizmo), MeshRender material slots, Animator state inspector. |
| **Content Browser** | In-scene tiles (Mesh, Material, Texture, AnimationClip), type filter, fuzzy search, F2 / right-click rename, double-click opens the asset editor. |
| **Settings**        | RenderSettings (HDR toggle, CSM, postprocess chain per-effect enabled + Edit dialog for uniform overrides), Camera controls, import flags (auto-scale, normal gen, mesh weld). |
| **Profiler**        | FPS sparkline, draw-call / triangle counts, cascaded-light shadow map viewer, GBuffer texture previews. |
| **Console**         | `FURY_INFO`/`WARN`/`ERROR` log buffer + ad-hoc Lua eval (errors caught inline). |

## CLI

The same `fury` binary hosts an offline asset CLI when `argv[1]` is a known subcommand; no window, no GL context, no Lua VM unless `fury exec` is used.

```sh
cd examples
./fury help
./fury convert gltf  character.glb character.bin          # engine runtime form
./fury convert fbx   tank.fbx tank.bin --auto-scale       # FBX -> glTF -> engine; auto-scales cm-native meshes
./fury convert scene tank.bin tank.json                    # round-trip bin <-> json
./fury info tank.bin                                      # CPU-side summary (nodes, meshes, AABB...)
./fury exec tank.bin some-script.lua                       # headless Lua over a scene
./fury render-mesh tank.bin Tank_005 /tmp/tank.png         # single-mesh thumbnail
```

Run `./fury help <subcommand>` for full flags; see `docs/CLI.md` for the reference.

## Special thanks

* [Rapidjson](https://github.com/miloyip/rapidjson) — loading pipeline / scene JSON
* [Plog](https://github.com/SergiusTheBest/plog) — log implementation
* [ThreadPool](https://github.com/progschj/ThreadPool) — threadpool implementation
* [Stb_image](https://github.com/nothings/stb) — image loading
* [LZ4](https://github.com/Cyan4973/lz4) — scene file compression/decompression
* [SFML](http://www.sfml-dev.org) — OS window/input/context
* [tinygltf](https://github.com/syoyo/tinygltf) — glTF 2.0 loader
* [FBX2glTF](https://github.com/facebookincubator/FBX2glTF) — FBX → glTF (precompiled binaries vendored per platform)
* [meshoptimizer](https://github.com/zeux/meshoptimizer) — mesh optimization + LOD chain
* [Lua](https://www.lua.org) + [sol2](https://github.com/ThePhD/sol2) — scripting bridge
* [Ogre3d](http://www.ogre3d.com) — octree implementation reference
* [ImGui](https://github.com/ocornut/imgui) + [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) + [ImReflect](https://github.com/CedricGuillemet/ImReflect) — editor UI / gizmo / reflected inspector
* [nativefiledialog-extended](https://github.com/samhocevar/nativefiledialog) — cross-platform file dialogs
* [RenderDoc](https://github.com/baldurk/renderdoc) — OpenGL debugging
* [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) - Sample assets
