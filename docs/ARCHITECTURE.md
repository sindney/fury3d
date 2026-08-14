# Fury3D — Engine Architecture

> Snapshot of the engine as it exists today (`engine/Fury/`, ~23k LOC of C++11),
> written as input to a refactor that will:
>
> - Add pytest-driven unit tests for core modules (math first).
> - **Keep SFML** for windowing / input / OS glue, but swap its GL context for
>   an **ANGLE** EGL surface so the upstream API stays GLES while the native
>   backend is Vulkan / Metal / D3D11.
> - Treat **ANGLE GLES 3.x as the RHI abstraction itself** — no extra
>   `RHI::Device / Buffer / Texture` interface layer for now. The renderer
>   keeps calling GLES; ANGLE handles native translation.
> - Drop FBX SDK and adopt **glTF 2.0** as the canonical asset format,
>   loading via the vendored **tinygltf** (the in-tree `GLTFDom` will be
>   retired in favour of it).
> - Vendor **sol2** for Lua bindings *after* the engine boundary stabilises,
>   so demos can be authored as scripts without recompiling the engine.
>
> This doc is descriptive only. Refactor plans live elsewhere.

---

## 1. 30-second summary

Fury3D is a study-grade rendering engine. The interesting parts are not its
graphics features (it ships a textbook light-pre-pass / deferred renderer with
shadow maps for dir / point / spot lights), but the way it is **declaratively
configurable**:

- The render **pipeline** is described in JSON — passes, render-targets,
  shaders, blend / clear / draw modes — and instantiated at runtime.
- **Scenes** are imported from FBX or loaded from a custom JSON / LZ4-compressed
  binary.
- A **component model** (`SceneNode` + typed `Component` map) hangs `Transform`,
  `MeshRender`, `Light`, `Camera` etc. off scene nodes.
- An **entity manager** (per-scene `EntityManager`) keeps deduplicated registries
  of meshes, materials, textures, animation clips, etc., keyed by name +
  hashcode.

All of this composes into a friendly target for an AI / tool-driven workflow:
text in, scene out.

The four hard couplings affected by the refactor:

| Coupling                  | Where it lives                                          | Plan                                                       |
|---------------------------|---------------------------------------------------------|------------------------------------------------------------|
| **SFML** windowing/input  | `Engine`, `Gui`, `InputUtil`                            | **Keep.** Replace its GL context with ANGLE's EGL surface. |
| **Raw GL 3.3**            | `GLLoader`, `Shader`, `Texture`, `Pass`, `ArrayBuffers`, `EnumUtil`, `RenderUtil`, `BufferManager` | **Keep call sites.** Rebind to **ANGLE GLES 3.x**; ANGLE *is* the abstraction. Retire `GLLoader` (replaced by ANGLE's GLES headers). |
| **FBX SDK**               | `FbxParser` (gated by `_FURY_FBXPARSER_IMP_`)           | **Drop.** Replace with a tinygltf-based importer.          |
| **rapidjson via `void*`** | `Serializable`                                          | Outstanding — flagged but not yet decided.                 |

Everything else — math, scene graph, octree, animation, threading, signal —
is platform-neutral and survives the refactor mostly intact.

---

## 2. Directory layout

```
fury3d/
├── engine/
│   ├── Fury/              # all engine source (one flat dir, ~50 modules)
│   ├── ThirdParty/        # ImGui, LZ4, STB (vendored)
│   └── CMakeLists.txt
├── examples/
│   ├── Demo.cpp           # SFML-driven sample app
│   └── bin/Resource/      # shaders, scenes, pipeline JSON
└── README.md
```

External (this refactor's) siblings:

```
furyengine/
├── fury3d/    ← this engine
├── angle/     ← Google ANGLE (GLES → Vulkan / Metal / D3D / WebGPU). Used as
│                the GLES driver underneath the existing renderer call sites.
├── sfml/      ← SFML 2.x (kept). Windowing, input, OS glue. We use it without
│                its OpenGL context creation — ANGLE/EGL builds the surface
│                from sf::WindowBase::getSystemHandle().
├── tinygltf/  ← Single-header glTF 2.0 loader. Replaces FbxParser and
│                supersedes the partial in-tree GLTFDom.
├── sol2/      ← Lua/C++ binding library. Deferred until the engine API
│                stabilises; will expose Scene / SceneNode / Pipeline /
│                Input signals to scripts.
└── gltf/      ← Khronos glTF 2.0 *spec* repo (not a loader library).
                 Reference only, kept for spec lookups.
```

> ⚠️ **Note on `gltf/`** — it contains `specification/`, `extensions/`,
> `LICENSES/` etc. but **no C/C++ parser**. The actual parser is
> `tinygltf` (above); the spec repo is kept only as a reference.

---

## 3. Module map

```
                        ┌──────────────────┐
                        │     Engine       │  static, SFML-tied
                        │  (init+loop)     │
                        └────────┬─────────┘
                                 │ owns init order
        ┌────────────────────────┼──────────────────────────────┐
        ▼                        ▼                              ▼
  ┌───────────┐           ┌─────────────┐                ┌─────────────┐
  │ ThreadUtil│           │  GLLoader   │                │  InputUtil  │
  │  Log      │           │  RenderUtil │                │  Gui (ImGui)│
  │  Signal   │           │  BufferMgr  │                └─────────────┘
  └───────────┘           └──────┬──────┘
                                 │
                ┌────────────────┼────────────────┐
                ▼                ▼                ▼
          ┌──────────┐    ┌──────────┐     ┌──────────────┐
          │ Pipeline │◄──►│   Pass   │◄───►│   Texture    │
          │ Prelight │    │   Shader │     │   Material   │
          └────┬─────┘    └────┬─────┘     │   Uniform    │
               │               │           └──────────────┘
               │               │
               ▼               ▼
          ┌──────────────────────┐
          │  RenderQuery         │  populated by OcTree culling
          └──────────┬───────────┘
                     │
         ┌───────────┴────────────┐
         ▼                        ▼
   ┌──────────┐            ┌─────────────┐
   │  OcTree  │◄───────────┤  SceneNode  │── component map ──┐
   │ OcTreeNd │            │  (hierarchy)│                   │
   └──────────┘            └──────┬──────┘                   ▼
                                  │              ┌────────────────────┐
                                  │              │ Transform          │
                                  │              │ MeshRender         │
                                  │              │ Camera, Light      │
                                  ▼              └────────────────────┘
                          ┌──────────────┐
                          │    Scene     │── EntityManager (per-type
                          │              │     hashed registries)
                          └──────┬───────┘
                                 │
                ┌────────────────┴───────────────┐
                ▼                                ▼
        ┌───────────────┐               ┌─────────────────┐
        │   FbxParser   │               │ Serializable    │
        │ (FBX SDK)     │               │ (rapidjson + LZ4)│
        └───────────────┘               └────────┬────────┘
                                                 │
                                          ┌──────▼──────┐
                                          │   GLTFDom    │ partial
                                          └──────────────┘
```

---

## 4. Application loop

`Engine` is an all-static façade. There is no engine instance — the user owns
the SFML `sf::Window` and pumps the loop themselves:

```cpp
sf::Window window(/* …gl context… */);
fury::Engine::Initialize(window, guiScale, numThreads, LogLevel::INFO);

sf::Clock clock;
while (window.isOpen()) {
    sf::Event ev;
    while (window.pollEvent(ev)) Engine::HandleEvent(ev);

    Engine::Update(clock.restart().asSeconds()); // emits OnUpdate(dt)
    Engine::FixedUpdate();                        // emits OnFixedUpdate()
    pipeline->Execute(octree);                    // your own draw call
    window.display();
}
Engine::Shutdown();
```

`Engine::Initialize` boots subsystems in this order — important because some
have hard ordering constraints:

1. `Log<0>` (so everything else can log).
2. `ThreadUtil` (and `SetMainThread()` is captured here).
3. `MeshUtil` primitives (Quad / Cube / Sphere / Cylinder, used by debug draw).
4. `InputUtil` (signal stubs created here).
5. `FbxParser` if `_FURY_FBXPARSER_IMP_` is defined.
6. `GLLoader::LoadGLFunctions()` — needs the GL context already current.
7. `RenderUtil`, `BufferManager`, `Gui`.

`Engine` exposes two signals:

- `OnUpdate<float>` — variable-step, every `Update(dt)` call.
- `OnFixedUpdate<>` — currently fired alongside Update; there's no actual fixed
  accumulator (the name is aspirational).

`HandleEvent(sf::Event&)` forwards into `InputUtil` (state mirrors + signal
emits) and `Gui` (ImGui).

---

## 5. Math layer

A thin, header-pair, value-type math library. Single file per type. No SIMD,
no template generality, no glm-style vector aliases — just `Vector4`,
`Quaternion`, `Matrix4`, `Plane`, plus `MathUtil` helpers.

### 5.1 `Vector4` — the only vector

There is **no Vector2 or Vector3 type**. `Vector4` is overloaded for all roles:

- `Vector4(x, y, z)` — used for 3D positions; `w` defaults to 1.
- `Vector4(x, y, z, w)` — full homogeneous control.
- `Vector4(x, y, 0, 0)` for 2D (UVs etc., though the engine often stores those
  as flat float arrays in `ArrayBufferf`).

All arithmetic operators ignore `w` and reset it to `1` on output. Comparisons
operate on `xyz` only. Constants `XAxis / YAxis / ZAxis` are direction vectors
with `w == 0`. The "set `w` from a previous vector" pattern is the
`Vector4(other, w)` constructor.

Implication: **the type is fragile**. If you call `vec.Length()`, `w` does not
contribute (intentional), but a careless `someMatrix * directionVector` with a
positional `w == 1` will translate it. Refactor candidates: an explicit
`Vector3` for directions/positions and a `Vector4` only for homogeneous slots,
or distinct `Direction` / `Point` types. For now it works and the convention is
documented in the header.

### 5.2 `Matrix4`

Column-major storage in a flat `float[16]`, OpenGL convention:

```
Raw[0]  Raw[4]  Raw[8]   Raw[12]    │   m00 m01 m02 tx
Raw[1]  Raw[5]  Raw[9]   Raw[13]    │   m10 m11 m12 ty
Raw[2]  Raw[6]  Raw[10]  Raw[14]    │   m20 m21 m22 tz
Raw[3]  Raw[7]  Raw[11]  Raw[15]    │   0   0   0   1
```

Methods come in `Append…` / `Prepend…` pairs (T·R·S vs S·R·T composition).
Projection helpers: `PerspectiveFov`, `PerspectiveOffCenter`, `OrthoOffCenter`,
`LookAt`. `Multiply` is overloaded for `Vector4`, `Quaternion`, `BoxBounds`,
`Plane`. `Inverse` is the general inverse — doesn't try to be clever about
projection vs affine (no shortcut for view-matrix-style inverses).

### 5.3 `Quaternion`

Stored as `(x, y, z, w)` = `(axis·sin(θ/2), cos(θ/2))`. Operations: `Identity`,
`DotProduct`, `Slerp`, `Conjugate`, `Pow`, `Normalize`, plus `*` for quaternion
multiply. **Conversion to/from a matrix lives in `MathUtil` and `Matrix4`, not
on `Quaternion` itself** — easy to miss.

### 5.4 `MathUtil`

Conversion utilities: `AxisRad ↔ Quaternion ↔ EulerRad` (Euler order is YXZ —
documented in a comment, not enforced anywhere else). `PointInCone`, `PI`,
`HalfPI`, `DegToRad`, `RadToDeg`.

### 5.5 `Plane`

4-coefficient `(a, b, c, d)` form, supplied to `Frustum` for the six culling
planes.

### 5.6 Spatial primitives

- `BoxBounds` — AABB with min / max / extents / center / size precomputed,
  supports `IsInside(point|aabb|sphere)`, `Encapsulate`, `ClosestPoint`,
  `FarestPoint` (sic), `GetVertexP/N` for plane-projection culling, an "infinite"
  flag, dirty-flag lazy recompute, and 8-corner extraction.
- `SphereBounds` — same shape, simpler.
- `Frustum` — owns 8 base corners, 8 transformed corners, 6 planes, and the
  current transform matrix. `Setup(fovy, aspect, near, far)` or off-center.
  `Transform(Matrix4)` recomputes corners and planes. Implements `Collidable`.
- `Collidable` (interface) — `IsInside(point|aabb|sphere)` returning `Side`
  (Inside / Outside / Intersect) plus `IsInsideFast` variants for early-out
  culling.

This whole subset is value-typed, allocation-free, self-contained — perfect
test target for pytest+pybind11 round-tripping.

### 5.1 Coordinate system & units

The engine uses a single, engine-wide convention. It is not configurable per
scene — pick assets that match, or scale them at import.

| Property    | Value                                      |
|-------------|--------------------------------------------|
| Handedness  | Right-handed                               |
| Up axis     | +Y                                         |
| Forward     | -Z (cameras look down -Z; matches glTF)    |
| Unit        | **1 world unit = 1 centimeter**            |

**Why cm:** glTF 2.0 is unitless, but the Khronos
[glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) models
the engine targets (e.g. the Fox, ~155 cm long) are authored in centimetres.
Adopting cm as the engine unit makes those samples render at correct real-world
scale without per-model import scaling. The in-repo `james.fbx` is a
hand-authored model and is **not** a unit reference.

**Consequences for authoring:**
- Camera `near`/`far` (`Camera::PerspectiveFov`) are in cm. The editor default
  is `near=1` (1 cm), `far=5000` (50 m) — covers hand-scale to outdoor scenes.
- Light radii, shadow bounds (`Camera::SetShadowFar`/`SetShadowBounds`), and
  editor move-speed (`move_speed` in `Editor.lua`) are in cm.
- Assets authored in **metres** (1 unit = 1 m) need a ×100 scale at import.
  FBX2glTF-converted FBX models already carry a 100× node scale for the
  cm→m conversion (see `docs/CLI.md §Limitations` and the `james` mesh node);
  pure-metre glTF assets need the scale applied manually or via the importer's
  scale option. The editor guards this: after File → Open / File → Import of a
  `.gltf`/`.glb`/`.fbx`, `examples/Editor.lua` measures the imported scene's
  world AABB (`Scene:ComputeWorldAABB`) and, when the largest dimension is
  under 1 m (100 units), offers to auto-scale the imported root node(s) via a
  Yes/No dialog (`Editor.RequestConfirmDialog`). The factor starts at 100× and
  escalates by powers of 100 from the measured bounds. Settings → Import →
  **Auto-Scale Detection** toggles the check (default on).
- `Matrix4` stores translation in `Raw[12,13,14]` (column-major, OpenGL-style);
  rotation matrices are built from quaternions via `Matrix4::Rotate`. The math
  layer is unit-agnostic — the cm convention is a scene/asset contract, not
  enforced by the math code.

---

## 6. Scene model

### 6.1 Entity / Component / SceneNode

Three layers, all reference-counted:

- **`Entity`** — base for anything serializable. `name`, `hashCode`,
  virtual `Load` / `Save`, plus a `static unordered_map<string, Functor>`
  used as a deserialization registry. `Mesh`, `Material`, `Texture`,
  `AnimationClip`, `Joint`, `SceneNode` etc. all inherit.
- **`Component`** (extends `Entity` + `TypeComparable`) — owned by a single
  `SceneNode` (held as `weak_ptr m_Owner`). Lifecycle hooks
  `OnAttaching` / `OnDetaching` / `OnOwnerDestructing`. Subclasses include
  `Transform`, `MeshRender`, `Camera`, `Light`, `Animator`.
  Components are clonable via `Clone()`.
- **`SceneNode`** (extends `Entity` + `enable_shared_from_this`) — the actual
  hierarchy node. Holds:
  - parent (`weak_ptr`) + children (`vector<shared_ptr>`),
  - typed component map (`unordered_map<type_index, shared_ptr<Component>>`),
  - local TRS + world matrix (with dirty flag and Pre/Post/Current for
    interpolation),
  - local-space and world-space `BoxBounds`,
  - a `weak_ptr<OcTreeNode>` back-pointer,
  - signal `OnTransformChange<SceneNode::Ptr>`.

`SceneNode::Clone()` is a *shallow* clone of components — children are not
cloned. The component-by-type-index lookup means you cannot have two
`MeshRender`s on one node (typical Unity-style restriction).

`TypeComparable` is the CRTP-ish helper that gives every component a
`type_index` for the map lookup.

### 6.2 `Scene`

Owns the root `SceneNode`, a `SceneManager` (octree, typically), an
`EntityManager`, and a working directory used to resolve relative resource
paths. There is a `Scene::Active` pointer for quick global access — a singleton
in disguise.

**File format & versioning.** `FileUtil::SaveFile` writes a scene as plain
JSON; `FileUtil::SaveCompressedFile` writes `[orgSize:u32be][compSize:u32be]`
+ an LZ4 block of the *same* document (one writer, two envelopes —
`.json`/`.bin` can never diverge). The scene root carries a `"version"` field
(`Scene::kFormatVersion`, currently 2); files without it are version 1
(pre-versioning). `Scene::Load` rejects files newer than supported with an
explicit error rather than mis-loading. On save, memory-backed textures are
extracted to PNG siblings and file-backed textures copied + rewritten to bare
filenames (portable-by-default; re-saves skip identical bytes, so only the
first save copies — saving to a *different* directory copies again, which is
the intent).

### 6.3 `EntityManager`

Two-level associative store:

```
map< type_index , map< hashCode , shared_ptr<void> > >
```

Add / Remove / Get / Count / `ForEach<T>` / iterators. The README's
`Scene::Manager()->Get<AnimationClip>("James|Walk")` lookup goes through here.
This is also what makes scenes deduplicated: import a mesh twice with the same
name and the second import will collide.

### 6.4 Octree

`OcTree` extends `SceneManager` and is the canonical spatial index.

- Root spans an explicit `(min, max)` box; children subdivide on demand.
- `OcTreeNode` holds eight children, parent, AABB, leaf flag, an aggregated
  `sceneNodeCount`, and (at leaves) `vector<weak_ptr<SceneNode>>`.
- `GetFitNode(aabb)` walks down to the smallest octant fully containing the
  AABB.
- Query API: `GetVisibleSceneNodes`, `GetVisibleRenderables`,
  `GetVisibleShadowCasters`, `GetVisibleLights` — all take a `Collidable`
  (the camera frustum) and append into a caller-owned vector.
- `WalkScene` accepts a custom filter functor for ad-hoc traversal.

The octree is **static** — no dynamic rebalance. SceneNodes are referenced
from the tree as well as their parent in the hierarchy, so you have a graph
with two ownership chains; the README warns to call `RemoveFromOcTree(true)`
before destroying a node, and `SceneNode::Clone` deliberately does not
re-register with the octree.

---

## 7. Animation

### 7.1 Skeletons (`Joint`)

Joints mirror scene-graph nodes — each `Joint` holds a `weak_ptr<SceneNode>`
to the glTF joint node it corresponds to. The scene graph (not a parallel
joint tree) produces the joint's world matrix `JᵢW`. Each joint stores:

- `m_LocalMatrix` — bind TRS in parent space (kept for inspection / re-link).
- `m_OffsetMatrix` — the glb `inverseBindMatrices` entry (ibm), used verbatim.
- `GetFinalMatrix()` — returns `sceneNodeWorld * m_OffsetMatrix` (the glTF
  formula `JᵢW · ibm`). The skin shader pairs this with an identity model
  matrix so the result lands in world space directly.

Tree links (first-child / sibling / parent pointers) are kept for the joint
visualization overlay and `FindFromRoot` lookups; they are not traversed to
compute skin matrices. Each joint also stores the UUID of its linked
SceneNode (`m_SceneNodeUUID`), persisted in `Mesh::Save`. `Scene::Load`
re-links each joint to its SceneNode by UUID after the node tree loads
(Mesh::Load rebuilds joints without refs; only GltfImporter wires them at
import time). UUIDs make the linkage unambiguous across instances — a name
fallback covers old scenes saved before the uuid field existed.

### 7.2 Clips (`AnimationClip` / `AnimationChannel` / `KeyFrame`)

```cpp
struct KeyFrame { unsigned tick; float x, y, z; };

class AnimationChannel {
    string name;                       // joint or scene-node name
    vector<KeyFrame> rotations;        // x,y,z = euler radians
    vector<KeyFrame> positions;
    vector<KeyFrame> scalings;
};

class AnimationClip {
    vector<ChannelPtr> m_Channels;
    float m_Duration;
    int   m_TicksPerSecond = 24;       // ← FBX-shaped default
    bool  m_Loop = true;
    float m_Speed = 1.0f;
};
```

- Frame-numbered (`tick`), not time-numbered.
- Linear interpolation for translation / scale, slerp for rotation.
- No animation blending, no IK, no morph targets, no curve types beyond
  linear.
- `AnimationUtil::OptimizeAnimClip` discards keyframes whose adjacent vector
  delta is below a threshold.

### 7.3 Playback (`Animator`)

A `Component` (defined in `AnimationPlayer.{h,cpp}`). Holds the registered
`AnimationState`s, current time, blend speed, and wrap mode. Two-phase tick:

1. `AdvanceTime(dt)` — advance each enabled state's time (wrap/clamp by
   `WrapMode`); pick the highest-weight state as dominant; for each channel,
   sample the keyframe tracks and write old/new TRS pairs.
2. `Display(alpha)` — interpolate each target's TRS pairs by render alpha.

Channel targets resolve via `ResolveTargets`: skinned channels resolve to the
owning Mesh's `Joint`, then to that joint's linked `SceneNode`'s `Transform`
(the scene graph's `Recompose` produces `JᵢW`, which `Joint::GetFinalMatrix`
pairs with the ibm). Node-level channels resolve to descendant `SceneNode`s
by name. No parallel joint tree-walk runs in `Display` — the scene graph is
the sole source of joint world matrices.

The shader reads `Final` matrices as a uniform array (skin matrices). The
Mesh's `IDs` and `Weights` buffers (4 indices and 3 explicit + 1 implicit
weight per vertex) feed the standard linear-blend skinning shader.

---

## 8. Asset loading

### 8.0 Asset path scheme (`Scene::ResolveAsset`)

`Scene::ResolveAsset(p)` has two cases:

- `Engine/foo.png` -> `<cwd>/Resource/foo.png` (engine-shared asset
  that ships with fury/furye; cwd is the `examples/` tree on a typical
  `./furye` launch).
- Anything else -> `working_dir + path` (project-local asset that
  lives next to the loaded `.bin`). Equivalent to the legacy
  `Scene::Path(p)`.

The `Engine/` prefix is the only thing distinguishing engine vs
project assets; "raw" paths (no prefix) all flow through the project
case. Use `Engine/` for things that ship with the engine binaries
(pipeline JSONs, atmosphere shaders, cloud/moon textures, etc.) and
raw paths for things that ship with a project (terrain height
images, splatmaps, etc.).

C++ callers of `ResolveAsset`: `Texture::CreateFromImage`,
`Heightmap::LoadMeta`, `Heightmap::Open`. Other engine-asset paths
loaded from Lua (`Resource/Pipeline/...`, `Resource/PostProcess/...`)
still flow through `FileUtil.GetAbsPath(p)` (cwd-relative) and can
migrate to `Engine/...` later for consistency.

### 8.1 FBX path (`FbxParser`)

Single entry point:

```cpp
FbxParser::Instance()->LoadScene(filePath, rootNode, importOptions);
```

Driven by an `FbxImportOptions` bitmask:

```
UV | NORMAL | TANGENT | SPECULAR_MAP | NORMAL_MAP |
OPTIMIZE_MESH | TRIANGULATE |
IMP_ANIM | IMP_POS_ANIM | IMP_SCL_ANIM | BAKE_CURVE_ANIM | OPTIMIZE_ANIM |
AUTO_PAIR_CLIP
```

What it produces, in order:

1. Axis conversion (FBX → Y-up), animation layer baking at 24 fps.
2. Recursive `LoadNode`, building a `SceneNode` tree mirroring FBX hierarchy.
3. Per-mesh: forced triangulation; UV / normal / tangent extraction handling
   both per-control-point and per-polygon-vertex layouts; sub-mesh split by
   material id; tangents either taken from FBX or computed via `MeshUtil`.
4. Per-skeleton: deformer/cluster scan, joint tree reconstruction, offset
   matrix computation.
5. Per-animation stack: 24 fps frame sampling (or curve keys if not baking),
   `AnimationClip` emit, optional optimization pass.
6. Per-material: maps `FbxSurfacePhong` / `FbxSurfaceLambert` → engine
   `Material` (`AMBIENT_*`, `DIFFUSE_*`, `SPECULAR_*`, `EMISSIVE_*`,
   `SHININESS`, plus diffuse/specular/normal textures via stb_image).

`FbxParser` is the source of nearly every FBX-shaped assumption that has
bled into engine types: 4-bone-max skin layout, 24 fps animation tick rate,
Phong/Lambert material model.

### 8.2 glTF path (`GLTFDom`) — partial

`GLTFDom` is a JSON-only DOM of glTF 2.0:

```cpp
vector<GLTFBuffer::Ptr>     Buffers;
vector<GLTFBufferView::Ptr> BufferViews;
vector<GLTFAccessor::Ptr>   Accessors;
vector<GLTFNode::Ptr>       Nodes;
vector<GLTFMesh::Ptr>       Meshes;
vector<GLTFSkin::Ptr>       Skins;
vector<GLTFAnimation::Ptr>  Animations;
vector<GLTFMaterial::Ptr>   Materials;     // PBR metallic-roughness only
vector<GLTFTexture::Ptr>    Textures;
vector<GLTFImage::Ptr>      Images;
vector<GLTFSampler::Ptr>    Samplers;
```

What it does **not** do:

- No buffer-view `byteStride` (returns an error if seen).
- No sparse accessors.
- No extensions (morph targets explicitly rejected).
- No `Save()` — load-only.
- **No conversion to engine types**. There is no `GLTFParser` analogous to
  `FbxParser`. `FileUtil::LoadGLTFFile` exists but its `Private…` body is a
  stub — it wires `AccessBuffer<T>` against `.bin` files but does not
  populate the scene graph.

So the glTF pipeline today reaches "I parsed the JSON" and stops.

### 8.3 Custom JSON / LZ4 (`Serializable` + `FileUtil`)

Generic `Serializable` interface — virtual `Load(void* json)` and
`Save(void* writer)`, where the `void*` is a `rapidjson::Value` or
`rapidjson::Writer<StringBuffer>`. Static helpers cover bool / int / uint /
float / string / `Color` / `Vector4` / `Quaternion` / `Matrix4` / `BoxBounds`,
plus `LoadArray(walker)` / `SaveArray(walker)` and object brackets.

Round-trip on disk:

```cpp
FileUtil::SaveCompressedFile(scene, "scene.bin");   // rapidjson → string → LZ4
FileUtil::LoadCompressedFile(scene, "scene.bin");
FileUtil::SaveFile(scene, "scene.json");            // human-readable
FileUtil::LoadFile(scene, "scene.json");
```

Caveats from reading the code:

- `Mesh::Save` writes positions / normals / tangents / UVs / indices /
  submeshes / aabb / cast-shadows. Skinned meshes also round-trip
  `bone_ids`, `bone_weights`, and a flat `joints` array (with parent
  indices) + `root_joint` name. The skin keys are emitted only when
  present, so static-mesh scene files remain byte-identical to
  pre-skin-roundtrip output. (Closed by the add-cli-gltf-convert change;
  the `TODO: no joints yet` in Mesh.cpp is gone.)
- The `void*` opacity means a rapidjson API change cascades through the
  whole codebase, and there is no schema validation.

### 8.4 Texture loading

`Texture::CreateFromImage(path, srgb, mipMap)` reads via `stb_image`
(JPEG / PNG / BMP). `Texture::GetTemporary(...)` is a render-target pool
(reused across frames for transient targets). The loaded texture object also
holds GL handles directly — load and GPU upload are the same call.

---

## 9. Rendering pipeline

### 9.1 Pass / Pipeline data model

The pipeline is JSON-described at runtime. Excerpt from
`examples/Resource/Pipeline/DefferedLightingLambert.json`:

```jsonc
{
    "name": "deffered_lighting_pipeline",
    "shaders": [
        { "name": "gbuffer_shader", "path": "…/Gbuffer.glsl",
          "type": "static_mesh", "textures": ["diffuse"],
          "defines": ["STATIC_MESH"] },
        { "name": "gbuffer_skin_shader",  "path": "…/Gbuffer.glsl",
          "type": "skinned_mesh", "defines": ["SKINNED_MESH"] },
        { "name": "pointlight_shader",    "path": "…/PointLight.glsl" },
        { "name": "dirlight_csm_shader",  "path": "…/SunLight.glsl",
          "defines": ["CSM"] }
        /* … 15 shaders total … */
    ],
    "textures": [
        { "name": "gbuffer_depth",   "format": "depth24", "width": 1280, "height": 720 },
        { "name": "gbuffer_normal",  "format": "rgba16",  "width": 1280, "height": 720 },
        { "name": "gbuffer_diffuse", "format": "rgba8",   "width": 1280, "height": 720 },
        { "name": "gbuffer_light",   "format": "rgba8",   "width": 1280, "height": 720 }
    ],
    "passes": [
        { "name": "pass_gbuffer", "index": 0,
          "input": [], "output": ["gbuffer_depth","gbuffer_normal","gbuffer_diffuse"],
          "shaders": ["gbuffer_shader","gbuffer_skin_shader", …],
          "blendMode": "replace", "drawMode": "opaque" },
        { "name": "pass_light",   "index": 1,
          "input": ["gbuffer_depth","gbuffer_normal"], "output": ["gbuffer_light"],
          "blendMode": "add", "clearMode": "color",
          "clearColor": [0.01,0.01,0.01,1], "drawMode": "light" },
        { "name": "pass_final",   "index": 2,
          "input": ["gbuffer_light","gbuffer_diffuse"], "output": [],
          "shaders": ["lambert_shader"],
          "blendMode": "replace", "drawMode": "quad" }
    ]
}
```

`Pipeline` is the abstract base; `PrelightPipeline` is the concrete
light-pre-pass implementation. The driving entrypoint:

```cpp
pipeline->Execute(octree);  // executes all passes in `index` order
```

`Pass` wraps the FBO + render state for one stage:

```cpp
class Pass {
    vector<shared_ptr<Texture>> m_OutputTextures;  // FBO color/depth attachments
    vector<shared_ptr<Texture>> m_InputTextures;   // sampler bindings
    unsigned int m_FrameBuffer;
    ClearMode m_ClearMode;
    BlendMode m_BlendMode;
    CullMode  m_CullMode;
    CompareMode m_CompareMode;
    DrawMode  m_DrawMode;          // opaque / transparent / light / quad
    void CreateFrameBuffer();
    void BindRenderTargets();
    void Bind(bool clear);
};
```

`DrawMode` selects what `Pipeline::Execute` does inside a pass:

- `opaque` / `transparent` — query the octree's `RenderQuery` for that bucket
  and submit each `RenderUnit` with the appropriate shader (sub-meshed by
  material).
- `light` — render light volumes (point = sphere, dir = full-screen quad,
  spot = cone) accumulating into `gbuffer_light`.
- `quad` — full-screen quad with the named shader (final composite).

`PipelineSwitch` is a small enum of runtime toggles
(`CASCADED_SHADOW_MAP`, `MESH_BOUNDS`, …) flipped via `SetSwitch`.

### 9.2 Shaders and materials

`Shader` owns the GL program object. `LoadAndCompile(path, useGeomShader)`:

- Reads a GLSL text file containing all stages glued together (the file
  includes its own `#version` line; defines from the JSON config get
  prepended).
- Splits into `GL_VERTEX_SHADER`, `GL_FRAGMENT_SHADER`, optional
  `GL_GEOMETRY_SHADER` (used for cubemap shadow projection).
- Compile / link / validate; on failure logs the GL info log.
- Binds vertex attribute locations by name pre-link
  (`glBindAttribLocation`).

Uniforms are looked up by name at bind time. There is **no UBO / SSBO usage**
— every uniform is a `glUniform*` call. `Shader` exposes typed
`BindFloat / Int / UInt / Matrix` helpers, plus higher-level
`BindCamera(camNode)` / `BindLight(lightNode)` that assemble the standard
uniform set (view, projection, light position/direction/color/intensity/range/
attenuation/falloff/shadow-map-sampler).

`Material` holds:

- A `TextureMap` (named slots, e.g. `DIFFUSE_TEXTURE`).
- A `UniformMap` of `Uniform<T, Size>` instances (templated wrappers over
  `glUniform*` calls).
- A list of `Shader` variants — the engine picks the right one based on the
  `Pass.DrawMode` and the mesh's `static_mesh` / `skinned_mesh` type.

`Uniform` is its own small templated class hierarchy
(`Uniform1f`, `Uniform2f`, …, `UniformMatrix4fv`).

### 9.3 Mesh GPU side

`Mesh` packs a fixed schema of typed `ArrayBuffer<T>` fields:

```cpp
ArrayBufferf Positions;   // float, 3 per vertex
ArrayBufferf Normals;     // float, 3 per vertex
ArrayBufferf Tangents;    // float, 3 per vertex
ArrayBufferf UVs;         // float, 2 per vertex
ArrayBufferf Weights;     // float, 3 per vertex (4th = 1 - sum)
ArrayBufferui IDs;        // uint,  4 per vertex (bone indices)
ArrayBufferui Indices;
vector<SubMesh::Ptr> m_SubMeshes;
unsigned int m_VAO;
```

Each `ArrayBuffer<T>` keeps a CPU-side `vector<T>` mirror plus a GL handle, a
target (`GL_ARRAY_BUFFER` / `GL_ELEMENT_ARRAY_BUFFER`), a usage hint, and a
dirty flag. Upload happens on first bind / when dirty. SubMeshes are separate
index ranges per material — a mesh with 3 materials has 3 SubMeshes sharing
the same vertex stream but different IBOs.

`MeshRender` is the component that pairs a `Mesh` with one or more `Material`s
(per submesh) and is what the octree visibility query collects.

### 9.4 Render utilities

- `RenderUtil` — debug draw (lines / wireframe, debug meshes, blit). Uses
  `glPolygonMode(GL_LINE)` for wireframe — the only desktop-GL-only call in
  the engine; needs a substitute on GLES/ANGLE.
- `RenderQuery` — buckets of `RenderUnit { node, mesh, material, subMeshIdx }`
  for opaque / transparent / shadow-caster / light, plus a `Sort(camPos)`
  for transparent back-to-front.
- `BufferManager` — central GL-handle book-keeper used to create / dispose
  buffers and textures owned by transient objects.
- `EnumUtil` — the **GL translation table**. Every engine enum
  (`TextureFormat`, `BlendMode`, `CullMode`, `CompareMode`, `ClearMode`,
  `DrawMode`, …) maps to GL constants here. ~150 GL enums referenced.
  This is the single most concentrated piece of GL surface area.

### 9.5 Built-in pipeline: light pre-pass deferred

`PrelightPipeline` is the only concrete `Pipeline` shipped:

1. **G-buffer** — render scene with the static/skinned `gbuffer_shader`s,
   writing depth + view-space normal + diffuse albedo.
2. **Light accumulation** — for each visible light:
   - render light volume (sphere/cone/quad);
   - read depth + normal, evaluate BRDF (lambert), output to `gbuffer_light`
     with `BlendMode::add`;
   - if `m_CastShadows`, sample the relevant shadow map (2D for spot,
     cubemap for point, CSM cascades for directional).
3. **Final** — full-screen quad combining `gbuffer_light * gbuffer_diffuse`.

Shadows: standard depth-only pass into a 2D / cubemap / CSM array, then
sampled in step 2. No PCF beyond the default texture filter.

> **Future investigation — shared shadow atlas (URP-style):** each casting
> light currently gets its own full-size temporary map (1024² 2D/cube,
> CSM array), so N shadowed lights = N separate targets. Unity's URP
> instead packs all additional-light shadow maps into ONE atlas (spot =
> 1 tile, point = 6 cube-face tiles, main directional gets its own
> cascaded map), downscaling per-light resolution with a warning when
> the atlas overfills. Worth evaluating if shadow-pass target count or
> memory ever becomes the bottleneck.

### 9.6 OpenGL surface area

Aggregate count from the survey: **~200–250 distinct GL entry points**
referenced across the renderer, clustered as:

| Cluster                | Approx count | Where                  |
|------------------------|--------------|------------------------|
| Shader pipeline        | ~30          | `Shader.cpp`           |
| Framebuffer / RT       | ~15          | `Pass.cpp`             |
| Texture                | ~20          | `Texture.cpp`          |
| Buffers (VBO/VAO/IBO)  | ~15          | `ArrayBuffers.cpp`     |
| Draw + state           | ~40          | `Pipeline.cpp`, `Pass.cpp`, `Mesh.cpp` |
| Queries (occlusion)    | ~8           | `RenderQuery.cpp`      |
| Misc (viewport, scissor, blend, depth, cull) | ~20 | scattered |

Functional dependencies that map cleanly to GLES 3.x (and therefore ANGLE):

- FBOs, VAOs, MRT, occlusion queries, texture arrays, cubemaps,
  `GL_DEPTH24_STENCIL8`, sRGB textures, basic blend / depth / cull state.

Things that need attention when we retarget to ANGLE GLES 3.x:

- **`GLLoader.{h,cpp}`** — the engine's hand-rolled function-pointer loader
  (1700 lines). Goes away. ANGLE ships standard `GLES3/gl3.h` + `EGL/egl.h`
  headers and exports the entry points directly via `libGLESv2` /
  `libEGL`; we just `#include` them and link. `GLLoader::LoadGLFunctions`
  is replaced by EGL display + context creation.
- **Geometry shaders** — used for cubemap shadow projection. Not in core
  GLES 3.0 / 3.1; ANGLE Vulkan / Metal supports them via
  `EXT_geometry_shader` / `OES_geometry_shader`. Acceptable for now (we
  enable the extension on supported backends); revisit if we ever target
  WebGL2.
- **`glPolygonMode(GL_LINE)`** in `RenderUtil` — does not exist in GLES.
  Substitute with explicit line-list rendering (we already have a debug
  line VAO).
- **`GL_DEPTH32F_STENCIL8`** — supported via ANGLE on every backend we
  care about; assert before using on truly mobile GLES if we ever ship
  there.
- **ImGui's GL backend** — currently the desktop-GL3 backend. Swap to
  ImGui's GLES3 backend (`imgui_impl_opengl3.cpp` works against GLES3 too
  with `IMGUI_IMPL_OPENGL_ES3` defined).
- **SFML's own GL context** — SFML 2.x creates an `NSOpenGLContext` /
  `wglCreateContext` / `glXCreateContext` for `sf::Window`. We bypass it
  by using `sf::WindowBase` (no GL context) and feeding
  `sf::WindowBase::getSystemHandle()` (HWND / NSWindow / X11 Window) into
  `eglCreateWindowSurface`. On macOS this means swapping the window's
  content view for a `CAMetalLayer`-backed view when ANGLE's Metal
  backend is selected — the one platform-specific bit of work.

---

## 10. Cross-cutting infrastructure

### 10.1 `Signal<Args…>`

A typed publish/subscribe primitive used everywhere:
`Engine::OnUpdate`, `InputUtil::OnKeyDown`, `SceneNode::OnTransformChange`,
…

```cpp
auto sig = Signal<float>::Create();
size_t key = sig->Connect(receiverShared, &Receiver::OnTick);   // member fn
sig->Connect(&someFreeFunction);                                // free fn
sig->Emit(0.016f);
sig->Disconnect(key);
```

- Implemented as `unordered_map<size_t, Link>`, where `Link` holds a
  `weak_ptr<void>` to the receiver and a `std::function<void(Args…)>`
  built from a member-function pointer + raw `this`.
- On `Emit`, expired weak_ptrs are pruned and the rest are invoked while
  holding the link mutex.
- **Lambdas are intentionally rejected** — the design hinges on
  weak-pointing the receiver to clean up dead subscriptions automatically,
  which only works for member functions where the captured `this` is itself
  a `shared_ptr`. A lambda's captures cannot be safely weak-tracked under
  this model.
- Two mutexes (link map / key generator) prevent connect-during-emit races,
  but the callback runs *under* the link mutex, so a callback that
  disconnects itself or emits the same signal would deadlock.

### 10.2 `ThreadUtil`

A `Singleton<ThreadUtil, size_t>` worker pool (progschj / ThreadPool variant):

```cpp
size_t Enqueue(function<void(int&)> task,
               function<void()> callback,
               function<void(int)> progressChanged);

template<class R>
size_t Enqueue(function<shared_ptr<R>(int&)>,
               function<void(shared_ptr<R>)>,
               function<void(int)>);

void Update();   // call on main thread; flushes finished callbacks
```

Tasks receive a mutable `int&` they can write progress to; the
`progressChanged` hook fires on every progress change (still on the worker
thread); the `callback` runs on the main thread inside `ThreadUtil::Update()`.
`SetMainThread()` / `IsMainThread()` are global helpers used by other
subsystems (the logger inserts thread-id markers on non-main calls).

### 10.3 `Singleton<T, Args…>`

Templated singleton wrapper. `Initialize(args)` + `Instance()`. Asserts on
double-init / use-before-init. **No automatic cleanup at process exit** —
manual destruction is required; the comments call out that `Log` must
outlive everything else.

Real singletons in the engine:

`Log<0>`, `ThreadUtil`, `InputUtil`, `RenderUtil`, `BufferManager`, `Gui`,
`FbxParser`, `MeshUtil`, plus indirectly `Scene::Active`.

### 10.4 `Log<int>`

Templated on a non-type integer so multiple parallel log streams can coexist
(`Log<0>`, `Log<1>`…). Levels: `EROR / WARN / INFO / DBUG`. Two formatters
(`Simple`, `Default`). Optional file output, append or truncate. Thread-safe
via a stream mutex. Convenience macros `FURYE / FURYW / FURYI / FURYD`.

`Log::Record` is a per-call temporary that buffers a `stringstream` via
`operator<<` and flushes on destruction (`operator+=` flushes back to the
log) — same idea as plog. `__FUNCTION__` is captured at construction with
namespace cruft stripped.

### 10.5 `InputUtil`

`Singleton<InputUtil, int, int>` (window width, height). State arrays for
all SFML mouse buttons / keyboard keys, plus signals:

```
OnKeyDown / OnKeyUp     <sf::Keyboard::Key>
OnWindowClosed          <>
OnWindowResized         <int, int>
OnWindowFocus           <bool>
OnTextEntered           <size_t>     (unicode codepoint)
OnMouseEnter            <bool>
OnMouseWheel            <float, int, int>
OnMouseMove             <int, int>
OnMouseDown / OnMouseUp <sf::Mouse::Button, int, int>
```

The `sf::Keyboard::Key` / `sf::Mouse::Button` types in the public API are
the most concrete way SFML has leaked into engine semantics.

### 10.6 `Macros.h`

`FURY_API` (Windows DLL export, no-op elsewhere), `ASSERT_MSG` (printf
+ `abort`), and the constant `FURY_MIPMAP_LEVEL = 5`.

### 10.7 `Gui`

Thin SFML+GL3 ImGui backend: `Initialize(window, scale)`, `HandleEvent`,
`NewFrame(dt)`, `Render`. Imports the ImGui source from `ThirdParty/ImGui`.

---

## 11. What's serialized, what isn't

Useful to know before designing tests and the new asset path:

| Type             | JSON in/out      | Notes                                                 |
|------------------|------------------|-------------------------------------------------------|
| `SceneNode`      | yes              | hierarchy, components, local TRS                      |
| `Transform`      | yes              | pre/post TRS                                          |
| `Mesh` (static)  | yes              | positions, normals, tangents, UVs, indices, AABB, submeshes |
| `Mesh` (skinned) | yes              | joint tree (flat array w/ parent indices) + bone_ids + bone_weights round-trip; closed via the cli-gltf-convert change |
| `Material`       | yes              | uniforms + texture refs                               |
| `Texture`        | by URI only      | image bytes are reloaded from disk via stb            |
| `AnimationClip`  | yes              | channels / keyframes                                  |
| `Joint`          | yes (tree)       | offset matrix included                                |
| `Light`, `Camera`| yes              |                                                       |
| Pipeline JSON    | load only        | `Pipeline::Save` is not really used                   |

---

## 12. Inventory of dated patterns (informational)

Not refactor decisions — just so the next document can refer to them:

- `void*`-as-rapidjson in `Serializable`, `void*`-as-payload in `ThreadUtil`,
  `shared_ptr<void>` storage in `EntityManager`. Loose typing; unsafe casts.
- `Singleton<T>` everywhere makes mocking and parallel test isolation
  harder.
- SFML enums in `InputUtil`'s public signatures.
- Skin / animation data does round-trip now (closed 2026-06-22); listed
  here for historical reference — pre-add-cli-gltf-convert it was a
  silent gap.
- `Transform` mixes interpolation with the transform component (animation
  state and spatial state share a class).
- Scene-nodes are referenced by **both** the parent hierarchy and the
  octree, with no ownership convention to disambiguate; manual
  `RemoveFromOcTree` is required.
- `Signal` deliberately rejects lambdas; modern callback patterns are
  not available.
- No reflection / no schema; the component-by-name registry is hand-maintained.
- 24 fps animation tick assumption baked into `AnimationClip`.
- 4-bone-per-vertex hard cap baked into `Mesh::IDs / Weights`.

---

## 13. Sibling repos — quick orientation

### 13.1 `furyengine/angle` — Google ANGLE (the RHI)

GN-built C++ project that exposes a standard **GLES 2.0 – 3.2 + EGL 1.5** API
(`include/GLES2/`, `include/GLES3/`, `include/EGL/`) and translates calls
underneath to:

- `src/libANGLE/renderer/vulkan/` — Vulkan (Linux / Windows / Android)
- `src/libANGLE/renderer/metal/`  — Metal (macOS / iOS)
- `src/libANGLE/renderer/d3d/`    — D3D 9 / 11 (Windows)
- `src/libANGLE/renderer/gl/`     — desktop GL pass-through (debug)
- `src/libANGLE/renderer/wgpu/`   — WebGPU

Backend selection happens at EGL display creation time
(`EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE`, `…_METAL_ANGLE`, etc.). Output is
`libGLESv2` + `libEGL`, static or shared.

**Decision: ANGLE *is* the RHI.** We do not introduce an extra
`RHI::Device / Buffer / Texture / CommandList` interface layer. The existing
renderer call sites (`Shader.cpp`, `Pass.cpp`, `Texture.cpp`,
`ArrayBuffers.cpp`, `RenderUtil.cpp`) keep calling GLES; ANGLE handles the
native translation. If we ever want to drop ANGLE and target Vulkan/Metal
natively, the same call sites are the rewrite target — but that's a problem
for future-us, and YAGNI today.

#### 13.1.1 Why ANGLE integration is non-trivial — findings (2026-06-18)

A first reconnaissance pass for the ANGLE retargeting (proposal scoped, then
held — see §15) surfaced constraints that materially shape how this lands.
Recording them here so the next attempt starts from the same baseline:

- **No CMake.** ANGLE's only build system is **GN + Ninja** (`BUILD.gn`,
  `.gn`, `DEPS`). There is no `CMakeLists.txt`, no `pkg-config`, no Meson.
  Consuming ANGLE via `add_subdirectory()` the way we consume SFML and
  tinygltf is **not possible**. The only build paths are: (a) drive Ninja
  from CMake via `add_custom_command` after a one-time `gclient sync` /
  `gn gen`, then `find_library` the resulting `libGLESv2`+`libEGL`; or
  (b) skip building and consume prebuilt binaries from elsewhere
  (Chrome's app bundle, third-party redistributables).
- **No release tags.** The local clone (`furyengine/angle` @ `9464aca6`) is
  Chromium-style rolling: zero `git tag -l` output, no semver. Pin choices
  are commit SHAs, and "latest stable" is whatever the current Chrome
  branch ships. There is no analogue to `tinygltf v2.9.7` or `SFML 3.1.0`
  for stability.
- **DEPS is large.** A real `gclient sync` against ANGLE's `DEPS` pulls
  ~10GB of Chromium-aligned deps (depot_tools, build/, third_party/...). The
  source-only checkout we have is 322MB; a buildable checkout is
  multi-gigabyte. This shapes CI cost and dev-machine setup.
- **No vendor-prebuilt redistributable.** Google ships ANGLE binaries
  inside Chrome / Edge installations as `libGLESv2.dylib` + `libEGL.dylib`,
  but **not** as a standalone download with a stable ABI. Any "use Chrome's
  ANGLE" path is a proof-of-concept, not production.
- **What other engines do.** Skia and Filament embed ANGLE by checking out
  the same GN tree and building it as part of their own GN build. Bevy's
  wgpu route avoids ANGLE entirely. None of them go through CMake
  `add_subdirectory`. So there is no off-the-shelf "vendor ANGLE in a
  CMake project" pattern to crib from — we have to invent it.
- **macOS surface attachment is platform-specific.** The plan to feed
  `sf::WindowBase::getNativeHandle()` into `eglCreateWindowSurface` is
  straightforward on Linux (X11 Window) and Windows (HWND). On macOS,
  ANGLE's Metal backend requires the window's content view to be a
  `CAMetalLayer`-backed `NSView`, not the default `NSOpenGLContext`-backed
  view that SFML installs. This is **Objective-C++ glue we don't have
  today** — a discrete sub-task of the retargeting, not a passing detail.
- **Engine-side surface area is large.** §9.6 already enumerates ~200–250
  GL entry points across the renderer. The retargeting includes: retiring
  `GLLoader.{h,cpp}` (~2,971 lines), replacing
  `glPolygonMode(GL_LINE)` (not in GLES) with explicit line-list rendering,
  swapping ImGui's GL3 backend for its GLES3 backend, and gating geometry
  shaders (used for cubemap shadow projection — not core in GLES 3.0/3.1,
  enabled via `EXT_geometry_shader` / `OES_geometry_shader` on supported
  ANGLE backends). Each of those is small in isolation; the aggregate is
  the work.
- **Implication for scoping.** The "drop in ANGLE" change cannot be a
  single small PR and is materially larger than the FBX → tinygltf swap.
  When we resume, the natural decomposition is several sequential
  changes: (1) submodule + GN-driven build wiring + headers-only compile,
  (2) EGL surface creation from `sf::WindowBase` + macOS `CAMetalLayer`
  view, (3) `glPolygonMode` and other GLES-incompatible call-site fixes
  + ImGui GLES3 backend swap, (4) geometry-shader fallback or extension
  gating. The ordering means the demo doesn't run end-to-end on ANGLE
  until step 2 lands, which is fine — each step is independently
  reviewable.

### 13.2 `furyengine/sfml` — SFML 2.x (kept, context bypassed)

Kept for windowing, input, OS event loop, clipboard, monitor enumeration,
and HiDPI handling. We continue to use:

- `sf::WindowBase` (not `sf::Window`) so SFML doesn't create a GL context
  of its own.
- `sf::Event` polled via `pollEvent` and forwarded into `Engine::HandleEvent`
  → `InputUtil` (unchanged).
- `sf::Keyboard::Key` / `sf::Mouse::Button` enums in `InputUtil`'s public
  signatures (unchanged — they leak SFML, but that's tolerable).

What changes:

- `Engine::Initialize` no longer expects an SFML-created GL context. It
  takes the native window handle from `sf::WindowBase::getSystemHandle()`
  and hands it to ANGLE's EGL.
- `Gui` (ImGui) loses its `imgui-sfml` GL3 backend wiring; we use ImGui's
  generic GLES3 backend and feed it SFML events ourselves.

### 13.3 `furyengine/tinygltf` — single-header glTF 2.0 loader

C++11 single-header loader. Covers everything the old `FbxParser` did and
everything `GLTFDom` is missing:

- `.gltf` (JSON) and `.glb` (binary) containers
- skins / inverse-bind matrices / joint hierarchies
- animations with explicit interpolation modes (`LINEAR`, `STEP`,
  `CUBICSPLINE`)
- PBR metallic-roughness materials (we'll need a new shader variant for
  these; the engine's `Material` is currently Phong/Lambert-shaped)
- sparse accessors, byte-stride buffer views
- embedded image data, external image URIs, KHR extensions

Public API at the level we'll touch:

```cpp
tinygltf::TinyGLTF loader;
tinygltf::Model    model;
std::string err, warn;
loader.LoadASCIIFromFile(&model, &err, &warn, path);     // .gltf
loader.LoadBinaryFromFile(&model, &err, &warn, path);    // .glb
// model.scenes / .nodes / .meshes / .skins / .animations / .materials …
```

**Replaces both `FbxParser` and `GLTFDom`.** The new `GltfImporter` will
walk `tinygltf::Model` and emit engine `SceneNode` / `Mesh` / `Material` /
`Joint` / `AnimationClip` / `Texture` instances — same target types as
today, just a different source.

### 13.4 `furyengine/sol2` — Lua/C++ bindings (deferred)

Header-only Lua binding library. Not wired in yet. The plan is:

1. Stabilise the engine API after the GLES/ANGLE retargeting and the
   tinygltf importer land.
2. Pick the binding surface — likely `Scene`, `SceneNode`, `Component`
   factories, `Pipeline`, `InputUtil` signals, the math types.
3. Wire sol2 so demos like `examples/Demo.cpp` can be authored as `.lua`
   scripts and reloaded without recompiling the engine.

We avoid binding too early because every signature change during the
refactor would propagate through bindings.

### 13.5 `furyengine/gltf` — Khronos spec (reference only)

Pure documentation: `specification/2.0/`, `extensions/`, `LICENSES/`,
`README.md`. **No C/C++ loader code.** Kept for spec lookups; the actual
parser is `tinygltf` (§13.3). Once the importer is stable we may remove
this clone entirely.

---

## 14. Suggested test surface (math first)

Modules that are pure value types with no GL / SFML / FBX dependency, and
therefore the natural first target for the pytest+pybind11 unit-test layer:

- `Vector4`, `Quaternion`, `Matrix4`, `Plane`, `MathUtil`
- `BoxBounds`, `SphereBounds`, `Frustum`
- `Transform` (with a stub `SceneNode`)
- `Color`
- `Signal` (basic connect/disconnect/emit semantics)
- `EntityManager` (against trivially-typed test entities)

Modules that are testable but need light scaffolding:

- `OcTree` / `OcTreeNode` (needs SceneNode stubs)
- `AnimationClip` / `Animator` (needs Joint stubs; pure CPU)
- `Serializable` round-trip on the math primitives (needs rapidjson in the
  test build)

Modules that need a real GPU context (defer until after the RHI refactor):

- `Shader`, `Texture`, `Buffer`, `ArrayBuffers`, `Mesh` (GPU upload paths),
  `Pass`, `Pipeline`, `PrelightPipeline`, `RenderUtil`, `BufferManager`,
  `Gui`.

---

## 15. Refactor decisions to date

What we've explicitly committed to (so the next round of work has a fixed
reference point):

- **Windowing & input → keep SFML**, but bypass its GL context. Use
  `sf::WindowBase` and feed `getNativeHandle()` into ANGLE's EGL.
  (Note: `getSystemHandle` was renamed to `getNativeHandle` in SFML 3.)
- **RHI → ANGLE GLES 3.x, no extra abstraction layer.** The renderer
  files (`Shader.cpp`, `Pass.cpp`, `Texture.cpp`, `ArrayBuffers.cpp`,
  `RenderUtil.cpp`) keep calling GLES; we treat ANGLE as the abstraction
  itself. `GLLoader` is retired in favour of ANGLE's standard headers.
- **Asset format → glTF 2.0 via tinygltf.** `FbxParser` is removed;
  `GLTFDom` is also removed (tinygltf supersedes it). A new
  `GltfImporter` walks `tinygltf::Model` into the existing engine types.
- **Scripting → sol2 / Lua, deferred.** Bind after the C++ API stabilises.
- **Tests → pytest + pybind11**, scoped to the GPU-free modules listed in
  §14 for the first round.

What's landed so far:

- **FBX SDK removed; `GLTFDom` removed; `tinygltf v2.9.7` vendored** as
  `engine/ThirdParty/tinygltf` submodule and compiled into `libfury` (with
  `TINYGLTF_NO_STB_IMAGE` to avoid stb_image ODR collision). No engine
  code calls `tinygltf` yet; that's the importer change. Commit
  `cc79a90`.
- **SFML 2.x → SFML 3.1.0 migration done** ahead of schedule. Originally
  the plan was "keep SFML 2.x, just bypass its GL context"; that path
  required SFML 2.x to be installed on the build host, and the local
  `furyengine/sfml` clone was already at 3.1.0+. Ported all 89 SFML 2 →
  SFML 3 call sites across `Engine.{h,cpp}`, `Gui.cpp`,
  `InputUtil.{h,cpp}`, `Demo.cpp` (variant `sf::Event`, scoped enums,
  `sf::State`/`sf::Style` split, `pollEvent` returning
  `std::optional<sf::Event>`, `sf::Int32` → `std::int32_t`,
  `LostFocus`/`GainedFocus` → `FocusLost`/`FocusGained`, etc.). Bumped
  to **C++17** + CMake 3.22 (SFML 3 requirements). Commit `cc79a90`.
- **rapidjson v1.1.0 vendored** as `engine/ThirdParty/rapidjson`
  submodule (header-only). System-installed paths
  (`RAPIDJSON_INCLUDE`/`SFML_INCLUDE`/`SFML_LIB`) no longer accepted.
  Commit `cc79a90`.
- **Pre-existing bug fixes that became necessary**: 5 rvalue-address
  bugs in `Shader.cpp:372-373` and `Pipeline.cpp:430,509,598,672`
  (taking `&Matrix4_returned_by_value.Raw[0]` is ill-formed under C++17
  AppleClang); CMake `-NDEBUG` typo → `-DNDEBUG`. Commit `cc79a90`.
- **sol2 + Lua 5.4 scripting landed.** Vendored `Lua v5.4.7` and
  `sol2 v3.5.0` as submodules under `engine/ThirdParty/{lua,sol2}`. The
  fixed-timestep main loop moved out of `Demo.cpp` and into
  `Engine::Run(window, callbacks)`. The build now produces a single
  static `fury` executable (was: `libfury.dylib` + a separate `demo`
  binary; the shared-lib path stays available behind
  `BUILD_SHARED_LIBS=ON`). `examples/Demo.cpp` is gone, replaced by
  `examples/Demo.lua` plus a thin C++ launcher at `examples/main.cpp`.
  Lua scripts register callbacks via `Engine.run({on_init=...,
  on_update=..., on_fixed_update=..., on_shutdown=...})`. The bound
  surface covers what `Demo.lua` exercises end-to-end: `Vector4`,
  `Quaternion`, `MathUtil`, `OcTree`, `Scene`, `SceneNode`, `Camera`,
  `Transform`, `Component`, `Pipeline`, `PrelightPipeline`,
  `FileUtil`, `LogLevel`, `RenderUtil`, `Gui`, plus `Engine.run`. See
  `docs/LUA.md`. Pre-existing bug fix: `SceneManager` gained a
  `virtual ~SceneManager() = default;` (sol2's templated destructor
  instantiation requires it; the pre-change code would have UB-deleted
  any heap-allocated `SceneManager` through a base pointer).

- **CLI surface on the `fury` binary + asset import (2026-06-22).** The
  `fury` binary now hosts both the Lua launcher (runtime path) and an
  offline asset CLI. `examples/main.cpp` dispatches on `argv[1]`: if it's
  a known subcommand (`convert`, `info`, `help`, `version`), `fury::Cli::Run`
  takes the offline path with no SFML window, no `Engine::Initialize`,
  and no Lua VM. Otherwise behaviour is unchanged (the Lua launcher
  loads `argv[1]` or defaults to `Demo.lua`). New subcommands: `fury
  convert gltf <in> <out.json|.bin>` and `fury convert fbx <in>
  <out.gltf|.glb|.json|.bin>`; `fury info <path>` for CPU-side counts of
  any supported asset. See `docs/CLI.md` for the full reference. The
  Mesh.cpp:120 `TODO: no joints yet` gap is closed — `Mesh::Save`/`Load`
  now round-trips `bone_ids`, `bone_weights`, the `joints` array (flat
  with parent indices), and `root_joint`. Static-mesh scene files remain
  byte-identical to pre-change output (the new keys are emitted only
  when present).
- **glTF importer + FBX subprocess (2026-06-22).** A new
  `engine/Fury/GltfImporter.{h,cpp}` translates `tinygltf::Model` into
  engine `Scene` / `SceneNode` / `Mesh` / `Material` / `Joint` /
  `AnimationClip`. It's CPU-only — no GL touch — so the same class is
  invoked by both the offline CLI (`fury convert gltf`) and the runtime
  Lua binding (`Importer.LoadGltf`). The originally-planned "runtime
  GltfImporter" is what landed; tinygltf is consumed at both offline
  and runtime entry points. Material mapping is lossy:
  PBR metallic-roughness → Lambert (`baseColorFactor` → `diffuse_color`,
  `baseColorTexture` → `diffuse_texture`, `emissiveFactor` →
  `emissive_color`, alpha mode → `opaque`; `metallicFactor`,
  `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture`,
  `occlusionTexture` are read but discarded with one warning per source
  material). Animation samples are resampled at 24 fps from glTF's
  seconds-based time. Rejections: morph targets, sparse accessors,
  non-default `byteStride`, non-triangle primitives, non-empty
  `extensionsRequired`. FBX support is restored via a subprocess to the
  vendored `engine/ThirdParty/FBX2glTF/FBX2glTF-{darwin,linux,windows}-x64`
  binaries (`engine/Fury/FbxConverter.{h,cpp}` is the wrapper). The
  engine never links the FBX SDK; FBX → glTF happens out of process,
  then chains through the same glTF importer.
- **glTF lights (KHR_lights_punctual) imported (2026-06-23).** The
  `GltfImporter` now reads `tinygltf::Model::lights` and attaches engine
  `Light` components to any `SceneNode` whose source glTF node references
  one. Mapping: point → `LightType::POINT` (range → radius), spot →
  `LightType::SPOT` (range → radius, inner/outer cone angles passed
  through in radians), directional → `LightType::DIRECTIONAL`. `color`
  copies 1:1 into engine `Color`. `intensity` is **unit-less pass-through
  in v1** — glTF's intensity is in candela / lumen / lux depending on
  light type per KHR_lights_punctual, and an HDR/PBR pipeline that
  consumes those units is a deferred follow-up. Each attached light is
  logged once at info level. Without this, deferred-Lambert renders any
  imported scene black since there's nothing illuminating the geometry;
  FBX2glTF emits `KHR_lights_punctual` for FBX sources by default, so
  the chain works end-to-end.
- **Runtime scene editor in `Demo.lua` (2026-06-22).** New Lua bindings:
  `Importer.LoadGltf / LoadFbx / LoadScene / MergeInto`,
  `FileUtil.ListDirectory / SaveFile / SaveCompressedFile`,
  `Scene:Clear()`, `Gui.InputText`. `Demo.lua` uses them to expose a
  `File` menu (New Scene / Open Scene / Import / Save Scene As) so an
  operator can drag any of the assets in `Resource/Scene/` into a live
  viewport. See `docs/LUA.md` for the full pattern. The camera node
  lives outside the scene's root tree so `Scene:Clear` doesn't drop it.

**Decision: keep `scene.json` / `scene.bin` as the runtime form (2026-06-22).**
The architecture question "should we drop the custom Serializable format now
that we have glTF?" was investigated and resolved in favour of keeping it.
The engine's runtime format encodes precomputed AABBs (the octree depends
on them), engine-shaped material uniforms matching the Lambert pipeline,
submesh-per-material splits, `cast_shadows` flags, and LZ4 compression
(1.6 MB `.bin` vs 11 MB `.json` vs raw glTF). Replacing the runtime loader
would be larger work than building a converter AND would lose that
precomputed data. The importer is the bridge: glTF / FBX in, engine runtime
form out, invokable offline (CLI) or at runtime (Lua binding).

**HDR/PBR pipeline + PBR material variant — deferred.** The engine ships a
Lambert deferred pipeline as of v1; HDR is a deliberately later step
(user-confirmed). Until the HDR pipeline lands, the importer's lossy
PBR → Lambert mapping is the right shape. When HDR arrives, a follow-up
change can add a PBR material variant alongside Lambert without
re-architecting the importer.

**Held: ANGLE integration (2026-06-18).** Reconnaissance pass scoped a
proposal but the work was held. Reasons enumerated in §13.1.1: ANGLE
has no CMake, no release tags, no vendor-prebuilt redistributable; the
GN/Ninja + `gclient sync` build path is materially different from
anything else we've vendored; macOS Metal needs `CAMetalLayer` view
glue we don't have today; and the engine-side surface area
(`GLLoader.{h,cpp}` retirement, `glPolygonMode` replacement, ImGui
GLES3 backend swap, geometry-shader gating) is large enough that one
PR is not the right shape. When we resume, plan to decompose into ~4
sequential changes (build wiring → window surface + Metal view →
GLES-incompatible call sites + ImGui swap → geom-shader fallback).

What we have **not** decided yet (open questions for the next discussion):

- Whether `Mesh` extends past 4 bone influences per vertex (glTF allows
  more than one `JOINTS_n` / `WEIGHTS_n` accessor set).
- Whether `Material` grows a PBR variant alongside the existing
  Phong/Lambert one, or whether we replace Phong/Lambert outright.
- Whether `AnimationClip` switches from frame-tick to seconds-based
  timing (glTF stores time as floats in seconds; the current
  ticks-per-second model is FBX-shaped).
- Whether the `Serializable` `void*`/rapidjson layer stays; if it stays,
  whether skinned-mesh round-tripping (currently a `TODO`) is in scope
  for this refactor.
- Whether `Signal` gets a lambda-friendly variant, or whether we leave
  it alone and live with member-function-only callbacks.

## 16. SFML 3 HiDPI on macOS

SFML 3.1's macOS backend exposes a `highDpi` flag on the internal
`SFOpenGLView`, but the public `sf::Window` construction path never sets
it. See `engine/ThirdParty/SFML/src/SFML/Window/macOS/SFOpenGLView.mm:128`:

```
// Currently, isHighDpi is always expected to be NO, and so the OpenGL view will render scaled.
```

Practical consequences:

- `window.getSize()` returns the window size in **screen points**, not
  backing pixels.
- The OpenGL framebuffer is allocated at point resolution, so on a Retina
  display the system upscales every blit to the backing surface. UI text
  and the deferred GBuffer look slightly soft.
- `glViewport(0, 0, w, h)` matches `getSize()` 1:1 — we are *not* in a
  point-vs-pixel mismatch, we're just rendering at half the native
  resolution of the panel.

The `fix-demo-fps-profiler-retina` change defaults `gui_scale` and
`gui_font_scale` to `1.0` to avoid the previously-baked SFML-2-era 2×
compensation, which would otherwise double-magnify the UI. Users on
Retina displays who want a larger UI can opt in via the Lua options table
(`{ gui_scale = 1.25, gui_font_scale = 1.25 }`); they will still see soft
upscaling, but at least the UI won't be triple-magnified.

Real HiDPI support is deferred. The path forward involves:

1. Patching `SFOpenGLView.mm:101-128` to take `highDpi = YES` (or
   vendoring a custom SFML patch).
2. Switching `Gui.cpp:230` and `Pass.cpp:652`'s `glViewport` calls to
   query the backing framebuffer size rather than `getSize()`.
3. Sizing GBuffer textures and shadow maps to backing pixels, not points.
4. Auditing every `InputUtil::m_WindowSize` consumer for point-vs-pixel
   assumptions.

That is a separate change; this one only fixes the symptom (double
scaling) and documents the underlying limitation.

## 17. Windows DPI awareness

The Windows build declares per-monitor V2 DPI awareness via an embedded manifest at `engine/Resources/fury.exe.manifest` (`<dpiAwareness>PerMonitorV2</dpiAwareness>`). Without it the editor process is "Per Monitor DPI Aware" (v1) at best — SFML 3.1's Win32 backend only sets the legacy SHCore flag, and the stock MSVC manifest has no `<dpiAwareness>`. On a 200% display the OS bitmap-stretches a 1× logical framebuffer to 2× backing pixels and the UI looks half-size. V2 (not V1) re-issues `WM_DPICHANGED` on monitor changes and lets the OS scale non-client chrome. macOS / Linux don't link the manifest; the existing paths are unchanged.

The system DPI is read once at `Engine::Initialize` via `Engine::GetSystemDPI()` (Windows: `GetDpiForSystem()/96.0f`; macOS: `[NSScreen mainScreen].backingScaleFactor` via `engine/Fury/Engine_dpi_mac.mm`, returns 1.0 today because SFML 3 macOS forces `highDpi=NO`; Linux: hard-coded 1.0). The PLATFORM_* macros are in `engine/Fury/Macros.h`.

The DPI is applied via two new `EngineOptions` fields:
- `gui_scale = 0.0f` (the default) is a sentinel meaning "use system DPI". Pass an explicit value (e.g. 1.0) to bypass.
- `gui_font_scale = 0.0f` (the default) follows the resolved `gui_scale` so the font density tracks the widget layout on HiDPI displays.
- `dpi_aware_override` (default `false`) when true multiplies the caller's `gui_scale` by the system DPI.

The resolved values are logged once at init. DPI is read once at init and never re-read — dragging the window between monitors of different DPI takes effect on the next launch. `Engine::HandleEvent` also bounds-checks `KeyPressed`/`KeyReleased` codes against `[0, sf::Keyboard::KeyCount)` to prevent an OOB write when SFML's Win32 backend returns `Key::Unknown` for IME virtual keys. The editor's `HandleShortcuts` checks `InputUtil::IsIMEComposing()` (always false today; stub for future IME wiring) before routing shortcuts.

---

## Appendix A — File-by-file responsibility

```
Engine.{h,cpp}            static init/shutdown/update + SFML event glue
Signal.h                  weak_ptr-aware typed pub/sub (no lambdas)
Singleton.h               templated singleton wrapper
Macros.h                  FURY_API, ASSERT_MSG, FURY_MIPMAP_LEVEL
Log.h                     templated log instance + Record builder

MathUtil.{h,cpp}          rotation conversions, constants, helpers
Vector4.{h,cpp}           the only vector type (2/3/4-D)
Matrix4.{h,cpp}           column-major 4×4, append/prepend ops
Quaternion.{h,cpp}        unit quaternion + slerp
Plane.{h,cpp}             4-coefficient plane
Color.{h,cpp}             RGBA float color

Transform.{h,cpp}         component, owns local TRS + interpolation
BoxBounds.{h,cpp}         AABB
SphereBounds.{h,cpp}      bounding sphere
Frustum.{h,cpp}           6 planes, 8 corners, view+proj
Collidable.h              IsInside / IsInsideFast interface
TypeComparable.h          GetTypeIndex CRTP

SceneNode.{h,cpp}         hierarchy + component map + world transform + AABBs
Component.{h,cpp}         base class for SceneNode-attached behaviors
Entity.{h,cpp}            base for serializable hashed objects
EntityManager.h           per-type hashed registry
Scene.{h,cpp}             root + EntityManager + working dir
SceneManager.h            base for scene managers (octree extends this)

OcTree.{h,cpp}             spatial index, visibility queries
OcTreeNode.{h,cpp}         8-children + AABB + leaf SceneNode list

Camera.{h,cpp}            view, projection, frustum
Light.{h,cpp}             type, color, intensity, attenuation, light-volume mesh
MeshRender.{h,cpp}        component pairing Mesh + Materials
Joint.{h,cpp}             bone in a skinned skeleton; ibm + scene-node ref
Mesh.{h,cpp}              vertex streams + skin + submeshes
MeshUtil.{h,cpp}           primitives + normal/tangent/optimize utilities
Material.{h,cpp}          textures + uniforms + shader variants
Uniform.{h,cpp}           templated GLSL uniform setter

Shader.{h,cpp}            compile/link/bind GLSL programs
Texture.{h,cpp}            GPU texture + stb_image loading + temp pool
Buffer.{h,cpp}             base GL buffer
BufferManager.{h,cpp}      central buffer/texture handle bookkeeper
ArrayBuffers.{h,cpp}       templated typed VBO / IBO with CPU mirror
Pass.{h,cpp}               FBO + render state for one stage
Pipeline.{h,cpp}           ordered list of passes (abstract)
PrelightPipeline.{h,cpp}   concrete light-pre-pass deferred renderer
RenderQuery.{h,cpp}        opaque/transparent/shadow/light render bucket
RenderUtil.{h,cpp}         debug draw + blit + helper utilities
EnumUtil.{h,cpp}           engine enum → GL constant tables
GLLoader.{h,cpp}           generated GL function-pointer loader

FbxParser.{h,cpp}          FBX SDK importer (gated by _FURY_FBXPARSER_IMP_)
GLTFDom.{h,cpp}            partial glTF 2.0 JSON DOM (no scene-graph build)
FileUtil.{h,cpp}            path resolution, stb image, rapidjson, LZ4, glTF buffers
Serializable.{h,cpp}        Load/Save virtual interface + primitive helpers

AnimationClip.{h,cpp}       channels + keyframes, 24 fps default
AnimationPlayer.{h,cpp}     `Animator` component, advances time, drives Transform TRS pairs (joints via their linked SceneNodes)
AnimationUtil.{h,cpp}       optimization helpers

InputUtil.{h,cpp}           SFML keyboard/mouse mirror + signals
Gui.{h,cpp}                 ImGui SFML+GL backend wrapper
ThreadUtil.{h,cpp}          worker pool with progress + main-thread callbacks

Fury.h                     umbrella include
```
