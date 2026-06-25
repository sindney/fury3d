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

### Requirement: `Demo.lua` SHALL expose a single `File` menu owning New / Open / Import / Save As / Quit

The shipped `examples/Demo.lua` SHALL register one menu-bar callback (via `Gui.SetMenuBarCallback`) that emits a single top-level `File` menu. The engine SHALL NOT emit its own `File` menu block; this top-level menu is owned exclusively by the running script. The `File` menu SHALL contain the following items in order:

- **`New`** — clears the active scene (`Scene.GetActive():Clear()`). Camera and pipeline remain active. The viewport renders an empty scene afterwards.
- **`Open`** — a submenu listing each entry from `FileUtil.ListDirectory("Resource/Scene/", {".json", ".bin", ".gltf", ".glb", ".fbx"})`. Selecting an entry: clears the active scene, calls `Importer.LoadScene(path)` to build a new scene, and merges the imported content into the active scene (so the camera/pipeline wiring survives).
- **`Import`** — a submenu with the same enumeration as `Open`. Selecting an entry: loads the file via `Importer.LoadScene` and calls `Importer.MergeInto(active, imported)` rather than replacing the active scene.
- **`Save As...`** — opens an ImGui modal containing a `Gui.InputText` field pre-filled with `scene_saved.json`. On confirm, writes the active scene to `Resource/Scene/<filename>` via `FileUtil.SaveFile` (for `.json`) or `FileUtil.SaveCompressedFile` (for `.bin`). A status line in `Log.txt` confirms the save.
- **(Separator)**
- **`Quit`** — calls the new `Window.Close()` Lua binding to close the engine window.

Demo.lua SHALL also continue to register a `Camera` top-level menu (alongside the engine's `View` menu) carrying its existing camera-tuning panel item. No top-level `Scene` menu is registered — that menu is retired.

The Demo SHALL handle the case where `Importer.LoadScene` rejects a file (returns `nil`): a one-line status message (rendered via the existing `set_status` helper) SHALL inform the user, and the previous scene SHALL remain active.

#### Scenario: Single top-level File menu replaces the prior File + Scene pair

- **WHEN** Demo.lua is loaded and the engine renders the top menu bar
- **THEN** exactly one top-level menu labeled `File` is visible
- **AND** no top-level menu labeled `Scene` is visible
- **AND** the `File` menu contains, in order: `New`, `Open ▸`, `Import ▸`, `Save As...`, a separator, and `Quit`

#### Scenario: New clears geometry without affecting camera or pipeline

- **WHEN** the user clicks `File → New` in Demo.lua
- **THEN** the next frame renders no geometry (just the clear color)
- **AND** the camera flythrough still works (camera and pipeline unaffected)

#### Scenario: Open replaces the active scene

- **WHEN** the user clicks `File → Open → james.fbx`
- **THEN** Demo.lua calls `Importer.LoadScene("Resource/Scene/james.fbx")`
- **AND** the previous scene is cleared
- **AND** the new scene is visible in the viewport
- **AND** the camera continues to work

#### Scenario: Open of a .json scene works against an empty active scene

- **WHEN** the user clicks `File → New` and then `File → Open → scene.json` (with `Resource/Scene/scene.json` being the bundled tank-on-grass scene)
- **THEN** `Importer.LoadScene` returns a non-nil scene with the tank mesh and its materials resolved
- **AND** Demo.lua merges that scene into the active scene
- **AND** the viewport renders the tank on the grass plane (no `Mesh T90 not found!` error, no `Serialization failed!` error)

#### Scenario: Import merges into the existing scene

- **WHEN** the user clicks `File → Import → tank.fbx` while a scene is already loaded
- **THEN** both the original scene's geometry and the imported tank are visible
- **AND** the imported subtree is reachable via the octree's visibility query (renders correctly when in frustum)

#### Scenario: Save As writes to a user-supplied path

- **WHEN** the user clicks `File → Save As...`, types `mything.json` into the modal's text field, and confirms
- **THEN** `Resource/Scene/mything.json` is written
- **AND** loading `mything.json` via `Importer.LoadScene` produces an equivalent scene (same node count, mesh count, material count)

#### Scenario: Quit closes the engine window

- **WHEN** the user clicks `File → Quit`
- **THEN** `Window.Close()` is invoked
- **AND** the engine window closes (next frame, the main loop exits)

#### Scenario: Import failure preserves active scene

- **WHEN** the user clicks `File → Open → broken.gltf` and the importer rejects the file
- **THEN** `Importer.LoadScene` returns `nil`
- **AND** the active scene remains the previous one (no clear is performed on error)
- **AND** a visible status message is shown in the UI

### Requirement: The Lua surface SHALL expose `Window.Close()`

The Lua bindings SHALL register a `Window` table with a `Close()` function that closes the engine window via the same path the engine's previous built-in `File → Quit` menu item used (`m_Window->close()`). Calling `Window.Close()` SHALL cause the main loop to exit on the next iteration. The call SHALL be idempotent — calling it after the window has already been closed SHALL be a no-op (no exception, no log spam).

#### Scenario: Window.Close closes the engine window

- **WHEN** a Lua script calls `Window.Close()` while the engine is running
- **THEN** the engine's main loop exits on the next iteration
- **AND** the process terminates cleanly

#### Scenario: Window.Close is idempotent

- **WHEN** a Lua script calls `Window.Close()` twice in succession
- **THEN** the second call does not throw, log an error, or cause undefined behavior

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

### Requirement: `Demo.lua` SHALL honor `arg[1]` as a startup scene path; otherwise SHALL load the default `Resource/Scene/scene.bin`

In `on_init`, after the active scene and pipeline are created, `Demo.lua` SHALL inspect `arg[1]`:

- If `arg[1]` is nil or an empty string: load `Resource/Scene/scene.bin` via the existing `FileUtil.LoadSceneFromCompressedFile` path (unchanged from today's behavior).
- If `arg[1]` is set: resolve the path with the following fallback order:
  1. The literal value, if it names an existing file (absolute or relative to the current working directory).
  2. `Resource/Scene/` + literal value, if step (1) failed.
  Then dispatch through `Importer.LoadScene` (which handles `.json` / `.bin` / `.gltf` / `.glb` / `.fbx` per extension).
- If `Importer.LoadScene` returns `nil` (resolution failed or unsupported extension): log a warning, surface a one-line status message in the demo UI, and fall back to loading the default `scene.bin`. The demo SHALL NOT refuse to start.

When the argv-provided scene loads successfully, the demo SHALL merge it into the active scene (so camera and pipeline wiring survive) and SHALL surface a one-line status message naming the loaded file.

#### Scenario: ./fury Demo.lua with no extra args loads the default scene

- **WHEN** the user runs `./fury Demo.lua`
- **THEN** the active scene contains the tank-on-grass content from `Resource/Scene/scene.bin`

#### Scenario: ./fury Demo.lua outdoor.fbx loads outdoor.fbx at startup

- **WHEN** the user runs `./fury Demo.lua outdoor.fbx` from `examples/bin/`
- **THEN** `Demo.lua` calls `Importer.LoadScene` on `Resource/Scene/outdoor.fbx` (the `Resource/Scene/` prefix fallback resolves the literal `outdoor.fbx`)
- **AND** the active scene contains the imported FBX content
- **AND** the viewport renders that content (lit, once KHR_lights_punctual translation is in place)

#### Scenario: ./fury Demo.lua with an absolute path

- **WHEN** the user runs `./fury Demo.lua /Users/me/Models/scene.json` and that path exists
- **THEN** `Demo.lua` calls `Importer.LoadScene` on the literal path (no `Resource/Scene/` prefix)
- **AND** the active scene contains that file's content

#### Scenario: ./fury Demo.lua with a missing file falls back gracefully

- **WHEN** the user runs `./fury Demo.lua does_not_exist.fbx`
- **THEN** `Importer.LoadScene` returns `nil` (after both literal and `Resource/Scene/`-prefix resolution failed)
- **AND** `Demo.lua` logs a warning and falls back to `Resource/Scene/scene.bin`
- **AND** the demo is interactive (does not crash, does not abort)

### Requirement: `Demo.lua` opens of `tank.fbx` and `james.fbx` SHALL render textured

When the user clicks `File → Open → tank.fbx` (or `File → Open → james.fbx`) in `Demo.lua`, the active scene SHALL render with the FBX's diffuse textures applied. The viewport SHALL show the textured tank body, wheels, and grass plane (for `tank.fbx`) or the textured character mesh (for `james.fbx`), not flat-colored geometry.

Implementation note (non-normative): this is satisfied by the new `embedded-textures` capability and the modified `gltf-importer` capability — embedded JPEGs from the FBX→glTF chain are uploaded to the GPU directly from memory via `Texture::CreateFromMemory`. No extraction to temp files happens at import time. Demo.lua itself is not modified.

#### Scenario: Open tank.fbx renders the embedded JPEGs

- **WHEN** a user runs `./fury Demo.lua` and clicks `File → Open → tank.fbx`
- **THEN** the imported scene's tank body material has a non-null diffuse-texture upload (`m_ID != 0`)
- **AND** the rendered viewport shows the body's texture (the JPEG that was embedded in the FBX), not a flat gray
- **AND** no `Texture::CreateFromImage failed` errors appear in `Log.txt`
- **AND** no `_image<i>.<ext>` files are written to any temp directory

#### Scenario: Open james.fbx renders the embedded character textures

- **WHEN** a user runs `./fury Demo.lua` and clicks `File → Open → james.fbx`
- **THEN** the imported character renders with its source textures, not flat-colored

#### Scenario: Save As after opening an FBX produces a self-contained scene

- **WHEN** a user opens `tank.fbx` and then uses `File → Save As...` to write `Resource/Scene/tank_saved.json`
- **THEN** `Resource/Scene/tank_saved.json` is created
- **AND** the JPEG textures (e.g. `body.jpg`, `wheels.jpg`, `grass.jpg`) are extracted to `Resource/Scene/` next to the saved scene
- **AND** loading `Resource/Scene/tank_saved.json` via `File → Open` afterwards renders the same textured scene

