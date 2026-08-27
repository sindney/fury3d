# ocean-wave-data

## Purpose

Looping, tileable FFT ocean wave data: the offline Python bake tool (Phillips spectrum, loop-quantized, per-band displacement/normal/foam payloads + JSON sidecar), the loadable shared `OceanWaves` asset, the optional GL 4.3+ GPU generation-at-load path producing the same arrays, and the CPU `WaveSampler` contract (bilinear + frame lerp, choppy-corrected queries) shared by rendering and physics.

## Requirements

### Requirement: A Python bake tool SHALL generate looping, tileable FFT ocean wave data

The tool SHALL evaluate a Phillips/Tessendorf-style directional spectrum
(wind speed, wind direction, fetch, choppiness, damping) with `numpy.fft` and
export, per frequency band: a displacement map sequence (RGB = choppy
horizontal dx/dz + vertical dy), a normal map sequence, and a foam/whitecap
map sequence derived from the Jacobian of the displacement field. Each band
SHALL take a tile size in meters, grid resolution, frame count, and loop
duration, and SHALL loop seamlessly (last frame wraps to first) and tile
seamlessly at UV boundaries. A JSON metadata file SHALL accompany each bake
recording all spectrum parameters, per-band tile size, amplitude range, frame
count, and loop period so the runtime can replay exactly what was baked.

#### Scenario: Bake produces seamless looping bands

- **WHEN** the tool bakes a 2-band ocean with 64 frames per band
- **THEN** each band exports displacement, normal, and foam sequences plus a metadata JSON
- **AND** sampling across the UV wrap edge and across the frame-63-to-frame-0 boundary shows no discontinuity above a stated epsilon

#### Scenario: Choppy displacement sharpens crests

- **WHEN** a bake uses choppiness > 0
- **THEN** the displacement maps contain nonzero horizontal (dx/dz) components
- **AND** the foam maps contain nonzero values at wave folds (negative Jacobian)

#### Scenario: Bake validates and reports loop/tile error metrics

- **WHEN** the bake finishes
- **THEN** the tool prints max edge-wrap and loop-wrap error per band and fails loudly if they exceed the epsilon

### Requirement: Baked wave data SHALL be a loadable engine asset

The engine SHALL load baked bands (textures + metadata JSON) from a
project-relative path following the same working-directory conventions as
other binary-referenced textures, into GPU textures (displacement/normal/foam
per band) and a CPU-side copy of per-band height and horizontal-displacement
frames. Loading SHALL be shared: two ocean components referencing the same
asset path reuse one loaded instance.

#### Scenario: Load and share a wave asset

- **WHEN** two `OceanComponent`s reference the same baked wave path and the scene loads
- **THEN** both sample identical data and the asset is loaded once

#### Scenario: Missing or corrupt bake fails gracefully

- **WHEN** the metadata JSON or a referenced texture is missing
- **THEN** the ocean renders as a flat plane at its water level, buoyancy queries return that flat level, and a single warning is logged

### Requirement: Wave arrays SHALL be generatable on the GPU at load when compute is effective

The engine SHALL generate the looped per-band arrays (displacement + normal
+ foam, frames as texture-array layers) on the GPU from the component's
serialized spectrum parameters at attach time whenever
`HasEffectiveCompute()` is true and the ocean's wave source is `Auto`,
using compute passes (spectrum evaluation, time evolution with hermitian
field packing, butterfly IFFT with ping-pong RG32F temporaries, normal and
Jacobian-foam passes) instead of reading a baked asset. After each generation the engine SHALL
perform a one-time synchronous readback into the CPU float copy, so the CPU
sampler, buoyancy, surface shading, and determinism contracts are identical
for generated and baked data. Editing a spectrum parameter SHALL trigger
regeneration (a one-time cost, never per-frame).

#### Scenario: Generated arrays meet the same contract as baked ones

- **WHEN** wave arrays are GPU-generated with any parameter set
- **THEN** loop-wrap and tile-wrap continuity pass the same epsilon checks as baked data
- **AND** CPU sampler queries match the generated GPU arrays within the stated tolerance

#### Scenario: Live sea-state edit

- **WHEN** compute is effective and the user edits wind speed in the inspector
- **THEN** the arrays regenerate once and rendering and buoyancy both reflect the new sea state without a scene reload

#### Scenario: Fallback when compute is ineffective

- **WHEN** compute is unavailable or disabled and the wave source is `Auto`
- **THEN** the component loads its baked wave asset path (flat fallback + warning if missing) and no compute pass runs

### Requirement: The runtime SHALL sample wave height on the CPU in sync with GPU replay

A CPU sampler SHALL evaluate wave height at any world (x, z, t) by bilinear
sampling of the baked height frames per band with linear interpolation
between looped frames, using the same data the GPU vertex shader replays, so
physics and rendering never diverge by more than sampler error. The sampler
SHALL additionally offer surface normal and horizontal choppy-displacement
queries, and a choppy-corrected height query that iterates the lookup a fixed
small number of times to account for horizontal vertex pinch near steep
crests.

#### Scenario: CPU height matches GPU displacement

- **WHEN** a height query is issued at a set of world positions and times spanning a full loop
- **THEN** the CPU result matches the GPU-displaced vertex height at those positions within the stated sampler tolerance

#### Scenario: Choppy-corrected query converges near crests

- **WHEN** a choppy-corrected query is issued at a steep crest with high choppiness
- **THEN** the returned height differs from the uncorrected query and the iteration terminates within the fixed iteration count

### Requirement: Wave replay SHALL be a pure function of position and time

Neither GPU playback nor CPU sampling SHALL accumulate per-frame simulation
state: displacement at (x, z, t) is computed solely from the baked loop and
the global time uniform, so seeking, pausing, or re-entering play mode
reproduces identical water.

#### Scenario: Deterministic replay across play-mode re-entry

- **WHEN** play mode is entered twice with the same start time
- **THEN** CPU height queries return identical values on both runs
