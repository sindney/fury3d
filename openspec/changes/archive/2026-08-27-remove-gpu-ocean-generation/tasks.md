# Tasks

Conventions (carried from ocean-round2-polish): ASCII-only, LF endings,
headless via `./fury exec` from `examples/`, visuals via the `fury` PLAYER
binary with `--screenshot-frame 30`. Suite re-runs green; the GPU path was
only active on GL 4.3+ machines, so on this macOS dev box the visual
output is byte-identical to before.

## 1. Engine cuts

- [x] 1.1 DELETE `engine/Fury/OceanFftGenerator.h` and
  `engine/Fury/OceanFftGenerator.cpp` (GPU generator + its readback).
- [x] 1.2 DELETE `examples/Resource/Shader/Ocean/FFTOceanCS.glsl` (4-stage
  compute shader, 286 lines).
- [x] 1.3 DELETE `Texture::BindImageForCompute` (declaration in
  `Texture.h`, implementation in `Texture.cpp`).
- [x] 1.4 DELETE `Cli::ComputeSmoke` and the `compute-smoke`
  subcommand dispatch in `examples/main.cpp`.

## 2. OceanComponent cascade

- [x] 2.1 Collapse `WaveSource` to a single value (Baked). Remove
  `Auto`, `TryGenerateWavesGpu`, `SetWaveSource`, the Lua
  `OceanComponent:SetWaveSource` binding.
- [x] 2.2 Simplify `EnsureWaves()` to a single `OceanWaves::Resolve(path)`
  call. The `m_ResolvedSource` field collapses to "1 (baked)" and
  `m_ResolvedReason` becomes the asset-load status.
- [x] 2.3 Drop `OceanSpectrumParams` from `OceanComponent.h` (only the GPU
  path used it).

## 3. Inspector + demos

- [x] 3.1 Inspector: `Wave Source` combo becomes a read-only "Baked asset"
  line; "Resolved" + reason stay.
- [x] 3.2 Demo builders: drop the `ocean:SetWaveSource(1)` line in each
  `setup_ocean_*.lua` (the one in `setup_ocean_base.lua` currently
  bakes; the others inherited the default).
- [x] 3.3 Verify the demo scenes still load and animate identically
  (ocean_base, ocean_infinite, ocean_island, ocean_lake, ocean_storm).

## 4. Tests + docs

- [x] 4.1 DELETE `tests/lua/ocean_gpu_gen.lua` (the test skips on
  non-compute platforms; meaningless after the cut).
- [x] 4.2 Suite re-run: `buoyancy_smoke`, `ocean_waves_spec`,
  `ocean_scene_roundtrip`, `ocean_asset_flow`, `physics_smoke` all PASS
  in both compute-enabled and `FURY_COMPUTE_SHADER=0` modes.
- [x] 4.3 `tests/check_engine_ascii.py` clean; LF-only.
- [x] 4.4 `docs/OCEAN.md`: drop the GPU generation paragraph.
- [x] 4.5 `grep -rE "TryGenerateWavesGpu|OceanFftGenerator|FFTOceanCS|ComputeSmoke|BindImageForCompute|compute-smoke" engine/ examples/ tools/ docs/ tests/` returns nothing.

## 5. Specs

- [x] 5.1 Archive `openspec/specs/compute-shaders/spec.md` (its only
  active content was the GPU wave generator).
- [x] 5.2 `openspec archive remove-gpu-ocean-generation --yes`.
