# lua-scene-scripting

## Purpose

Lua bindings (in `engine/Fury/LuaBindings.cpp`) that expose scene-mutation and scene-introspection capabilities to user scripts — the `Mesh` and `Material` usertypes, the `MeshUtil` / `MeshSimplifier` namespace tables, and the `Scene.ForEach*` iteration helpers. These surface area lets Lua scripts (notably `fury exec <scene> <script.lua>`) inspect, transform, simplify, and re-save loaded scenes.

## Requirements

### Requirement: The `Mesh` usertype SHALL expose vertex/index buffers, submeshes, AABB, and LOD chain accessors

`Mesh` SHALL be registered as a Lua usertype (via `sol::new_usertype<Mesh>` appended to `engine/Fury/LuaBindings.cpp` per the recipe at `docs/LUA.md:612-621`) with the following members. Buffer accessors round-trip via flat Lua tables (not `sol::as_container` views) — see design decision D6.

- `:GetName() / :SetName(name)` — mesh name string.
- `:GetAABB()` — returns a `BoxBounds`.
- `:GetPositions() / :SetPositions(tbl)` — flat Lua table `{x0,y0,z0,...}` round-trip.
- `:GetNormals() / :SetNormals(tbl)` — same shape.
- `:GetUVs() / :SetUVs(tbl)` — same shape.
- `:GetTangents() / :SetTangents(tbl)` — same shape.
- `:GetBoneIds() / :SetBoneIds(tbl)` — flat int table.
- `:GetBoneWeights() / :SetBoneWeights(tbl)` — flat float table.
- `:GetIndices() / :SetIndices(tbl)` — flat int table.
- `:GetSubmeshCount()` — int.
- `:GetSubmeshIndices(i)` — flat int table for submesh `i`.
- `:GetLodCount()`, `:GetLodMesh(i)`, `:SetLodMeshes(meshes_tbl, thresholds_tbl)`, `:ClearLodChain()` — direct mirror of `Mesh.h:227-236`.

#### Scenario: Round-trip mesh positions
- **WHEN** a script calls `local p = mesh:GetPositions()` then `mesh:SetPositions(p)`
- **THEN** the mesh's positions buffer is unchanged (round-trip is identity)
- **AND** `#p` equals the original position count × 3

#### Scenario: Set LOD chain on a mesh
- **WHEN** a script calls `mesh:SetLodMeshes({lod1, lod2}, {0.5, 0.0})`
- **THEN** `mesh:GetLodCount() == 2`
- **AND** `mesh:GetLodMesh(0)` returns `lod1`
- **AND** `mesh:GetLodMesh(1)` returns `lod2`

#### Scenario: Clear LOD chain
- **WHEN** a script calls `mesh:ClearLodChain()` after `SetLodMeshes`
- **THEN** `mesh:GetLodCount() == 0`

#### Scenario: Submesh indices access
- **WHEN** a script calls `mesh:GetSubmeshIndices(0)`
- **THEN** the returned table is a flat int table of indices for submesh 0
- **AND** `mesh:GetSubmeshCount()` reflects the total number of submeshes

### Requirement: The `Material` usertype SHALL expose name, opaque flag, and uniform/texture get/set by string key

`Material` SHALL be registered as a Lua usertype with:

- `:GetName() / :SetName(name)`.
- `:IsOpaque() / :SetOpaque(bool)`.
- `:GetUniform(key)` — returns the uniform value (number for `Uniform1f`/`Uniform1ui`; table for `Uniform3f`/`Uniform4f`) or `nil` if absent.
- `:SetUniform(key, value)` — sets the uniform value, inferring the uniform type from the Lua value's shape (`number` → `Uniform1f`, `{x,y,z}` → `Uniform3f`, `{x,y,z,w}` → `Uniform4f`, integer-valued `number` → `Uniform1ui`).
- `:GetTexture(key)` — returns the texture path string or `nil`.
- `:SetTexture(key, path)` — sets the texture path; the engine resolves the path against `Scene::Active`'s working directory at render time.
- `:GetTextureCount()` — int.

The set of valid uniform keys matches the keys documented in `docs/CLI.md`'s scene-format appendix (`diffuse_color`, `diffuse_factor`, `specular_color`, `specular_factor`, `ambient_color`, `ambient_factor`, `emissive_color`, `emissive_factor`, `transparency`, `shininess`, `material_id`).

#### Scenario: Set diffuse color (vec4)
- **WHEN** a script calls `material:SetUniform("diffuse_color", {1, 0, 0, 1})`
- **THEN** `material:GetUniform("diffuse_color")` returns `{1, 0, 0, 1}` (a 4-element Lua table)

#### Scenario: Set scalar uniform
- **WHEN** a script calls `material:SetUniform("shininess", 32.0)`
- **THEN** `material:GetUniform("shininess")` returns `32.0`

#### Scenario: Get absent uniform returns nil
- **WHEN** a script calls `material:GetUniform("nonexistent_key")`
- **THEN** the return value is `nil`

#### Scenario: Set texture by key
- **WHEN** a script calls `material:SetTexture("diffuse", "textures/foo.png")`
- **THEN** `material:GetTexture("diffuse")` returns `"textures/foo.png"`

### Requirement: The `MeshUtil` namespace table SHALL expose primitive-mesh factory and mesh-processing utilities

A `MeshUtil` Lua table SHALL be registered with:

- `MeshUtil.CreateCube()`, `MeshUtil.CreateQuad()`, `MeshUtil.CreateSphere(segments)`, `MeshUtil.CreateIcoSphere(subdivisions)`, `MeshUtil.CreateCylinder(segments)` — return `Mesh` instances. Mirrors `MeshUtil.h:46-55`.
- `MeshUtil.TransformMesh(mesh, matrix)` — applies a 4x4 transform (passed as a flat Lua table of 16 numbers, row-major) to a mesh's positions/normals/tangents in place. Mirrors `MeshUtil.h:57`.
- `MeshUtil.OptimizeMesh(mesh)` — dedupes vertices in place. Mirrors `MeshUtil.h:60`.
- `MeshUtil.CalculateNormal(mesh)` — recomputes normals in place. Mirrors `MeshUtil.h:63`.
- `MeshUtil.CalculateTangent(mesh)` — recomputes tangents in place. Mirrors `MeshUtil.h:67`.

#### Scenario: Create unit cube
- **WHEN** a script calls `local m = MeshUtil.CreateCube()`
- **THEN** `m` is a `Mesh` instance
- **AND** `m:GetAABB()` is a unit cube centered at origin

#### Scenario: Optimize a mesh with duplicate vertices
- **WHEN** a script calls `MeshUtil.OptimizeMesh(mesh)` on a mesh with duplicate vertices
- **THEN** the position buffer is shorter than before
- **AND** the index buffer is rebased to the new vertex buffer

#### Scenario: Recompute normals
- **WHEN** a script calls `MeshUtil.CalculateNormal(mesh)` on a mesh whose normals were zeroed
- **THEN** `mesh:GetNormals()` returns a non-zero buffer of the expected length

### Requirement: The `MeshSimplifier` namespace table SHALL expose `SimplifyMesh` with a plain-table options argument

A `MeshSimplifier` Lua table SHALL be registered with:

- `MeshSimplifier.SimplifyMesh(mesh, opts)` — returns `{ lod_meshes = {mesh, mesh, ...}, thresholds = {t1, t2, ...} }`.

The `opts` argument is a plain Lua table with optional keys. All keys are optional; defaults match `MeshSimplifyOptions` defaults (`MeshSimplifier.h:17-44`):

- `lod_count` (default 3, clamped to [1, 5]) — number of LOD levels to generate.
- `reduction_ratio` (default 0.5, range (0, 1)) — per-level reduction factor.
- `target_error` (default 0.5) — `meshopt_simplify` error tolerance.
- `lock_borders` (default true) — currently unused in C++ implementation (`use_sloppy=true` is hardcoded per `MeshSimplifier.cpp:242`); accepted for forward compatibility.

A `MeshSimplifyOptions` usertype is NOT registered — opts is always a plain Lua table. This matches the convention of other namespace-table functions (e.g., `Importer.LoadScene`).

#### Scenario: Generate 3 LODs with defaults
- **WHEN** a script calls `local r = MeshSimplifier.SimplifyMesh(mesh, {})` on a mesh with 1000 triangles
- **THEN** `#r.lod_meshes == 3`
- **AND** `r.lod_meshes[1]` has fewer triangles than `mesh`
- **AND** `r.lod_meshes[2]` has fewer triangles than `r.lod_meshes[1]`
- **AND** `r.lod_meshes[3]` has fewer triangles than `r.lod_meshes[2]`
- **AND** `#r.thresholds == 3`
- **AND** `r.thresholds[1] > r.thresholds[2] > r.thresholds[3]`

#### Scenario: Custom lod_count
- **WHEN** a script calls `MeshSimplifier.SimplifyMesh(mesh, { lod_count = 2 })`
- **THEN** `#r.lod_meshes == 2`
- **AND** `#r.thresholds == 2`

#### Scenario: Simplify result is attachable to a Mesh LOD chain
- **WHEN** a script calls `local r = MeshSimplifier.SimplifyMesh(mesh, {})` then `mesh:SetLodMeshes(r.lod_meshes, r.thresholds)`
- **THEN** `mesh:GetLodCount() == #r.lod_meshes`

#### Scenario: Opts table omitted entirely
- **WHEN** a script calls `MeshSimplifier.SimplifyMesh(mesh)` (no second argument)
- **THEN** the call uses default options (equivalent to `{}`)
- **AND** returns a 3-LOD result

### Requirement: The `Scene` usertype SHALL expose `ForEachMesh`, `ForEachMaterial`, and `ForEachNode` iteration helpers

`Scene` SHALL expose (in addition to its existing bindings in `LuaBindings.cpp:200-219`):

- `Scene.ForEachMesh(fn)` — calls `fn(mesh)` for every `Mesh` in the scene's `EntityManager`. Equivalent to `EntityManager::ForEach<Mesh>`.
- `Scene.ForEachMaterial(fn)` — calls `fn(material)` for every `Material` in the scene's `EntityManager`.
- `Scene.ForEachNode(fn)` — recursively walks the scene tree from `GetRootNode()`, calling `fn(node)` on each `SceneNode` (pre-order traversal: parent before children).

A non-nil return from `fn` SHALL short-circuit the iteration (matching `EntityManager::ForEach`'s convention).

#### Scenario: Count meshes
- **WHEN** a script runs `local count = 0; Scene.GetActive():ForEachMesh(function(m) count = count + 1 end)`
- **THEN** `count` equals the number of meshes in the scene's `EntityManager`

#### Scenario: Count materials
- **WHEN** a script runs `local count = 0; Scene.GetActive():ForEachMaterial(function(m) count = count + 1 end)`
- **THEN** `count` equals the number of materials in the scene's `EntityManager`

#### Scenario: Walk the node tree
- **WHEN** a script runs `local count = 0; Scene.GetActive():ForEachNode(function(n) count = count + 1 end)`
- **THEN** `count` equals the total number of `SceneNode`s in the scene tree (including root)

#### Scenario: Short-circuit on non-nil return
- **WHEN** a script runs `Scene.GetActive():ForEachMesh(function(m) return true end)` on a scene with N meshes
- **THEN** the callback is invoked exactly once (on the first mesh)
- **AND** iteration stops immediately after the first non-nil return

#### Scenario: ForEachNode visits parent before children
- **WHEN** a script runs `Scene.GetActive():ForEachNode(function(n) print(n:GetName()) end)` on a scene with a root `R` and child `C`
- **THEN** `R` is visited before `C` (pre-order traversal)