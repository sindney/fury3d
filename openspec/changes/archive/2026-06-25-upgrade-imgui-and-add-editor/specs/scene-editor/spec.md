## RENAMED Requirements

- FROM: `### Requirement: \`Demo.lua\` SHALL expose a single \`File\` menu owning New / Open / Import / Save As / Quit`
- TO: `### Requirement: \`Editor.lua\` SHALL extend the editor-owned File menu via \`Editor.SetSceneIO\` and register camera controls via \`Editor.SetCameraSettings\``

- FROM: `### Requirement: \`Demo.lua\` SHALL honor \`arg[1]\` as a startup scene path; otherwise SHALL load the default \`Resource/Scene/scene.bin\``
- TO: `### Requirement: \`Editor.lua\` SHALL honor \`arg[1]\` as a startup scene path; otherwise SHALL load the default \`Resource/Scene/scene.bin\``

- FROM: `### Requirement: \`Demo.lua\` opens of \`tank.fbx\` and \`james.fbx\` SHALL render textured`
- TO: `### Requirement: \`Editor.lua\` opens of \`tank.fbx\` and \`james.fbx\` SHALL render textured`

## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: The Lua surface SHALL expose `Window.Close()`

**Reason**: With the editor-owned File menu, the script-driven `File → Quit` item that motivated this binding is gone. The OS window-close button (and `SIGINT`) handle quit. We retire the binding to keep the Lua surface narrow.

**Migration**: Scripts that programmatically need to close the window MAY continue to call `Window.Close()` until it is removed in a subsequent change — the binding itself stays compiled in this change for backward compatibility, but it is no longer required by Editor.lua and the spec no longer mandates its existence. A future cleanup change SHALL retire the binding entirely; users wanting clean shutdown should rely on OS-level close.
