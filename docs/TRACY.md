# Tracy Profiler

fury3d integrates [Tracy](https://github.com/wolfpld/tracy) (vendored at `engine/ThirdParty/tracy`, pinned to v0.14.1) for CPU + GPU frame profiling.

## Enabling

Tracy is **on by default in dev/editor builds** (Debug, RelWithDebInfo, multi-config) and **off by default in shipping builds** (Release, MinSizeRel). Explicit flags always win:

```bash
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Debug      # Tracy ON (default)
cmake -S engine -B build-rel -DCMAKE_BUILD_TYPE=Release # Tracy OFF (shipping default)
cmake -S engine -B build -DFURY_WITH_TRACY=OFF          # force off
cmake -S engine -B build-rel -DCMAKE_BUILD_TYPE=Release -DFURY_WITH_TRACY=ON  # profile a shipping config
```

> Existing build dirs cache the option: run `cmake -B build -U FURY_WITH_TRACY` once (or delete the cache entry) to pick up a changed default.

Options:

| Option | Default | Effect |
|---|---|---|
| `FURY_WITH_TRACY` | ON for Debug/RelWithDebInfo/multi-config, OFF for Release/MinSizeRel | Compiles TracyClient.cpp, defines `TRACY_ENABLE` + `TRACY_ON_DEMAND`, arms all `FURY_*` macros |
| `FURY_WITH_TRACY_GPU` | ON (when Tracy on) | Adds OpenGL timer-query GPU zones (`FURY_GPU_ZONE*`). Optional by design: GL timestamp queries are broken on some drivers (Apple/TBDR), so turn it off per-machine with `-DFURY_WITH_TRACY_GPU=OFF` |

When off, `engine/Fury/Profiler.h` macros (`FURY_ZONE`, `FURY_FRAME`, ...) expand to nothing and no Tracy code is linked.

## Capturing

Enabled builds are **on-demand**: the engine collects nothing until a profiler connects, so you can run normally and attach mid-session.

The Tracy tools build automatically with the engine (standalone `tracy_tools` target) whenever `FURY_WITH_TRACY=ON`, and land next to the executables:

- `examples/tracy-profiler` — the profiler UI (version always matches the vendored submodule)
- `examples/tracy-capture` — headless capture CLI

Then:

1. Run the engine (`./furye Editor.lua` or `./fury Player.lua`).
2. Run `./tracy-profiler` and connect (localhost default), or capture headless (below).

To skip building the tools (e.g. building only the runtime target), build just `fury`/`furye` — the tools only build with the default ALL target.

### Headless capture (agents / no GUI)

```bash
# terminal 1: waits for the app, records, writes the file on disconnect
./tracy-capture -o /tmp/fury.tracy -f -s 30

# terminal 2: run any scene
./fury Player.lua Projects/ocean/ocean_island.bin
```

Open `/tmp/fury.tracy` in `tracy-profiler` afterwards, or check zones without a GUI via the csvexport tool (`engine/ThirdParty/tracy/csvexport`).

## Runtime control

Disarming is a flag flip, not a profiler shutdown: zones stay compiled in but record nothing, even if a client connects mid-run.

- `FURY_TRACY=0` env var at startup disarms the profiler for that run.
- Editor: Settings -> Rendering -> "Tracy Profiler" checkbox, persisted as `Tracy=0|1` in imgui.ini.
- Lua: `Engine.run(cb, { tracy = false })` or `Engine.SetTracyEnabled(false)`.

## macOS GPU-zone caveat

Tracy's OpenGL backend warns at compile time: "OpenGL timestamps are unreliable on Apple devices" — Apple Silicon is TBDR and many drivers report zero `GL_TIMESTAMP` precision. CPU zones are unaffected. On macOS, treat GPU-zone timings as unreliable/absent (build with `-DFURY_WITH_TRACY_GPU=OFF` if the context creation warnings are noisy); on Windows GL 4.3+ they work as intended.

## What you get

- **Frame marks**: one per presented frame (after `window.display()`).
- **Threads**: `main`, `worker-N` (ThreadUtil pool); Jolt job threads appear unnamed.
- **CPU zones**: main-loop phases (`FixedUpdate`, `Lua+Render`, `Gui::*`, `Editor::*`), `PhysicsWorld::TickFixed` (with inner `Jolt::Update`), `Animator::*`, `PrelightPipeline::Execute` with a `Culling` zone and one dynamically-named zone per pipeline pass, shadow-map draws, `DrawSky`, `DrawOcean`, `RunPostProcessChain`.
- **GPU zones** (when `FURY_WITH_TRACY_GPU`): one per pipeline pass, aligned with the CPU timeline.

## Notes for instrumentation authors

- Engine code includes only `Fury/Profiler.h` — never `<tracy/...>` directly.
- Use `FURY_ZONE` / `FURY_ZONE_NAMED("Name")` at function scope; `FURY_ZONE_DYNAMIC(str)` for runtime names (it is two statements — never braceless under an `if`).
- GPU zones need the GL context current on the calling thread (today: always the main thread).
- Keep GPU zones per-pass, not per-draw — each zone is a real timer query.
