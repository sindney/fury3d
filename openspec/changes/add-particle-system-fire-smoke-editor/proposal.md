## Why

The outdoor scene (`examples/Projects/outdoor/outdoor_water.bin`) ships a wood pile with a point light but no visual cue for the fire it implies — there's nothing animating above the logs to read as "burning". More generally, fury3d has no CPU-driven particle subsystem: smoke, sparks, dust, magic, weather — any effect the user might want beyond static meshes is unreachable today. Building the engine's first particle system lets us land a Unity-style API on top of existing primitives (SceneNode + Component + Material + Texture + Shader), drop a concrete deliverable into a shipped scene, and give the editor a third asset editor that shares its 3D preview infrastructure with the mesh editor.

## What Changes

### Particle runtime

- Add `engine/Fury/ParticleSystem.{h,cpp}` — a `Component` that owns a CPU-side pool of particle instances and a `Shader`-driven emitter. Mirrors Unity's `ParticleSystem` shape (modules under one struct, single `Burst`-style spawn API, lifetime-driven per-particle state).
- Add `engine/Fury/ParticleRenderer.{h,cpp}` — the visible side: builds a dynamic billboard-quad mesh per emitter, binds the smoke/fire texture, draws with additive/alpha blending against the active pipeline. The renderer is a separate component attached to the same `SceneNode` (mirrors Unity's split between `ParticleSystem` and `ParticleSystemRenderer`).
- Add `engine/Fury/ParticleModules.{h}` — module structs: `EmissionModule` (rate over time, bursts), `ShapeModule` (box / sphere / cone — supports the wood-pile position), `VelocityModule` (initial + inherited from parent node), `ColorOverLifetimeModule`, `SizeOverLifetimeModule`, `RotationOverLifetimeModule`, `RendererModule` (texture, blend mode, sort mode).
- Per-frame update in `Scene::Update` walks every `ParticleSystem` and steps simulation; per-frame draw in the existing render pass collects every `ParticleRenderer` after `MeshRender` (transparent pass ordering — the postprocess-effects/transparent-rendering trap: linear gbuffer depth + transparent objects must be drawn back-to-front and after opaque geometry).
- New `ParticleShader` (additive / alpha-blended billboard) compiled by `Shader::Create`; reuses the existing `Material` texture-binding path so a `Material` can carry a `ParticleDiffuse` slot for the smoke/fire texture.

### Texture authoring

- Two 256×256 RGBA PNGs committed as static assets under `examples/Projects/outdoor/`:
  - `fire.png` — additive fire: warm gradient core (white→yellow→orange→red→transparent), FBM-warped.
  - `smoke.png` — alpha smoke: soft round puff, premultiplied alpha, cool gray.
- Generated once with a throwaway FBM noise script. (Original plan committed the
  generator under `engine/Tools/`; user directive 2026-07-31: texture tooling is
  temporary — don't commit it.)

### Scene edit

- Add a `ParticleEmitter` entity to `examples/Projects/outdoor/outdoor_water.bin` (re-authored `.bin`, saved via `fury` CLI), parented to the existing wood-pile scene node with two stacked emitter shapes: a tall thin fire column + a wider short-lived smoke plume. Both use the generated textures and the existing point light's color.
- Verify visually by running `fury examples/Demo.lua` (or the outdoor-water Lua entry) and screenshotting the lit zone — the verification deliverable lives in `screenshots/outdoor_fire_smoke.png`.

### Editor

- Add `engine/Fury/Editor/EditorParticleWindow.{h,cpp}` — a per-asset editor modeled on `EditorAnimationWindow` (separate ImGui window, popup-id-keyed state).
- Refactor `engine/Fury/Editor/EditorAssetWindows.cpp`'s `RenderMeshPreview` so its orbit camera + ground grid + AABB wireframe are reusable: extract `Editor3DPreview.{h,cpp}` owning the `OrbitState`, `PreviewRT`, grid and AABB wireframe helpers. `RenderMeshPreview` and the new `RenderParticlePreview` both call into it.
- `RenderParticlePreview` spins the assigned particle system in-place (clock driven by a manual preview-time slider so the user can scrub lifetime curves without playing the scene), renders the live `ParticleRenderer` output into the same FBO the mesh editor uses.
- Editor body shows the module structs (emission rate, shape, velocity, color/size/rotation over lifetime curves, texture binding) and live updates on every change. Texture binding uses the existing `EditorAssetPicker` (same row pattern as the material editor's `RenderMaterialTextureRow`).

## Capabilities

### New Capabilities
- `particle-system`: CPU-driven billboard particle system — emitter + renderer components, modules, scene-graph integration, and runtime update/draw path.
- `particle-editor`: per-asset particle editor window in `furye` — module inspector, texture binding, preview rendering, sharing the orbit camera / FBO with the mesh editor.
- `procedural-particle-textures`: ~~Python noise-based fire/smoke texture generator~~ (dropped per user directive 2026-07-31 — tooling is temporary) — reduced to the two texture assets, committed as static files under `examples/Projects/outdoor/`.

### Modified Capabilities
- `asset-editor-windows`: `EditorAssetWindows.cpp`'s preview infrastructure is split out into a shared helper so both the mesh editor and the new particle editor render through one orbit camera + ground grid + AABB wireframe pipeline. Spec-level requirement: a single `Editor3DPreview` helper is the rendering primitive for any future asset editor that needs a 3D viewport.
- `transparent-rendering`: particle emitters are transparent geometry. The existing post-process / transparency chain (`fury/transparency-postfx-traps`) already orders transparents after opaques; the particle system is added to that same transparent queue. No new ordering rules, but the spec gains a new entry under "known transparent kinds" listing particles.

## Impact

- **Code**: `engine/Fury/ParticleSystem.{h,cpp}`, `engine/Fury/ParticleRenderer.{h,cpp}`, `engine/Fury/ParticleModules.h`, `engine/Fury/Editor/EditorParticleWindow.{h,cpp}`, `engine/Fury/Editor/Editor3DPreview.{h,cpp}` (new). `SceneNode::ComponentRegistry` gains `"ParticleSystem"` and `"ParticleRenderer"`. `Scene::Update` and the transparent pass get a new walker. `EditorAssetWindows.{h,cpp}` gets `OpenParticleEditor` / `RenderAllOpenAssetEditors` callbacks and a `RenderMeshPreview` rewrite that delegates orbit/grid/AABB to `Editor3DPreview`.
- **Build**: only new engine sources under `engine/Fury/` and `engine/Fury/Editor/` — added to the existing `FURY` target / `FURY_EDITOR` target in `engine/CMakeLists.txt`. No new third-party deps.
- **Assets**: two PNGs under `examples/Projects/outdoor/` (committed as static assets; generator not committed). The outdoor-water `.bin` is re-saved with two extra `ParticleEmitter` nodes parented to the wood pile.
- **Verification**: visual self-check by launching `fury` against the outdoor-water scene and screenshotting the fire zone. Editor preview self-check by opening the particle editor in `furye`, scrubbing the size-over-lifetime curve, and confirming the preview updates without a frame stall.
- **Out of scope**: GPU compute particles, mesh particles, trail particles, sub-emitters, collision modules, soft-particle fade against depth — these are deliberately deferred. The CPU billboard path covers the fire/smoke deliverable and gives us the API shape to grow into the rest later without breaking changes.