## ADDED Requirements

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
