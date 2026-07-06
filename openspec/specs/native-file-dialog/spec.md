# native-file-dialog

## Purpose

TBD

## Requirements

### Requirement: The build SHALL vendor `nativefiledialog-extended` as a git submodule under `engine/ThirdParty/nfd`
The repository SHALL include a git submodule at `engine/ThirdParty/nfd` pointing at the upstream `nativefiledialog-extended` repository (`https://github.com/btzy/nativefiledialog-extended.git`). The submodule SHALL be initialized by the standard `git submodule update --init --recursive` workflow that the other third-party submodules (SFML, rapidjson, tinygltf, lua, sol2) already require.

`engine/CMakeLists.txt` SHALL verify the submodule is present at configure time with a `if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/nfd/CMakeLists.txt") message(FATAL_ERROR ...)` check, matching the existing pattern used for the other submodules.

#### Scenario: Fresh clone builds after submodule init
- **WHEN** a user clones the fury3d repository and runs `git submodule update --init --recursive`
- **THEN** the `engine/ThirdParty/nfd/` directory contains nfd's `CMakeLists.txt`, `src/`, `include/`, and `LICENSE` files
- **AND** a subsequent `cmake` configure step succeeds without an "nfd submodule missing" error

#### Scenario: Missing submodule produces a clear error
- **WHEN** the user runs `cmake` without first initializing the `engine/ThirdParty/nfd` submodule
- **THEN** the configure step fails with a `FATAL_ERROR` message instructing the user to run `git submodule update --init --recursive`

### Requirement: nfd SHALL be built as a static library and linked into the `fury` target
`engine/CMakeLists.txt` SHALL bring nfd into the build via `add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/nfd)`. nfd's default `BUILD_SHARED_LIBS=OFF` SHALL be preserved (no override that flips it to a shared library). The `fury` target SHALL link nfd via its `nfd::nfd` alias target.

The `add_subdirectory` and the `target_link_libraries(... nfd::nfd)` line SHALL be gated on `WITH_EDITOR` — non-editor builds do not link nfd and do not include the `add_subdirectory` call.

The nfd build options `NFD_BUILD_TESTS`, `NFD_BUILD_SDL2_TESTS`, `NFD_BUILD_GLFW3_TESTS`, and `NFD_INSTALL` SHALL be forced to `OFF` in the cache (so nfd never builds its test binaries or generates install rules).

#### Scenario: Editor build links nfd statically
- **WHEN** the engine is configured with `WITH_EDITOR=ON` (the default)
- **THEN** the `fury` executable links `nfd::nfd`
- **AND** `libnfd.a` (or the platform equivalent) is produced as a static library
- **AND** no nfd shared library (`libnfd.dylib` / `nfd.dll`) is produced

#### Scenario: Non-editor build does not link nfd
- **WHEN** the engine is configured with `WITH_EDITOR=OFF`
- **THEN** the `fury` target does not link nfd
- **AND** `add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/nfd)` is not invoked

#### Scenario: nfd tests and install targets are suppressed
- **WHEN** the engine is configured with `WITH_EDITOR=ON`
- **THEN** nfd's test binaries are not built
- **AND** no nfd install rules are generated

### Requirement: The Lua binding SHALL expose `Editor.OpenDialog` and `Editor.SaveDialog`
The Lua binding layer SHALL expose two functions on the `Editor` table:

- `Editor.OpenDialog(opts) -> string | table<string> | nil`
  - `opts` is a table with optional keys: `filter` (string, e.g., `"png,jpg,jpeg"`; defaults to `"All"`), `default_path` (string, optional), `multi` (boolean, default `false`).
  - Returns the selected path as a string (single-select), a 1-indexed table of path strings (multi-select), or `nil` if the user cancelled or an error occurred.

- `Editor.SaveDialog(opts) -> string | nil`
  - `opts` is a table with optional keys: `filter` (string), `default_path` (string, optional), `default_name` (string, optional).
  - Returns the chosen path as a string, or `nil` if the user cancelled or an error occurred.

Both functions SHALL call the underlying `NFD_OpenDialog` / `NFD_SaveDialog` (or their multi-select variant) directly and translate the result. Errors from nfd SHALL be logged via `FURYE` and the function SHALL return `nil` (the editor continues running).

#### Scenario: OpenDialog single-select returns a path
- **WHEN** a Lua script calls `Editor.OpenDialog({filter = "png", default_path = "/tmp"})` and the user picks a file `foo.png`
- **THEN** the function returns the string `"/tmp/foo.png"` (or the platform equivalent)

#### Scenario: OpenDialog multi-select returns a table of paths
- **WHEN** a Lua script calls `Editor.OpenDialog({filter = "png", multi = true})` and the user picks `foo.png` and `bar.png`
- **THEN** the function returns a 1-indexed table `{"/path/to/foo.png", "/path/to/bar.png"}`

#### Scenario: OpenDialog cancel returns nil
- **WHEN** the user dismisses the open dialog without picking a file
- **THEN** the function returns `nil`

#### Scenario: SaveDialog returns a path
- **WHEN** a Lua script calls `Editor.SaveDialog({filter = "scene", default_name = "untitled"})` and the user confirms `my_scene.scene`
- **THEN** the function returns the string `"/path/to/my_scene.scene"`

#### Scenario: SaveDialog cancel returns nil
- **WHEN** the user dismisses the save dialog without confirming
- **THEN** the function returns `nil`

#### Scenario: nfd error does not crash the editor
- **WHEN** nfd returns an error result from `NFD_OpenDialog` (e.g., out of memory)
- **THEN** the error is logged via `FURYE`
- **AND** the function returns `nil`
- **AND** the editor continues running

#### Scenario: WITH_EDITOR=OFF stub
- **WHEN** the engine is built with `WITH_EDITOR=OFF`
- **THEN** `Editor.OpenDialog` and `Editor.SaveDialog` are no-op stubs in Lua that return `nil`

### Requirement: The Save-As flow SHALL use `Editor.SaveDialog` instead of the ImGui modal
The "File → Save As" menu item SHALL trigger a Lua-registered `on_save_as` callback (registered via the existing `Editor.SetSceneIO` table) which calls `Editor.SaveDialog` to obtain the destination path and then calls the existing `SceneIO.on_save_as(path)` writer with that path. The previous `RenderSaveAsModal` / `g_SaveAsModalOpen` ImGui-based modal SHALL be removed.

If `Editor.SaveDialog` returns `nil` (user cancelled), no scene write SHALL occur and the editor's "scene dirty" state SHALL be unchanged.

#### Scenario: Save-As via native dialog
- **WHEN** the user picks "File → Save As"
- **THEN** a native OS save-file dialog appears
- **AND** the user is prompted for a path with the default name and filter
- **WHEN** the user confirms a path
- **THEN** the active scene is written to that path via `SceneIO.on_save_as(path)`
- **AND** the editor's "scene dirty" flag is cleared on success

#### Scenario: Save-As cancel
- **WHEN** the user dismisses the native save dialog
- **THEN** no scene write occurs
- **AND** the editor's "scene dirty" flag is unchanged
- **AND** no error is shown

#### Scenario: No ImGui modal remains for Save-As
- **WHEN** the user opens "File → Save As"
- **THEN** no in-editor ImGui modal appears (the native dialog is the only UI)
