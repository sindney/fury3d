# Proposal: remove-gpu-ocean-generation

## Why

`OceanFftGenerator` is the only consumer of GL 4.3+ compute shaders in the
engine. It runs the same Phillips-spectrum FFT math as the offline Python
baker (`tools/gen_ocean_assets.py`), producing byte-identical output - so
its only practical value is a workflow convenience (edit wind/fetch in the
inspector, regenerate at load).

The real cost is the upkeep burden:

- One-time at attach: ~5-30 s of compute dispatch per ocean component.
- The path is **untestable on macOS** (GL 4.1 ceiling), our primary dev
  platform, so every change is a "we hope it works on Windows/Linux".
- The shipped built-in water systems (Unreal Water System, Unity HDRP's
  Crest) do not offer runtime FFT regeneration either - they all bake
  offline and replay at runtime. The real-time FFT is a plugin tier
  (e.g. Oceanology, GPU Gems Ch. 1), not the default.
- The offline bake is already fast (5-7 s for the 3-band engine + storm
  assets), and the previews (PNG) are inspection-ready.

Removing the path:

- Eliminates the only place in the engine that needs GL 4.3+ compute.
- Cuts ~600 lines of GPU-only code (FFT shader, generator, compute-smoke
  CLI test, the `BindImageForCompute` Texture API, the ComputeShaders
  TogglePath branch in `Engine::Initialize`, the GL function-pointer
  block for the FFT compute entry points).
- Drops the runtime ComputeSmoke self-test - macOS no longer needs to
  decide whether to skip it; everyone falls back to baked.
- Aligns our default with the industry baseline (Unreal Water, Crest HDRP):
  bake offline, replay at runtime.
- The cascade is mechanical: `OceanComponent::EnsureWaves` becomes a
  single `OceanWaves::Resolve` call; the `Auto`/`Baked` waveSource enum
  collapses to just `Baked`; the inspector's "resolved source" loses
  the "GPU compute" option (was already unavailable on mac); the
  `compute-shaders` enable flag and its CLI/setting toggles become
  ocean-irrelevant (still useful for the unrelated ComputeSmoke test if
  we keep it, but we don't).

## What Changes

- DELETE: `engine/Fury/OceanFftGenerator.h`, `engine/Fury/OceanFftGenerator.cpp`,
  `examples/Resource/Shader/Ocean/FFTOceanCS.glsl`.
- DELETE: `Texture::BindImageForCompute` (declaration + implementation) -
  no other callers.
- DELETE: `Cli::ComputeSmoke` and its `compute-smoke` subcommand
  dispatch in `examples/main.cpp` and the GPU-only GL function-pointer
  block in `engine/Fury/GLLoader.cpp` / `GLLoader.h` that was used solely
  for the FFT compute entry points (`glCompileShader` etc. remain for
  the regular shader pipeline).
- COLLAPSE: `OceanComponent::WaveSource::Auto` -> removed; the component
  always resolves the baked asset. `OceanSpectrumParams` deleted.
- SIMPLIFY: `OceanComponent::EnsureWaves()` becomes a single
  `OceanWaves::Resolve(path)` (the "GPU vs baked" branch goes).
- CASCADE: Inspector "resolved source" loses the "GPU compute" option;
  `WaveSource` combo becomes a read-only "Baked asset" line. Demo scene
  builders lose the `ocean:SetWaveSource(1)` line (now redundant).
- DROP: `tests/lua/ocean_gpu_gen.lua` (the test is "skipped on platforms
  without compute" - no longer meaningful).
- SPECS: archive `openspec/specs/compute-shaders/spec.md` (its only
  active content was the FFT wave generator).

## Non-Goals

- Not introducing Gerstner waves in-shader. The bake is sufficient for
  the current visual target; if we want live in-editor tweaks later, that's
  a separate proposal.
- Not removing the `Engine::HasEffectiveCompute` / `GetComputeShadersEnabled`
  surface - some other tool may grow into it. The toggle does become
  ocean-irrelevant; that's fine.
- Not changing the `Engine::Run` API.

## Verification

- Suite re-runs green in both compute modes (the GPU-skip path becomes
  the only path).
- Demo scenes (base / infinite / island / lake / storm) still load and
  animate identically to before.
- ASCII / LF clean.
- `grep -r "TryGenerateWavesGpu\|OceanFftGenerator\|FFTOceanCS\|BindImageForCompute\|compute-smoke" engine/ examples/ tools/ docs/ tests/` returns nothing.
- `--screenshot-series` still works (it was a side tool, unrelated).
