# add-render-thread

## Why

fury3d renders entirely on the main thread: logic, physics, culling, GL submission, and present run in sequence every frame, so the GPU idles while the CPU walks the scene and vice versa. Vegetation-heavy scenes (ocean_island's trees + grass) are submission-bound on the CPU. Moving GL work to a dedicated render thread (Unity-style split) overlaps game update with draw submission and measurably improves frame time, without the full complexity of a UE-style parallel object model.

## What Changes

- **Two-thread frame pipeline** (Unity `RenderingThreadingMode.RenderThread` style): the game thread runs logic/physics/animation/particle sim and builds a per-frame render snapshot; a new render thread owns the GL context and executes culling, draw submission, and present. The render thread runs at most one frame behind the game thread, bounded by double-buffered frame data.
- **Per-frame render snapshot**: instead of UE's UObject/FScene object split, keep one object model and copy the minimal render-relevant data (transforms, materials, mesh/light/camera refs, instance lists) into a plain frame packet. Game-thread objects stay freely mutable; the render thread only ever reads its own packet — no locks on scene objects, no cross-thread object lifetimes.
- **Static mesh draw-command caching** (UE mesh-drawing-pipeline style): for static renderers, cache ready-to-submit draw commands keyed by (mesh, material, LOD, pass); rebuild only when the renderer is dirtied. Per-frame work drops to cull + sort + submit.
- **Threading toggle**: render thread ON by default; force-single-threaded fallback available via CLI flag / settings for debugging, running the exact same code path synchronously on the main thread.
- **Tracy instrumentation**: render thread named, zones across both sides of the pipeline (game: update/snapshot; render: cull/submit/present), frame-mark semantics updated for render-thread present, counters for queue depth and command-cache hit/rebuild rates.
- **Benchmark support**: unlimited-framerate mode plus a vegetation+particle stress scene (ocean_island base with multiple fire/smoke emitters from outdoor.bin) for threading on/off A/B comparison.
- **ASAN verification pass** over the threaded path.

## Capabilities

### New Capabilities

- `render-thread`: two-thread rendering pipeline — frame sync and packet handoff, render snapshot contents, static draw-command cache, threading toggle, Tracy zones, and benchmark tooling.

### Modified Capabilities

- `tracy-profiling`: frame-mark placement moves to render-thread present; render thread must be named; new required zones for the threaded pipeline stages.

## Impact

- **Engine core**: `Engine::Run` frame loop, GL context ownership transfer to the render thread, renderer pass orchestration (gbuffer, shadow/CSM, SSR/AO, postfx chain, sky, ocean, particles, vegetation instancing).
- **Components**: MeshRenderer/SkinnedMesh/InstancedMeshRender/Light/Camera/ParticleSystem produce snapshot records instead of drawing directly.
- **GL resource lifetime**: texture/mesh/shader uploads must be marshaled to the render thread (deferred upload queue); editor ImGui GL and viewport picking/readbacks marshaled likewise.
- **Editor (furye)**: same threaded path as the player, with readback/picking synchronization points.
- **Tracy**: new zones + thread name; existing macros reused.
- **Assets**: new stress scene(s) under `examples/Projects/`.
- **Build/docs**: ASAN workflow documented; no new third-party dependencies.
