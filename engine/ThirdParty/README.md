# Third-party dependencies

Most dependencies are git submodules pinned to a specific upstream commit.

| Path | Upstream | Pin |
|---|---|---|
| `SFML` | https://github.com/SFML/SFML | `v3.1.0` |
| `rapidjson` | https://github.com/Tencent/rapidjson | `v1.1.0` |
| `tinygltf` | https://github.com/syoyo/tinygltf | `v2.8.1-166-g81bd50c` |
| `lua` | https://github.com/lua/lua | `v5.4-beta-326-g1ab3208a` |
| `sol2` | https://github.com/ThePhD/sol2 | `v3.5.0` |
| `nfd` | https://github.com/mlabbe/nativefiledialog-extended | `v1.3.0-8-g3cd252c` |
| `JoltPhysics` | https://github.com/jrouwe/JoltPhysics | `v5.6.0` |
| `tracy` | https://github.com/wolfpld/tracy | `v0.14.1` |
| `Kraut` | https://github.com/sindney/Kraut-CLI | `v0.0.1-6-gb2cb479` |
| `RmlUi` | https://github.com/mikke89/RmlUi | `6.1-186-gba95ffe8` |
| `metal-cpp` | https://github.com/apple/metal-cpp | `release/metal-cpp_macOS15_iOS18` |
| `SPIRV-Cross` | https://github.com/KhronosGroup/SPIRV-Cross | `vulkan-sdk-1.4.363.0` |
| `imgui-node-editor` | https://github.com/thedmd/imgui-node-editor | `v0.9.4` |
| `volk` | https://github.com/zeux/volk | `vulkan-sdk-1.4.363.0` |
| `Vulkan-Headers` | https://github.com/KhronosGroup/Vulkan-Headers | `v1.4.363` |
| `VulkanMemoryAllocator` | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | `v2.1.0-1054-g3aa9212` |
| `imgui_test_engine` | https://github.com/ocornut/imgui_test_engine | `v1.92.8` (`7be88e7fa267f251c7271a33d72b1aa5ca04a4cb`) |

`imgui_test_engine` is editor-automation only: `furye-ui-tests` compiles it,
and the production `fury` / `furye` / `furye-cli` targets contain no
test-engine code. Its library sources live in the submodule's
`imgui_test_engine/` subdirectory.

Initialize with:

```sh
git submodule update --init --recursive
```

`--recursive` is required because some submodules have their own submodules.

## Bundled in-tree

These are vendored as source rather than submodules:

- `ImGui` - Dear ImGui 1.92.8 docking, with local patches.
- `ImGuizmo`, `ImReflect` - editor widgets and reflection helpers.
- `STB` - image codec headers.
- `LZ4`, `SHA1`, `meshoptimizer` - small single-purpose libraries.
- `FBX2glTF` - pinned upstream release, fetched by CMake.
- `KTX-Software` - pinned upstream release, fetched by CMake.
- `slang` - pinned upstream release, fetched by CMake.
- `MoltenVK` - pinned upstream release, fetched by CMake.
