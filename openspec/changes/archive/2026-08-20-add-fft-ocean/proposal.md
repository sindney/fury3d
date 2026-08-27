## Why

The engine renders sky (with time-of-day) and heightmap terrain, but has no water: outdoor scenes stop at the shoreline. We want Unity HDRP-quality ocean (FFT spectrum waves, crest foam, infinite horizon) adapted to our GL 3.3 pipeline, plus CPU-side buoyancy so Jolt rigid bodies visibly float and can be tuned in play mode.

## What Changes

- **FFT ocean bake tool (Python)**: offline generator producing tileable, seamlessly looping ocean animation data from a Phillips/Tessendorf-style spectrum - multi-band (swell + ripple) displacement maps with choppy (horizontal-pinch) displacement for Gerstner-style sharp crests, derived normal maps, and Jacobian-based foam/whitecap maps. Same authoring pattern as the existing terrain/sky generator scripts. This is the always-available baseline path.
- **Compute shader support (opportunistic GL 4.3+)**: the engine baseline stays GL 3.3, but context creation negotiates the highest available core profile and a capability probe reports whether compute shaders + image load/store are usable. When they are, the same looped wave arrays are generated on the GPU at load time (spectrum + butterfly IFFT compute passes) instead of read from disk - a "GPU bake at load" that also makes spectrum params live-editable. When they are not (any macOS, older drivers, headless), the engine uses the baked-asset fallback automatically, and a user-facing setting (plus env override) can force compute off even when supported.
- **Ocean surface component**: two geometry modes - (a) traditional finite grid mesh (inland lakes, water areas inside terrain) and (b) "infinite" camera-centered ocean built from concentric static-density grid rings (GL 3.3 has no tessellation, so ring LOD is baked into the meshes; rings follow the camera snapped to grid cells to avoid vertex swimming). Vertex shader replays baked displacement bands; fragment shader adds detail normals, crest/shore foam, and integrates with the existing HDR + SSR + postfx chain and sky TOD lighting.
- **CPU wave sampler**: loads the same baked wave data and evaluates height (and normal) queries on the CPU - HDRP's "Option B: CPU emulation", perfectly frame-synced with rendering, no GPU readback latency. Public query API for gameplay and editor gizmos.
- **Buoyancy component**: per-body float sample points; applies buoyant force, water drag, and righting torque to Jolt bodies during play mode. Works with the existing physics-world/play-mode specs.
- **New example project** `examples/Projects/ocean` with three demo scenes:
  1. Infinite ocean + sky TOD, with pure-color plastic balls/cubes and floatable props as buoyancy debug objects (mirrors `outdoor/outdoor_terrain.bin` setup conventions).
  2. Terrain/landscape with an inland water area (finite grid mode).
  3. Island terrain surrounded by infinite ocean (shore foam at the waterline).
- **Editor support**: ocean inspector (spectrum/wind/fetch/choppiness/foam/LOD radii), asset picker wiring for baked wave data, debug views (wireframe ring LOD, foam mask, displacement magnitude), buoyancy float-point gizmos.

## Capabilities

### New Capabilities

- `compute-shaders`: GL context version negotiation, compute capability detection, compute stage in the shader compiler, dispatch/image-binding helpers, and the global user on/off switch (persisted setting + env override).
- `ocean-wave-data`: offline FFT bake tool, baked wave asset format (looping multi-band displacement/normal/foam + metadata), GPU-at-load generation of the same arrays when compute is effective, runtime loading, and the CPU sampling contract that rendering and buoyancy both consume.
- `ocean-rendering`: ocean surface geometry modes (finite grid, infinite camera-centered rings), wave displacement playback, foam and detail shading, and integration with the HDR/SSR/postfx pipeline and sky lighting.
- `ocean-buoyancy`: CPU water-height queries driving Jolt rigid-body forces (float points, drag, righting), play-mode behavior, and debug visualization.

### Modified Capabilities

<!-- No existing spec requirements change. Ocean plugs into physics-world, play-mode, sky-atmosphere, and screen-space-effects without altering their contracts. -->

## Impact

- **Engine** (`engine/Fury/`): context version negotiation in app bootstrap, GLLoader compute entry points, `Shader` compute stage + dispatch API, `Texture::BindImage`, capability/settings plumbing; new `OceanComponent`, `OceanWaves` asset + loader, CPU `WaveSampler`, `BuoyancyComponent`, new ocean surface + FFT compute shaders, render-pass hook (`pass_ocean`) in the pipeline, editor inspector + debug view additions.
- **Tools**: new Python bake script next to the existing terrain/sky generators (numpy + image output; FFT via numpy.fft).
- **Examples**: new `examples/Projects/ocean/` project (three `.bin` scenes, generated wave textures, floatable prop assets).
- **Docs/specs**: three new capability specs; no breaking changes to existing APIs.
- **Performance notes**: GPU cost is 2-3 displacement texture samples per vertex + foam/normal samples per fragment (no compute shaders, GL 3.3-safe); CPU cost is a handful of bilinear texture reads per floating body per frame.
