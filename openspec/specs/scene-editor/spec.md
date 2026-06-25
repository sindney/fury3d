# scene-editor

## Purpose

The runtime Lua surface that turns `Demo.lua` into a minimum-viable scene editor: importer bindings (`LoadGltf` / `LoadFbx` / `LoadScene` / `MergeInto`), scene-clearing semantics, the Scene-menu pattern (`New` / `Open` / `Import` / `Save As`), and directory enumeration via `FileUtil.ListDirectory`. Covers the contract Demo.lua relies on so future scripts can replicate the editor pattern.

## Requirements

### Requirement: The Lua binding surface SHALL expose `Importer.LoadGltf`, `Importer.LoadFbx`, `Importer.LoadScene`, and `Importer.MergeInto`

The `fury` Lua bindings SHALL register an `Importer` table on the global Lua namespace with four functions:

- `Importer.LoadGltf(path)` — load a `.gltf` or `.glb` file via `GltfImporter::Import` and return a new `Scene` (Lua usertype). Returns `nil` on error; the error reason is logged via `FURYE`.
- `Importer.LoadFbx(path)` — convert FBX to a temp glTF via `FbxConverter::Convert`, then call `GltfImporter::Import` on the result. Returns a new `Scene` or `nil`.
- `Importer.LoadScene(path)` — dispatch by extension: `.json` → `FileUtil.LoadFile`, `.bin` → `FileUtil.LoadCompressedFile`, `.gltf` / `.glb` → `LoadGltf`, `.fbx` → `LoadFbx`. Returns a new `Scene` or `nil`.
- `Importer.MergeInto(target_scene, source_scene)` — append `source_scene`'s root children, materials, meshes, animations, and joints into `target_scene`'s `EntityManager`, then call `target_scene:GetSceneManager():AddSceneNodeRecursively(target_scene:GetRootNode())` so the new subtrees are registered with the octree. Returns the number of nodes merged.

Each function SHALL be safe to call from `on_init` or `on_update` (no GL context required at call time; texture GPU upload is deferred until the texture is first sampled, which happens during render after `Pipeline::Execute`).

#### Scenario: LoadGltf returns a usable Scene
- **WHEN** a Lua script calls `Importer.LoadGltf("Resource/Scene/Box.glb")`
- **THEN** the return value is a usertype `Scene` whose `GetRootNode()` has at least one child node with a `MeshRender` component

#### Scenario: LoadFbx invokes the FBX → glTF chain
- **WHEN** a Lua script calls `Importer.LoadFbx("Resource/Scene/james.fbx")`
- **THEN** the FBX2glTF subprocess is invoked, the intermediate glTF is read, and a new `Scene` is returned
- **AND** the intermediate glTF temp file is cleaned up after the load completes

#### Scenario: LoadScene dispatches by extension
- **WHEN** a Lua script calls `Importer.LoadScene(path)` with paths ending `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`
- **THEN** the correct loader is selected for each extension
- **AND** an unsupported extension returns `nil` with a clear error in `Log.txt`

#### Scenario: MergeInto preserves both scenes' content
- **WHEN** a Lua script calls `Importer.MergeInto(active_scene, imported_scene)`
- **THEN** the active scene contains all of its previous nodes / materials / meshes
- **AND** the active scene also contains the imported scene's nodes / materials / meshes
- **AND** the newly-added subtrees are queryable through the octree (visible in `Pipeline::Execute` output)

### Requirement: The Lua surface SHALL expose `Scene:Clear()` and `FileUtil.ListDirectory(path, extensions)`

`Scene:Clear()` SHALL be bound as an instance method on the existing `Scene` usertype. It clears the scene's `EntityManager`, `SceneManager`, and root-node children, matching the C++ `Scene::Clear` behavior. Camera and pipeline state are not affected.

`FileUtil.ListDirectory(path, extensions)` SHALL list files in a directory and return a Lua array of relative filenames. The `extensions` parameter is an optional Lua array of file-extension strings (e.g. `{".json", ".gltf", ".fbx"}`) — when provided, only matching files are returned. When `nil` or absent, all entries are returned. Hidden files (leading `.`) SHALL be excluded.

#### Scenario: Clear preserves camera and pipeline
- **WHEN** a Lua script calls `active_scene:Clear()` while a camera node is attached and a pipeline is active
- **THEN** the active scene's root has no children
- **AND** the camera scene-node (which lives outside the scene's tree in the demo) is unaffected
- **AND** the active pipeline is unaffected

#### Scenario: ListDirectory filters by extension
- **WHEN** a Lua script calls `FileUtil.ListDirectory(path, {".gltf", ".fbx"})`
- **THEN** the returned array contains only files whose extensions match (case-insensitive)
- **AND** subdirectories and hidden files are excluded

#### Scenario: ListDirectory with no filter
- **WHEN** a Lua script calls `FileUtil.ListDirectory(path)` or `FileUtil.ListDirectory(path, nil)`
- **THEN** all non-hidden files in the directory are returned

#### Scenario: ListDirectory on missing path
- **WHEN** the path does not exist
- **THEN** an empty array is returned (not an error)
- **AND** a one-line warning is logged via `FURYW`

### Requirement: `Editor.lua` SHALL extend the editor-owned File menu via `Editor.SetSceneIO` and register camera controls via `Editor.SetCameraSettings`

The shipped `examples/Editor.lua` (renamed from `examples/Demo.lua`) SHALL register its file-IO behavior through the C++ editor's `Editor.SetSceneIO(...)` Lua surface — it SHALL NOT emit `File → New / Open / Import / Save As` items via `Gui.SetMenuBarCallback`. The editor (C++) owns the entire `File` menu structure; Editor.lua provides the policy by passing callbacks into `Editor.SetSceneIO`:

- `list_files = function() return FileUtil.ListDirectory("Resource/Scene/", {".json",".bin",".gltf",".glb",".fbx"}) end`
- `on_new = function() Scene.GetActive():Clear(); set_status("scene cleared") end`
- `on_open = function(filename) ... open_scene(filename) end` (existing helper from Demo.lua, slightly adjusted)
- `on_import = function(filename) ... import_scene(filename) end`
- `on_save_as = function(filename) save_active_scene(filename) end`
- `scene_dir = function() return FileUtil.GetAbsPath("Resource/Scene/") end` (or the directory of the most-recently-opened/saved file)

The legacy custom `Save As` modal in Demo.lua (lines ~401-414) SHALL be removed; the editor's built-in modal serves this role. The legacy `Auto-Add Default Sun` File-menu toggle SHALL be moved into the editor's Settings → Import section by registering its boolean state with the editor's settings panel (or by reading it from a Lua-side function the editor exposes through `Editor.SetSceneIO`'s settings hook — the spec leaves the exact wiring to design.md but the user-facing outcome is that the toggle lives in Settings → Import, not in the File menu).

The `File → Quit` item SHALL be rendered by the editor (C++) and SHALL invoke `Gui::CloseWindow` to close the engine window. Closing via the OS window controls remains supported and equivalent.

The legacy script-emitted `Camera` top-level menu SHALL NOT be registered by `Editor.lua`. Camera tuning controls live inside `Settings → Camera`, populated by `Editor.SetCameraSettings({controls = {...}})`. The Settings window itself is reachable via `File → Settings`. Editor.lua MAY still register a `Gui.SetMenuBarCallback` for any other project-specific menus, but SHALL NOT use that callback to emit a `Camera` menu.

#### Scenario: Editor.lua does not emit File menu items

- **WHEN** Editor.lua loads
- **THEN** it does not call `Gui.BeginMenu("File")` or any `Gui.MenuItem` directly under the File menu

#### Scenario: File → New uses Editor.lua's on_new policy

- **WHEN** the user clicks `File → New`
- **THEN** the editor invokes the registered `on_new` callback
- **AND** Editor.lua's `on_new` clears the active scene and emits a status message

#### Scenario: File → Open lists files from Editor.lua's list_files

- **WHEN** the user opens `File → Open`
- **THEN** the submenu items match exactly the list returned by Editor.lua's `list_files()` callback

#### Scenario: File → Save As routes through Editor.lua's on_save_as

- **WHEN** the user clicks `File → Save As…`, types `mything.json`, and clicks Save
- **THEN** the editor invokes the registered `on_save_as` callback with `"mything.json"`
- **AND** Editor.lua writes `Resource/Scene/mything.json`

#### Scenario: File → Quit closes the engine window

- **WHEN** the user clicks `File → Quit`
- **THEN** the editor invokes `Gui::CloseWindow`
- **AND** the engine main loop exits on the next iteration

#### Scenario: Settings opens via File → Settings (no Camera top-level menu)

- **WHEN** the engine renders the main menu bar
- **THEN** no top-level `Camera` menu is emitted by Editor.lua
- **AND** `File → Settings` toggles the Settings window, whose Camera section is populated by `Editor.SetCameraSettings`

### Requirement: `FileUtil.ListDirectory` enumerates `Resource/Scene/` dynamically (not hard-coded)

The `FileUtil.ListDirectory` binding SHALL list entries from the filesystem at call time via `std::filesystem::directory_iterator` — not from a fixed compile-time list. Files added to `Resource/Scene/` after the engine starts (e.g., via `Save As`) SHALL be visible in the next call to `ListDirectory` without restarting the engine.

#### Scenario: Files added at runtime appear on the next enumeration

- **WHEN** a Lua script calls `FileUtil.SaveFile(scene, "Resource/Scene/runtime_save.json")` while the engine is running
- **AND** then calls `FileUtil.ListDirectory("Resource/Scene/", {".json"})` on the next frame
- **THEN** the returned array contains `runtime_save.json`

### Requirement: `Importer.LoadScene` SHALL load `.json` / `.bin` engine-scene files into an empty active scene without `MeshRender::Load` failures

When invoked on a `.json` or `.bin` file, `Importer.LoadScene` SHALL produce a non-null `Scene` whose `MeshRender` components have correctly-resolved `Mesh` and `Material` references, *regardless of the active scene's current `EntityManager` contents at call time*. This SHALL hold in particular when called immediately after `Scene:Clear()` (e.g., after `File → New`).

Implementation note (non-normative): one acceptable mechanism is to temporarily set `Scene::Active` to the import target during the load and restore it afterward; the spec does not mandate the mechanism, only the observable outcome.

#### Scenario: Loading scene.json after New succeeds

- **WHEN** Lua calls `Scene.GetActive():Clear()` (which empties the active scene's `EntityManager`)
- **AND** then calls `Importer.LoadScene("/abs/path/Resource/Scene/scene.json")`
- **THEN** the returned scene has at least one node with a `MeshRender` component
- **AND** that `MeshRender`'s mesh reference is non-null (resolved to the `T90` mesh deserialized from the file)
- **AND** no `Mesh ... not found!` or `Serialization failed!` errors are logged

#### Scenario: Loading scene.bin after New succeeds

- **WHEN** Lua calls `Scene.GetActive():Clear()` and then calls `Importer.LoadScene("/abs/path/Resource/Scene/scene.bin")`
- **THEN** the returned scene has the same node, mesh, and material counts as a freshly-launched demo (where the engine loaded `scene.bin` directly at startup)

### Requirement: The engine SHALL forward extra command-line arguments to the Lua script via the standard `arg` table

`examples/main.cpp` SHALL populate a global Lua table `arg` before executing the user script. The convention SHALL match the standard Lua interpreter:

- `arg[0]` = the script path (the same string passed as `argv[1]` to the engine, or `"Demo.lua"` if no script argument was given).
- `arg[1..N]` = `argv[2..argc-1]` (each entry a string, in order).
- `arg` itself is a regular Lua table (1-indexed) — `#arg` yields the count of extra arguments.

The script-path argument SHALL continue to be `argv[1]` (no change to the existing routing). The `arg` table SHALL be the script's primary mechanism for reading launch parameters.

#### Scenario: arg table is populated from argv

- **WHEN** the engine is invoked as `./fury Demo.lua outdoor.fbx --debug`
- **THEN** the Lua script sees `arg[0] == "Demo.lua"`, `arg[1] == "outdoor.fbx"`, `arg[2] == "--debug"`, `#arg == 2`

#### Scenario: arg is empty when only the script is supplied

- **WHEN** the engine is invoked as `./fury Demo.lua`
- **THEN** `arg[0] == "Demo.lua"` and `#arg == 0`

#### Scenario: arg[0] defaults to "Demo.lua" when no script argument is given

- **WHEN** the engine is invoked as `./fury` (no arguments)
- **THEN** `arg[0] == "Demo.lua"` and `#arg == 0`

### Requirement: `Editor.lua` SHALL honor `arg[1]` as a startup scene path; otherwise SHALL load the default `Resource/Scene/scene.bin`

In `on_init`, after the active scene and pipeline are created, `Editor.lua` SHALL inspect `arg[1]` and apply the same fallback logic that `Demo.lua` previously used:

- If `arg[1]` is nil or an empty string: load `Resource/Scene/scene.bin` via `FileUtil.LoadSceneFromCompressedFile`.
- If `arg[1]` is set: resolve via the literal path first, then `Resource/Scene/` + literal, then dispatch through `Importer.LoadScene`.
- On failure: log a warning, set a one-line status, and fall back to the default scene.

This requirement is unchanged from the prior `Demo.lua` requirement except for the script's filename. The behavior is preserved verbatim; only the file the engine looks for has been renamed.

`examples/main.cpp` SHALL update its default-script-path argv resolution from `"Demo.lua"` to `"Editor.lua"` so `./fury` (no args) loads the new file.

#### Scenario: ./fury with no extra args loads Editor.lua and the default scene

- **WHEN** the user runs `./fury` from `examples/`
- **THEN** the engine loads `Editor.lua` (not `Demo.lua`)
- **AND** the active scene contains the tank-on-grass content from `Resource/Scene/scene.bin`

#### Scenario: ./fury Editor.lua outdoor.fbx loads outdoor.fbx

- **WHEN** the user runs `./fury Editor.lua outdoor.fbx`
- **THEN** Editor.lua resolves `outdoor.fbx` via the `Resource/Scene/` prefix fallback
- **AND** the imported FBX renders in the viewport

#### Scenario: ./fury Editor.lua with a missing file falls back gracefully

- **WHEN** the user runs `./fury Editor.lua does_not_exist.fbx`
- **THEN** Editor.lua logs a warning and falls back to `Resource/Scene/scene.bin`
- **AND** the editor is interactive

### Requirement: `Editor.lua` opens of `tank.fbx` and `james.fbx` SHALL render textured

When the user clicks `File → Open → tank.fbx` (or `File → Open → james.fbx`) in `Editor.lua`, the active scene SHALL render with the FBX's diffuse textures applied. The viewport SHALL show the textured tank body, wheels, and grass plane (for `tank.fbx`) or the textured character mesh (for `james.fbx`), not flat-colored geometry.

This requirement is unchanged in substance from the prior `Demo.lua` version; only the script name has been updated. The implementation continues to be backed by the `embedded-textures` capability and the modified `gltf-importer` capability — embedded JPEGs from the FBX→glTF chain are uploaded to the GPU directly from memory via `Texture::CreateFromMemory`. Editor.lua itself contains no texture-handling code.

#### Scenario: Open tank.fbx renders the embedded JPEGs

- **WHEN** a user runs `./fury Editor.lua` and clicks `File → Open → tank.fbx`
- **THEN** the imported scene's tank body material has a non-null diffuse-texture upload (`m_ID != 0`)
- **AND** the rendered viewport shows the body's texture (the JPEG that was embedded in the FBX), not a flat gray
- **AND** no `Texture::CreateFromImage failed` errors appear in `Log.txt`
- **AND** no `_image<i>.<ext>` files are written to any temp directory

#### Scenario: Open james.fbx renders the embedded character textures

- **WHEN** a user runs `./fury Editor.lua` and clicks `File → Open → james.fbx`
- **THEN** the imported character renders with its source textures, not flat-colored

#### Scenario: Save As after opening an FBX produces a self-contained scene

- **WHEN** a user opens `tank.fbx` and then uses `File → Save As…` to write `Resource/Scene/tank_saved.json`
- **THEN** `Resource/Scene/tank_saved.json` is created
- **AND** the JPEG textures (e.g. `body.jpg`, `wheels.jpg`, `grass.jpg`) are extracted to `Resource/Scene/` next to the saved scene
- **AND** loading `Resource/Scene/tank_saved.json` via `File → Open` afterwards renders the same textured scene

