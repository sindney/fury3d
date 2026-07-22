# gltf-importer

## Purpose

The CPU-only translator from `tinygltf::Model` to engine `Scene` / `SceneNode` / `Mesh` / `Material` / `Joint` / `AnimationClip`. Covers what gets mapped, the lossy choices (PBR → Lambert, seconds → ticks), and what's explicitly rejected (morph targets, sparse accessors, byte-stride buffer views). Used by both the CLI and the Lua bindings.

## Requirements

### Requirement: The CLI SHALL provide a `convert gltf` subcommand that imports glTF 2.0 into the engine's runtime scene format

The `fury` binary SHALL accept the invocation `fury convert gltf <input> <output>` where `<input>` is a path to a `.gltf` or `.glb` file and `<output>` is a path with extension `.json` or `.bin`. The output extension SHALL determine the on-disk format: `.json` uses `FileUtil::SaveFile` (human-readable Serializable JSON), `.bin` uses `FileUtil::SaveCompressedFile` (LZ4-compressed Serializable JSON). The importer SHALL populate a `Scene` containing the glTF model's node hierarchy, meshes (including submesh splits per material), materials (lossy PBR → Phong/Lambert), joints (for skinned meshes), and animation clips (resampled to ticks at 24 fps). The importer SHALL NOT require an OpenGL context — all CPU-side data must be populated without GPU upload.

#### Scenario: Convert a static glTF to scene.json
- **WHEN** a user runs `./fury convert gltf Box.gltf out.json` against tinygltf's `models/Box/Box.gltf`
- **THEN** the process exits with code 0
- **AND** `out.json` exists on disk and is valid JSON containing top-level keys `materials`, `meshes`, `nodes`
- **AND** loading `out.json` via `FileUtil::LoadFile` populates a `Scene` whose root has one descendant node with a `MeshRender` component pointing at one mesh

#### Scenario: Convert a static glTF to scene.bin
- **WHEN** a user runs `./fury convert gltf Box.gltf out.bin`
- **THEN** the process exits with code 0
- **AND** `out.bin` exists on disk and is LZ4-compressed
- **AND** loading `out.bin` via `FileUtil::LoadCompressedFile` produces the same scene shape as the corresponding `.json` output

#### Scenario: Convert a binary glTF (.glb)
- **WHEN** a user runs `./fury convert gltf Box.glb out.json`
- **THEN** the importer uses `tinygltf::TinyGLTF::LoadBinaryFromFile`
- **AND** the output is byte-for-byte identical to converting the equivalent `.gltf` text variant

#### Scenario: Output extension determines format
- **WHEN** a user runs `./fury convert gltf in.gltf out.bin`
- **THEN** the converter writes LZ4-compressed output (no `.json` is produced)
- **AND WHEN** the same input is converted to `out.json`, plain JSON is written

#### Scenario: Reject unsupported output extension
- **WHEN** a user runs `./fury convert gltf in.gltf out.xml`
- **THEN** the process exits with code 1 (user error)
- **AND** stderr contains a message naming the supported output extensions (`.json`, `.bin`)

### Requirement: The importer SHALL translate the glTF node hierarchy into SceneNode trees

For each `scene` listed in `tinygltf::Model::scenes` (or the default if multiple are present), the importer SHALL walk the node graph and emit a corresponding `SceneNode` tree under the engine `Scene`'s root. Each glTF node's transform SHALL be applied to a `Transform` component on the emitted `SceneNode` (decomposed from the node's matrix or composed from its TRS arrays per glTF rules). Each glTF mesh reference SHALL emit a `MeshRender` component on the corresponding `SceneNode`.

#### Scenario: glTF node tree maps 1:1 to SceneNode tree
- **WHEN** the importer processes a glTF model with N nodes in a single scene
- **THEN** the resulting engine `Scene::GetRootNode()` has descendants matching the glTF hierarchy (same parent/child relationships)

#### Scenario: TRS array decoded into Transform
- **WHEN** a glTF node specifies `translation`, `rotation`, and `scale` arrays
- **THEN** the emitted `SceneNode`'s `Transform` component carries the same values (rotation as `Quaternion(x, y, z, w)`, scale as `Vector4(x, y, z, 1)`, translation as `Vector4(x, y, z, 1)`)

#### Scenario: Matrix-only node decomposed into TRS
- **WHEN** a glTF node specifies a 16-float `matrix` instead of TRS arrays
- **THEN** the matrix is decomposed and stored as TRS on the `Transform` component

### Requirement: The importer SHALL translate glTF meshes into engine Mesh records with precomputed AABBs and submeshes-per-material

For each `tinygltf::Mesh`, the importer SHALL emit one engine `Mesh` registered in the scene's `EntityManager`. For each primitive in the glTF mesh, the importer SHALL emit one `SubMesh` containing that primitive's index buffer. Vertex attributes (`POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`) SHALL be read via `tinygltf::Accessor` and copied into the corresponding `ArrayBufferf` field on the engine `Mesh`. The importer SHALL compute the mesh's AABB by iterating positions (or read it from the accessor's `min` / `max` when present, since glTF 2.0 mandates them on POSITION accessors). Only triangle-list primitives (mode 4) SHALL be accepted; other primitive modes SHALL cause a clear error.

#### Scenario: One submesh per primitive per material
- **WHEN** a glTF mesh has 3 primitives referencing 3 different materials
- **THEN** the emitted engine `Mesh` has 3 `SubMesh` records, each with its own index buffer
- **AND** the emitted `MeshRender` component holds 3 `Material` references in the same order

#### Scenario: AABB precomputed from POSITION accessor
- **WHEN** a glTF mesh primitive has `POSITION.min` and `POSITION.max` set
- **THEN** the emitted engine `Mesh`'s AABB matches those values without re-iterating vertices

#### Scenario: Non-triangle primitive rejected
- **WHEN** a glTF mesh primitive declares `mode = 1` (LINES)
- **THEN** the importer fails with a clear error naming the unsupported mode
- **AND** the process exits with code 1

### Requirement: The importer SHALL translate glTF materials to engine Lambert materials with lossy PBR mapping

For each `tinygltf::Material`, the importer SHALL emit one engine `Material` registered in the scene's `EntityManager` with the engine's named-uniform shape (matching today's `Material::Save` output for the Lambert pipeline). The PBR `baseColorFactor` SHALL map to the `diffuse_color` uniform; the `baseColorTexture` SHALL map to the `diffuse_texture` slot. The texture's pixels SHALL be loaded according to the new "embedded glTF images directly from memory" requirement above (in-memory upload for embedded images, file-path passthrough for external-URI images). The `emissiveFactor` SHALL map to the `emissive_color` uniform. Other PBR fields (`metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`) SHALL be read but not mapped to engine uniforms in v1; the importer SHALL log exactly one warning per source material that lists the discarded fields. The emitted material's `opaque` flag SHALL be `true` when the glTF material's `alphaMode` is `OPAQUE` and `false` otherwise. (Note: the engine ships a Lambert deferred pipeline only as of v1; an HDR/PBR pipeline is a deliberately deferred follow-up.)

#### Scenario: baseColorFactor maps to diffuse_color
- **WHEN** a glTF material has `pbrMetallicRoughness.baseColorFactor = [0.8, 0.2, 0.2, 1.0]`
- **THEN** the emitted engine `Material` has a `Uniform3f` named `diffuse_color` with values `(0.8, 0.2, 0.2)`

#### Scenario: baseColorTexture maps to diffuse_texture slot (external URI)
- **WHEN** a glTF material has `pbrMetallicRoughness.baseColorTexture.index` set, AND that texture's image has a non-empty `uri` (e.g., `"foo.png"`)
- **THEN** the emitted engine `Material` has an entry in its texture map with key `diffuse_texture` whose `m_FilePath` is the URI verbatim
- **AND** the texture's GPU upload uses `Texture::CreateFromImage(uri, ...)` (existing path)

#### Scenario: baseColorTexture maps to diffuse_texture slot (embedded bytes)
- **WHEN** a glTF material has `pbrMetallicRoughness.baseColorTexture.index` set, AND that texture's image is bufferView-backed with empty `uri`
- **THEN** the emitted engine `Material` has an entry in its texture map with key `diffuse_texture` whose GPU upload was driven by `Texture::CreateFromMemory(bytes, len, ...)`
- **AND** the texture's `m_FilePath` is empty until the scene is saved
- **AND** the texture's recorded `m_OriginalFilename` matches the glTF `image.name` (or the synthesized `<stem>_image<i>.<ext>` fallback when `image.name` is empty)

#### Scenario: One warning per material with discarded PBR fields
- **WHEN** a glTF material has a non-null `normalTexture`, `metallicFactor`, and `roughnessFactor`
- **THEN** stderr contains a single warning line for that material naming the discarded fields
- **AND** no extra warnings are emitted for other materials that share the same discarded set

#### Scenario: alphaMode controls opaque flag
- **WHEN** a glTF material has `alphaMode = "BLEND"`
- **THEN** the emitted engine `Material`'s `opaque` flag is `false`
- **AND WHEN** `alphaMode` is `"OPAQUE"` (or unspecified), the flag is `true`

### Requirement: The importer SHALL load embedded glTF images directly from memory into engine `Texture` objects, without writing to disk

When a `tinygltf::Image` has an empty `image.uri` and a non-negative `bufferView`, the importer SHALL:

1. Resolve the bufferView to its underlying `tinygltf::Buffer` slice (offset + length).
2. Determine the encoded format from `image.mimeType` (`image/jpeg`, `image/png`, `image/bmp`).
3. Construct the engine `Texture` and call a new `Texture::CreateFromMemory(bytes, len, srgb, mipmap)` API to upload the texture to the GPU.
4. Record the original filename for later use during scene serialization. Preference order: `image.name` if non-empty, else `<scene_stem>_image<i>.<ext>` derived from the input glTF's basename.

The importer SHALL NOT call `Texture::SetFilePathAndSRGB`, SHALL NOT synthesize a temp-file URI, and SHALL NOT write any bytes to disk during this load. The encoded bytes SHALL remain attached to the `Texture` object so they can be extracted later if the scene is saved.

When `image.uri` is non-empty (external-URI case), the importer SHALL keep the existing behavior: pass the URI through to `Texture::SetFilePathAndSRGB` and rely on the scene's working_dir to resolve relative paths. No bytes-in-memory path runs for external-URI images.

#### Scenario: tank.fbx embedded JPEG loads textured without disk hops

- **WHEN** Demo.lua calls `Importer.LoadFbx("Resource/Scene/tank.fbx")` and FBX2glTF emits a `.glb` with `image[0].name = "wheels.jpg"`, `image[1].name = "body.jpg"`, `image[2].name = "grass.jpg"`, all bufferView-backed
- **THEN** each emitted engine `Texture` has its GPU `m_ID != 0` after import (eager upload from memory succeeded)
- **AND** no files are written to any temp directory by the importer
- **AND** the rendered viewport shows the textured tank body, wheels, and grass plane

#### Scenario: External-URI .gltf still works through the file path

- **WHEN** the importer loads a `.gltf` whose `image.uri` is `textures/foo.png` (relative external file)
- **THEN** the existing `SetFilePathAndSRGB` + `CreateFromImage` path runs unchanged
- **AND** no in-memory upload path is invoked for that image

#### Scenario: Importer no longer needs a temp directory for embedded images

- **WHEN** any code path invokes `GltfImporter::Import` (via `LoadGltf`, `LoadFbx`, or the CLI `convert` chain) on a `.glb` with embedded images
- **THEN** no `temp_directory_path()` call is made by the importer
- **AND** no `_image<i>.<ext>` files appear on disk after the import returns

### Requirement: `Texture` SHALL provide `CreateFromMemory` for in-memory image upload and SHALL retain the encoded bytes for later extraction

The `Texture` class SHALL gain:

- `void CreateFromMemory(const unsigned char *bytes, size_t len, bool srgb, bool mipmap)`: Decode the encoded image with `stbi_load_from_memory` (already linked via `STB_IMAGE_IMPLEMENTATION` in `FileUtil.cpp`), upload to the GPU using the same `glTexStorage2D` / `glTexSubImage2D` path as `CreateFromImage`. On success, the texture's `m_ID` is non-zero, `m_Width` / `m_Height` / `m_Format` are set, and the encoded bytes are stored in a private `m_EncodedBytes` member alongside `m_OriginalFilename`.
- A way to expose the encoded bytes to the save path. Either: (a) the save-time extraction code in `FileUtil` is `friend` of `Texture` and reads `m_EncodedBytes` directly, or (b) the API surfaces `GetEncodedBytes() const -> const std::vector<unsigned char>&` and `GetOriginalFilename() const -> std::string`. The choice is non-normative; the spec only mandates that the bytes survive from import to save.

The `Texture::Save` serializer SHALL not embed bytes inline in JSON. The disk-extraction step in `FileUtil::SaveFile` / `SaveCompressedFile` (see the `embedded-textures` spec) is responsible for writing the bytes to a sibling file and setting `m_FilePath` before serialization runs.

#### Scenario: CreateFromMemory uploads and retains bytes

- **WHEN** the importer calls `texture->CreateFromMemory(jpeg_bytes, jpeg_len, /*srgb=*/true, /*mipmap=*/true)`
- **THEN** the texture's `m_ID` is non-zero (GPU upload succeeded)
- **AND** the texture's encoded bytes are retrievable later (via friend access or getter) — `len` bytes, identical to the input
- **AND** the texture's `m_FilePath` is empty (no disk path yet)

#### Scenario: Texture round-trip after save extracts to file

- **WHEN** a scene with an in-memory texture is saved via `FileUtil::SaveFile` (which triggers the save-time extraction step)
- **THEN** the texture's `m_FilePath` is set to the relative or absolute path of the extracted sibling file
- **AND** the saved JSON's texture entry has `path` pointing at that file


### Requirement: The importer SHALL translate glTF skins into engine Joint trees and emit per-vertex skin data

For each `tinygltf::Skin` referenced by a node, the importer SHALL build an engine `Joint` tree, populate each joint's `m_OffsetMatrix` from the skin's `inverseBindMatrices` accessor, and emit `IDs` (4 `uint` indices per vertex) and `Weights` (3 explicit `float` weights per vertex, with the 4th implicit as `1 - sum`) into the corresponding engine `Mesh`. The importer SHALL register every emitted `Joint` in the mesh's `m_JointMap` and `m_Joints` vector, and SHALL set the mesh's `m_RootJoint`. Skinned meshes thus produced SHALL survive a full `FileUtil::SaveFile` → `FileUtil::LoadFile` round-trip with no data loss (which requires fixing the existing Mesh.cpp:120 `// TODO: no joints yet` gap).

#### Scenario: glTF skin emits a Joint tree
- **WHEN** the importer processes a glTF model with one skin containing 20 joints
- **THEN** the emitted engine `Mesh` has 20 entries in its `m_Joints` vector and `m_JointMap`
- **AND** `m_RootJoint` points at the joint corresponding to the skin's `skeleton` node (or the common ancestor of the joints if `skeleton` is absent)

#### Scenario: inverseBindMatrices populate joint offsets
- **WHEN** the glTF skin specifies `inverseBindMatrices` via an accessor
- **THEN** each emitted `Joint`'s `m_OffsetMatrix` matches the corresponding `inverseBindMatrices` entry

#### Scenario: Per-vertex skin data emitted
- **WHEN** a glTF mesh primitive has `JOINTS_0` and `WEIGHTS_0` accessors
- **THEN** the emitted engine `Mesh`'s `IDs` array contains 4 `uint` values per vertex (the joint indices) and `Weights` contains 3 `float` values per vertex (the first 3 weights; the 4th is implicit)

#### Scenario: Skinned-mesh round-trip
- **WHEN** a skinned glTF is converted to `out.json` and then loaded via `FileUtil::LoadFile`
- **THEN** the loaded `Mesh` has the same `IDs` and `Weights` data as the source
- **AND** `m_Joints`, `m_JointMap`, and `m_RootJoint` are populated equivalently

### Requirement: The importer SHALL translate glTF animations into engine AnimationClips resampled at 24 fps

For each `tinygltf::Animation`, the importer SHALL emit one engine `AnimationClip` registered in the scene's `EntityManager`. For each animation channel (target node × target path ∈ {translation, rotation, scale}), the importer SHALL emit one engine `AnimationChannel` whose `name` is the target joint or node name, and whose `positions` / `rotations` / `scalings` arrays are resampled keyframes at 24 fps over the animation's time range. Rotation samples SHALL be converted from glTF quaternions to Euler radians for storage in the engine's `KeyFrame.x/y/z`. The clip's `m_TicksPerSecond` SHALL be 24; `m_Duration` SHALL be the source duration in seconds × 24 (frames).

#### Scenario: Animation channel emits engine AnimationChannel
- **WHEN** a glTF animation has one channel targeting `node[5].translation` with 60 keyframes spanning 0.0–2.0 seconds
- **THEN** the emitted engine `AnimationChannel`'s `name` is the name of glTF node 5
- **AND** the channel's `positions` array contains 48 keyframes (2.0s × 24fps) at consecutive ticks 0..47

#### Scenario: Quaternion samples become Euler radians on storage
- **WHEN** a glTF rotation channel samples are quaternions
- **THEN** the emitted engine `AnimationChannel`'s `rotations` keyframes store Euler radians (YXZ order per `MathUtil::QuatToEulerRad`)
- **AND** at playback time the engine `AnimationPlayer` recovers the original orientation within float precision

#### Scenario: Unsupported interpolation mode warns and falls back
- **WHEN** a glTF animation sampler declares `interpolation = "CUBICSPLINE"`
- **THEN** the importer emits a one-time warning per sampler
- **AND** treats it as LINEAR resampling at 24 fps

### Requirement: The importer SHALL reject glTF features that cannot be expressed in the engine's runtime format

The importer SHALL reject with a clear error message any glTF input that uses morph targets, sparse accessors, or buffer views with a non-default `byteStride`. The importer SHALL also reject any glTF input that declares a non-empty `extensionsRequired` (no glTF extensions are supported in v1; LODs are produced in-engine rather than via the `MSFT_lod` extension). Rejections SHALL exit with code 1 and a single stderr message identifying the offending feature and the source asset name.

#### Scenario: Morph targets rejected
- **WHEN** a glTF mesh primitive has a non-empty `targets` array
- **THEN** the converter exits with code 1
- **AND** stderr contains a message naming `morph targets` as unsupported

#### Scenario: Sparse accessor rejected
- **WHEN** a glTF accessor has a `sparse` field set
- **THEN** the converter exits with code 1
- **AND** stderr names `sparse accessors` as unsupported

#### Scenario: Required extension rejected
- **WHEN** a glTF file has `extensionsRequired = ["KHR_materials_unlit"]`
- **THEN** the converter exits with code 1
- **AND** stderr names the unsupported extension(s)

### Requirement: The importer SHALL translate `KHR_lights_punctual` entries into engine `Light` components

For each entry in `tinygltf::Model::lights`, the importer SHALL build an engine `Light` prototype according to the mapping below. For each `tinygltf::Node` whose `light` index is non-negative, the importer SHALL clone the corresponding prototype and attach it as a `Light` component on the emitted `SceneNode`.

Mapping:

- `tinygltf::Light::type == "point"` → `LightType::POINT`. The `range` field (float, glTF units) SHALL map to the engine `Light`'s radius. When `range` is 0 or unset, the engine's existing default radius applies.
- `tinygltf::Light::type == "spot"` → `LightType::SPOT`. `range` maps to radius as above. `spot.innerConeAngle` and `spot.outerConeAngle` (radians, per the glTF spec) SHALL map directly (no conversion) onto the engine's inner-cone and outer-cone fields.
- `tinygltf::Light::type == "directional"` → `LightType::DIRECTIONAL`. `range` is ignored.
- `tinygltf::Light::color` (linear-space float[3]) SHALL be copied to the engine `Light`'s color.
- `tinygltf::Light::intensity` SHALL be copied 1:1 to the engine `Light`'s intensity. (PBR-correct luminous-flux conversion is deferred; v1 is unit-less pass-through.)

The importer SHALL log exactly one info-level line per imported light naming the source node, light type, and intensity. If a node references a light index that is out of range, the importer SHALL log a warning and skip that node's light without aborting the rest of the import.

When `tinygltf::Model::lights` is empty AND no lights are emitted, the importer SHALL log a single warning advising "imported scene has no lights — viewport will render black under deferred Lambert pipeline." This is informational; the import still succeeds.

#### Scenario: glTF point light is translated to a POINT light on the corresponding SceneNode

- **WHEN** the importer processes a glTF model whose `model.lights` contains a `{type:"point", color:[1,0.8,0.5], intensity:5, range:8}` entry, and a node with `node.light` pointing at that entry
- **THEN** the emitted `SceneNode` has a `Light` component
- **AND** that `Light` has `GetType() == LightType::POINT`
- **AND** its color is approximately `(1, 0.8, 0.5)`, its intensity is approximately `5`, and its radius is approximately `8`

#### Scenario: glTF spot light cone angles are preserved without conversion

- **WHEN** the importer processes a glTF spot light with `innerConeAngle = 0.4`, `outerConeAngle = 0.9` (both radians)
- **THEN** the emitted engine `Light` has inner and outer cone fields equal to `0.4` and `0.9`

#### Scenario: glTF directional light translates to DIRECTIONAL

- **WHEN** the importer processes a glTF light with `type:"directional"`
- **THEN** the emitted engine `Light` has `GetType() == LightType::DIRECTIONAL`
- **AND** the light's `range` field (if any in the source) is ignored

#### Scenario: Scenes with no lights produce a deferred-Lambert warning

- **WHEN** the importer processes a glTF model whose `model.lights` is empty (e.g., a mesh-only export)
- **THEN** the import succeeds (returns a non-null `Scene`)
- **AND** the log contains exactly one warning naming the asset and stating "viewport will render black under deferred Lambert"

#### Scenario: Outdoor demo asset's fire light survives an FBX → glTF → engine round-trip

- **WHEN** a user clicks `File → Open → outdoor.fbx` in Demo.lua (which triggers `Importer.LoadFbx`, which invokes FBX2glTF and then `GltfImporter::Import` on the resulting `.glb`)
- **AND** the source FBX carries a point light at the campfire location
- **THEN** the imported scene contains a `SceneNode` with a `Light` component of `LightType::POINT` at the campfire location
- **AND** the viewport renders the surrounding geometry illuminated by that point light (not black)

### Requirement: The importer SHALL build an LOD chain on the source mesh from glTF meshes that share a name suffix of the form `_LOD<N>`

When the importer's mesh pass produces a set of engine `Mesh` records whose names match the pattern `<base>_LOD<N>` (e.g. `Tree_LOD0`, `Tree_LOD1`, `Tree_LOD2`), the importer SHALL group them into an LOD chain ordered by `N` ascending. The chain is stored on the source mesh (the `LOD0` entry) via `Mesh::SetLodMeshes`; the thresholds default to a `1.0 → 0.0` linear ramp. A group is only formed when at least 2 matching `<base>` names exist; a single `Foo_LOD0` mesh is kept as a plain `Mesh` and the importer logs `FURYD` noting the lone `_LOD<n>` mesh. The group is skipped (with a `FURYI` log) if the source mesh is already part of an existing chain.

#### Scenario: Group by name suffix

- **WHEN** a glTF model declares meshes named `Tree_LOD0`, `Tree_LOD1`, `Tree_LOD2`
- **THEN** the importer emits a 3-entry LOD chain with thresholds `{1.0, 0.5, 0.0}` and meshes `[Tree_LOD0, Tree_LOD1, Tree_LOD2]`
- **AND** the chain is attached to the `Tree_LOD0` mesh (the source / LOD 0)
- **AND** a `FURYI` log line names the group and its base mesh name

#### Scenario: Single `_LOD<N>` mesh is not grouped

- **WHEN** a glTF model declares only `Foo_LOD0` (no `Foo_LOD1`)
- **THEN** no LOD chain is formed
- **AND** `Foo_LOD0` is a plain `Mesh` registered in the `EntityManager` as today
- **AND** a `FURYD` log notes the lone `_LOD<n>` mesh

#### Scenario: Non-contiguous LOD indices are accepted

- **WHEN** a glTF model declares `Tree_LOD0` and `Tree_LOD2` (no `Tree_LOD1`)
- **THEN** the importer emits a 2-entry LOD chain with thresholds `{1.0, 0.0}` and meshes `[Tree_LOD0, Tree_LOD2]`
- **AND** a `FURYW` warns about the gap

### Requirement: Imported light radii SHALL be converted from glTF metres to engine centimetres

For `KHR_lights_punctual` point and spot lights, glTF `range` is specified in metres while the engine unit is 1 cm. The importer SHALL set the engine light radius to `range × 100 / world_scale_of_light_node`, where the division compensates the deferred pipeline's volume sizing (`world_matrix × radius`) — the importer strips light-node local scale to 1 (the FBX cm-to-m parent-scale fix), so the divisor is the parent chain's world scale. A `range` of 0 / unset (infinite per spec) SHALL keep the engine-units default radius (10 units), matching hand-authored scenes.

#### Scenario: FBX fire light with no authored range matches the engine default

- **WHEN** an FBX-derived glTF's point light has `range == 0`
- **THEN** the imported light's radius is 10 engine units (identical to a hand-authored scene's value)

#### Scenario: Metre-authored range converts to centimetres

- **WHEN** a glTF point light has `range = 1.5` and its node sits under a 1× parent
- **THEN** the imported light's radius is 150 engine units

#### Scenario: FBX 100× parent scale does not double-apply

- **WHEN** a glTF point light has `range = 2` and its node's parent chain carries a 100× scale (light-node local scale stripped to 1)
- **THEN** the imported light's radius is 2 engine units
- **AND** the rendered light volume spans 200 engine units in world space

#### Scenario: Scaled scene keeps light contribution

- **WHEN** a scene containing a point light with radius 10 is scaled ×100 at the root (e.g. the editor's import auto-scale)
- **THEN** the light's effective attenuation radius is 1000 units (`radius × node world scale`)
- **AND** shadow-caster collection and shadow projection use the same effective radius (no empty caster set, no contribution loss)

### Requirement: Imported scenes SHALL self-register their node hierarchy with the scene manager

Before returning, `GltfImporter::Import` SHALL register the imported root and all descendants into the returned scene's `SceneManager` (`AddSceneNodeRecursively`), so the scene is renderable as-is via `Scene.SetActive(imported)` + `Pipeline.Execute` without a `MergeInto` round-trip. (`SceneNode::AddChild` does not register nodes; previously only `MergeInto` and `Scene::Load` performed registration.)

The `Importer.MergeInto` Lua entry point SHALL remove each moved top-level child from the SOURCE scene manager (recursively) before attaching it to the target root, so the discarded source scene's octree destruction cannot wipe the target-tree back-pointer (`OcTreeNode::Clear` calls `SetOcTreeNode(nullptr)` on every node it still holds).

#### Scenario: Direct SetActive renders an imported scene

- **WHEN** a script imports a glTF via `Importer.LoadGltf` and calls `Scene.SetActive` on the returned scene without `MergeInto`
- **THEN** every mesh node in the imported hierarchy is visible to the render query (a static skinned mesh renders even with no Animator)

#### Scenario: MergeInto remains clean after self-registration

- **WHEN** an imported (self-registered) scene is merged into the active scene via `Importer.MergeInto`
- **THEN** the moved nodes are registered in the target scene manager exactly once
- **AND** destroying the imported scene does not clear the target-tree back-pointers of the moved nodes

### Requirement: The importer SHALL generate normals for NORMAL-less primitives in a user-selectable smooth or flat mode

For a glTF primitive without a `NORMAL` attribute (the glTF spec says the loader should generate them), the importer SHALL generate per-vertex normals in one of two modes controlled by `GltfImporter::Options::NormalGen`:

- **Smooth (default)** — face normals (area-weighted cross products) are accumulated per POSITION, welding vertices whose positions are bit-identical (exporter-split meshes with per-face vertices still shade smoothly), then normalized and broadcast to every vertex in the weld group.
- **Flat** — each triangle's normalized face normal is assigned to its three corners. On shared-vertex topology later triangles sharing a vertex win (true flat shading requires the exporter's split vertices).

The mode SHALL be exposed on the Lua import entry points as an optional second argument: `Importer.LoadGltf(path, normal_mode)`, `Importer.LoadFbx(path, normal_mode)`, `Importer.LoadScene(path, normal_mode)` where `normal_mode` is `"smooth"` (default when omitted) or `"flat"`; an unrecognized value SHALL log a warning and fall back to `"smooth"`. The mode SHALL apply to all import paths equally (direct glTF, FBX via FBX2glTF, and the editor's File → Open / File → Import flows).

The editor SHALL surface the mode in Settings → Import as a `Normal Gen` combo (`Smooth (default)` / `Flat`) backed by a `normals_smooth` import flag (default `true`), following the `auto_default_sun` pattern; `examples/Editor.lua` SHALL read the flag and pass the matching string to every `Importer.LoadScene` call.

#### Scenario: Default is smooth with position welding

- **WHEN** a glTF mesh whose primitives omit `NORMAL` and whose vertices are duplicated per-face is imported with no `normal_mode` argument
- **THEN** every vertex gets a normalized normal accumulated across all triangles sharing its exact position
- **AND** adjacent faces sharing positions shade without a hard seam

#### Scenario: Flat mode produces per-face normals

- **WHEN** the same mesh is imported with `normal_mode = "flat"`
- **THEN** each triangle's three corners receive that triangle's normalized face normal
- **AND** the mesh shades with visible facets

#### Scenario: Settings combo drives editor imports

- **WHEN** the user picks `Flat` in Settings → Import → Normal Gen and imports a NORMAL-less glTF via File → Import
- **THEN** the imported mesh carries flat (per-face) normals

#### Scenario: Meshes with authored normals are unaffected

- **WHEN** a glTF primitive provides a `NORMAL` attribute
- **THEN** the importer copies the authored normals verbatim regardless of the mode setting
