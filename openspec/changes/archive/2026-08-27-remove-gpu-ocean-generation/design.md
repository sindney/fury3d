# Design: remove-gpu-ocean-generation

## WaveSource collapses to baked-only

`OceanComponent::WaveSource` was a 2-value enum (Auto = "GPU if possible,
else baked"; Baked = "always baked"). The GPU branch exists only in
`OceanComponent::TryGenerateWavesGpu` and is the only consumer of GL 4.3+
compute shaders in the engine. Removing it leaves `Auto` with no meaning
beyond "behave like Baked" - the enum is reduced to a single value.

The cleanest cut: delete `WaveSource`, change the serialized field name
on disk to a plain `waveSource` int (kept for forward compatibility -
1 == baked; older `.bin` files with `waveSource: 0` deserialize as 1),
and the inspector's combo becomes a read-only "Baked asset" line. The
one remaining knob is `m_WaveAssetPath` - keep that.

## GL entry points

The `engine/Fury/GLLoader.{h,cpp}` table loads every GL entry point
needed across the engine. The GPU generator path needed
`glCompileShader`/`glGetShaderiv` (already in the table - kept for
the regular shader pipeline) and... no compute-specific entry points of
its own (the `glBindImageTexture` family was the one in question, and
the only consumer in this codebase is `OceanFftGenerator.cpp` via
`Texture::BindImageForCompute`). So no entry-point deletions are
required at the GLLoader level - the lone caller of `BindImageForCompute`
goes away with the rest of the GPU path.

## ComputeSmoke

`Cli::ComputeSmoke` is a 60-line self-test for the compute pipeline.
The test is useful on real-GL machines as a generic compute sanity
check, but the only production code in the engine that needs compute
will be gone, so the test has nothing to validate. Delete it.

## Cascade list

Files to delete:
- `engine/Fury/OceanFftGenerator.h`
- `engine/Fury/OceanFftGenerator.cpp`
- `examples/Resource/Shader/Ocean/FFTOceanCS.glsl`
- `tests/lua/ocean_gpu_gen.lua`

Symbols to remove from headers/sources:
- `engine/Fury/Texture.h` `BindImageForCompute` declaration
- `engine/Fury/Texture.cpp` `BindImageForCompute` implementation
- `engine/Fury/OceanComponent.h` `WaveSource` enum + `TryGenerateWavesGpu`
  + `OceanSpectrumParams` forward decl + `SetWaveSource` setter
- `engine/Fury/OceanComponent.cpp` `TryGenerateWavesGpu` impl +
  `EnsureWaves` simplification
- `engine/Fury/Cli.h` `ComputeSmoke` decl
- `engine/Fury/Cli.cpp` `ComputeSmoke` impl
- `examples/main.cpp` `compute-smoke` subcommand dispatch
- `engine/Fury/LuaBindings.cpp` `SetWaveSource` binding + `OceanSpectrumParams`
  bind + `TryGenerateWavesGpu` Lua surface (none today, but check)
- Demo scripts: `setup_ocean_*.lua` - drop the now-redundant `SetWaveSource(1)` lines
- Docs: `docs/OCEAN.md` - drop the GPU generation paragraph
- Specs: `openspec/specs/compute-shaders/spec.md` - archive

## WaveSource on disk

`OceanComponent::Save` currently writes `waveSource` as a `unsigned int`.
Old `.bin` files with `waveSource: 0` will still load (the field is
read; we just don't branch on it). After the cut, the field is write-
only at save time (always 1) and the load can be a no-op or just read
silently for backward compat. Old demo assets that have `waveSource: 0`
will load and behave identically to `waveSource: 1` (which is what
they were already doing on macOS, which is the only platform we test).
