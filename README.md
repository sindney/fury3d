![](https://img.shields.io/badge/dev-v0.2.1-green.svg) ![](https://img.shields.io/badge/build-passing-green.svg) ![](https://img.shields.io/badge/license-MIT-blue.svg)

# Fury3D

[中文简体](README.ZH-CN.md)

## Introduction

Fury3d is a cross-platform rendering engine written in C++17 and modern OpenGL.

Works on Windows and macOS currently.

Please note, this is just a simple project for study purpose.

Features:

* Modern OpenGL (3.3+ core profile).
* C++17 smart pointers for memory management.
* Flexible Signal message system (member-function pointers, not lambdas).
* Lua scripting via sol2 + Lua 5.4 — drive the main loop from a `.lua` file.
* JSON-configurable rendering pipeline.
* Built-in light pre-pass deferred rendering pipeline.
* Integrated [ImGui](https://github.com/ocornut/imgui) for debug UI.
* Shadow mapping for directional, point, and spot lights (cascaded SM optional).
* Custom scene format — serialises to JSON or LZ4-compressed binary.

Plans:

* Add shadow maps. (Done, needs improvements.)
* Add skeleton animation support. (Done, needs improvements.)
* Implement glTF 2.0 importer using vendored `tinygltf`. (In progress — FBX SDK is already removed and `tinygltf` is vendored as a submodule; the runtime importer is the next change.)
* Implement HDR rendering pipeline and PBR materials.

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
* tinygltf v2.9.7 — glTF 2.0 loader (importer is the next change).
* LZ4, STB image, ImGui — bundled in-tree.

No FBX SDK requirement.

## Screenshots

![Shadows](screenshots/1.jpg)

![PolyScene](screenshots/2.jpg)

## Run the demo

```sh
git clone --recursive https://github.com/sindney/fury3d
cd fury3d
cmake -S engine -B build-engine
cmake --build build-engine --target fury -j
cp build-engine/fury examples/bin/fury        # one-time install
cd examples/bin && ./fury Demo.lua
```

Controls:

* **WASD** / **arrows** — move along the camera's forward / strafe.
* **Space** / **LControl** — move up / down.
* **Hold left mouse button + drag** — yaw and pitch.
* **Mouse wheel** — adjust move speed.
* **LShift** — 5× speed multiplier.
* **Menu bar** — `File → Quit`, `View → Profiler / GBuffer / Shadow Buffers`, `Camera → Settings`.

## Examples

The demo is now driven from `examples/Demo.lua`. A minimal scene + flythrough camera looks like this:

```lua
local function on_init()
    local octree = OcTree.Create(
        Vector4(-1000, -1000, -1000, 1),
        Vector4( 1000,  1000,  1000, 1),
        2)

    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))
    FileUtil.LoadSceneFromCompressedFile(
        Scene.GetActive(),
        FileUtil.GetAbsPath("Resource/Scene/scene.bin"))

    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1, 100)

    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0, 10, 25, 1))
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
end

local function on_update(dt)
    Gui.ShowDefault(dt)
    Gui.Render()
    Pipeline.GetActive():Execute(SceneManager.Instance())
end

Engine.run({ on_init = on_init, on_update = on_update })
```

See `docs/LUA.md` for the full bound API reference, and `examples/Demo.lua` for the WASD flythrough + Camera settings panel.

You can also configure the rendering pipeline via JSON — [see the example](https://github.com/sindney/fury3d/blob/master/examples/bin/Resource/Pipeline/DefferedLightingLambert.json).

## Special thanks

* [Rapidjson](https://github.com/miloyip/rapidjson) — loading pipeline / scene JSON
* [Plog](https://github.com/SergiusTheBest/plog) — log implementation
* [ThreadPool](https://github.com/progschj/ThreadPool) — threadpool implementation
* [Stb_image](https://github.com/nothings/stb) — image loading
* [LZ4](https://github.com/Cyan4973/lz4) — scene file compression/decompression
* [SFML](http://www.sfml-dev.org) — OS window/input/context
* [tinygltf](https://github.com/syoyo/tinygltf) — glTF 2.0 loader
* [Lua](https://www.lua.org) + [sol2](https://github.com/ThePhD/sol2) — scripting bridge
* [Ogre3d](http://www.ogre3d.org) — octree implementation reference
* [ImGui](https://github.com/ocornut/imgui) — debug GUI
* [RenderDoc](https://github.com/baldurk/renderdoc) — OpenGL debugging

## One more thing

If you use Sublime Text, you can try my [GLSLCompiler](https://github.com/sindney/GLSLCompiler) plugin to debug GLSL code :D
