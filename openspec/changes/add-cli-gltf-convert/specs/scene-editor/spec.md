## ADDED Requirements

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

### Requirement: `Demo.lua` SHALL expose a File menu with New / Open / Import / Save commands

The shipped `examples/Demo.lua` SHALL extend the engine's menu bar with a `File` menu (added via `Gui.SetMenuBarCallback`, alongside the existing `Camera` menu) containing at minimum these items:

- **`New Scene`** — clears the active scene (`Scene.GetActive():Clear()`). Camera and pipeline remain active. The viewport renders an empty scene afterwards.
- **`Open Scene`** — a submenu listing each entry from `FileUtil.ListDirectory("Resource/Scene/", {".json", ".bin", ".gltf", ".glb", ".fbx"})`. Selecting an entry: clears the active scene, calls `Importer.LoadScene(path)` to build a new scene, and re-assigns the active scene reference and pipeline target.
- **`Import`** — a submenu with the same enumeration as `Open Scene`. Selecting an entry: loads the file via `Importer.LoadScene` and calls `Importer.MergeInto(active, imported)` rather than replacing the active scene.
- **`Save Scene As`** — opens an ImGui modal containing a `Gui.InputText` field pre-filled with `scene_saved.json`. On confirm, writes the active scene to `Resource/Scene/<filename>` via `FileUtil.SaveFile` (for `.json`) or `FileUtil.SaveCompressedFile` (for `.bin`). The `Gui.InputText` Lua binding is added as part of this change. A status line in `Log.txt` confirms the save.

The Demo SHALL handle the case where the user opens a file the importer rejects (returns `nil`): a one-line ImGui-toast-style message (`Gui.Text` inside a transient window) SHALL inform the user, and the previous scene SHALL remain active.

#### Scenario: New Scene clears geometry
- **WHEN** the user clicks `File → New Scene` in Demo.lua
- **THEN** the next frame renders no geometry (just the clear color)
- **AND** the camera flythrough still works (camera and pipeline unaffected)

#### Scenario: Open Scene swaps content
- **WHEN** the user clicks `File → Open Scene → james.fbx`
- **THEN** Demo.lua calls `Importer.LoadScene("Resource/Scene/james.fbx")`
- **AND** the previous scene is cleared
- **AND** the new scene is visible in the viewport
- **AND** the camera continues to work

#### Scenario: Import merges content
- **WHEN** the user clicks `File → Import → tank.fbx` while a scene is already loaded
- **THEN** both the original scene's geometry and the imported tank are visible
- **AND** the imported subtree is reachable via the octree's visibility query (renders correctly when in frustum)

#### Scenario: Save Scene As writes to a user-supplied path
- **WHEN** the user clicks `File → Save Scene As`, types `mything.json` into the modal's text field, and confirms
- **THEN** `Resource/Scene/mything.json` is written
- **AND** loading `mything.json` via `Importer.LoadScene` produces an equivalent scene (same node count, mesh count, material count)

#### Scenario: Import failure preserves active scene
- **WHEN** the user clicks `File → Open Scene → broken.gltf` and the importer rejects the file
- **THEN** `Importer.LoadScene` returns `nil`
- **AND** the active scene remains the previous one (no clear is performed on error)
- **AND** a visible error is shown in the UI (toast or status line)
