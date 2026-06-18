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
  `Transform`, `MeshRender`, `Camera`, `Light`, `AnimationPlayer`.
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

Joints are nodes in a parallel tree (not the scene-node tree). Each joint
stores:

- `m_LocalMatrix` — TRS in parent space.
- `m_CombinedMatrix` — `m_LocalMatrix * parent->m_CombinedMatrix`.
- `m_OffsetMatrix` — inverse bind, model→bone space (sourced from FBX cluster
  `transformLinkMatrix.Inverse() * transformMatrix * geomMatrix`).
- `m_FinalMatrix = m_CombinedMatrix * m_OffsetMatrix` — what the vertex shader
  consumes.

Each joint also keeps `(old, new)` TRS pairs for animation blending; `Update(dt)`
linearly interpolates positions / scales and slerps rotation between them.
Tree links use first-child / sibling pointers, so traversal is `for (j = root;
j; j = j->next)` style — cache-friendly, slightly idiosyncratic.

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

### 7.3 Playback (`AnimationPlayer`)

A `Component`. Holds the active clip, current time, blend speed, and the
bound skeleton root. Each `Update(dt)`:

1. Advance `m_Time`, wrap or clamp by `m_Loop`.
2. Compute current tick = `time * ticksPerSecond * speed`.
3. For each channel, find the bracketing keyframes, interpolate, write into
   the corresponding `Joint`'s `(old, new)` TRS pair.
4. Walk the joint tree: each `Joint::Update(dt)` blends old→new and recomputes
   `Combined` and `Final` matrices.

The shader reads `Final` matrices as a uniform array (skin matrices). The
Mesh's `IDs` and `Weights` buffers (4 indices and 3 explicit + 1 implicit
weight per vertex) feed the standard linear-blend skinning shader.

---

## 8. Asset loading

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
  submeshes / aabb / cast-shadows. **It does not write joint or weight
  data** — there is a literal `TODO` in the file. Skinned meshes do not
  round-trip through the JSON format yet.
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
`examples/bin/Resource/Pipeline/DefferedLightingLambert.json`:

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
| `Mesh` (skinned) | **partial**      | joint / weight data is `TODO` — does not round-trip   |
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
- Skin / animation data does not round-trip through the JSON format
  (silent gap).
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
- `AnimationClip` / `AnimationPlayer` (needs Joint stubs; pure CPU)
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
  `sf::WindowBase` and feed `getSystemHandle()` into ANGLE's EGL.
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
Joint.{h,cpp}             bone in a skinned skeleton, TRS + matrices
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
AnimationPlayer.{h,cpp}     component, advances time, drives Joint TRS pairs
AnimationUtil.{h,cpp}       optimization helpers

InputUtil.{h,cpp}           SFML keyboard/mouse mirror + signals
Gui.{h,cpp}                 ImGui SFML+GL backend wrapper
ThreadUtil.{h,cpp}          worker pool with progress + main-thread callbacks

Fury.h                     umbrella include
```
