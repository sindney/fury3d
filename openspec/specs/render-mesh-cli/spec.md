# render-mesh-cli

## Purpose

The `fury render-mesh` CLI subcommand for visual debugging of the mesh-rendering
pipeline. Renders a named mesh from a scene to a 256×256 offscreen PNG using the
same simple Lambert shader the editor's mesh preview / thumbnail use, so
developers can iterate on the shared `RenderMeshLambert` helper and camera math
without launching the editor.

## Requirements

### Requirement: The engine SHALL ship a `fury render-mesh` CLI subcommand for visual debugging of mesh rendering

The `fury render-mesh <scene> <mesh_name> <output.png>` CLI subcommand SHALL load a scene, find the named mesh, render it to a 256×256 offscreen FBO using the same simple Lambert shader the editor's mesh preview / thumbnail use, and write a PNG file. The subcommand exists to let developers iterate on the mesh-rendering pipeline (shader, camera math, FBO setup) without launching the editor.

The subcommand needs a GL context (an SFML window), so it lives in the launcher's main path, not in the headless `Cli::Run` dispatch. It uses `Cli::LoadSceneForExec` to load the scene via the same dispatch `fury exec` uses (`.json`, `.bin`, `.gltf`, `.glb`, `.fbx`). The output PNG is written via `stbi_write_png` (the same stb_image_write already linked for `Engine::WriteBackBufferAsPng`). The camera is framed on the mesh's AABB via the same `ComputeInitialDistance` helper the editor uses.

The subcommand exits with code 0 on success, 1 on user error (missing args, mesh not found), or 2 on internal error. It does NOT launch the editor, does NOT initialize the Lua VM, and does NOT open any windows the user can see.

#### Scenario: Render a mesh to a PNG

- **WHEN** the user runs `fury render-mesh Resource/Scene/scene.json T90 /tmp/t90.png`
- **THEN** the engine loads the scene, finds the mesh named "T90"
- **AND** renders it to a 256×256 offscreen FBO with the simple Lambert shader on an opaque black background
- **AND** writes the result to `/tmp/t90.png`
- **AND** exits with code 0

#### Scenario: Mesh not found in scene

- **WHEN** the user runs `fury render-mesh scene.json NonExistent /tmp/out.png`
- **AND** the scene does not contain a mesh named "NonExistent"
- **THEN** the engine prints an error to stderr naming the missing mesh
- **AND** exits with code 1
- **AND** no PNG is written

#### Scenario: Help text mentions render-mesh

- **WHEN** the user runs `fury help`
- **THEN** the help text includes a one-line description of `render-mesh` as a subcommand

#### Scenario: Used for debugging the editor's preview

- **WHEN** the developer changes the simple Lambert shader's lighting in `EditorAssetWindows.cpp`
- **THEN** the developer can run `fury render-mesh scene.json <mesh> /tmp/out.png` to see the new look as a static image
- **AND** can compare the result against the editor's preview / Content Browser thumbnail to verify consistency
