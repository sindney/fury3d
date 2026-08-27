# ocean-rendering (delta for remove-gpu-ocean-generation)

## MODIFIED Requirements

### Requirement: OceanComponent SHALL be a serializable scene component

`OceanComponent` SHALL be registered in `SceneNode::ComponentRegistry` (name
`OceanComponent`) with: geometry mode (finite grid / infinite), wave asset
path, shading parameters (absorption color, scatter color, specular/roughness,
normal detail strength, foam amount, shore-foam depth range), and an SSR
participation flag. It SHALL round-trip through scene save/load exactly
like other registered components. The component resolves the wave asset
through the asset's path (baked) on attach / when the path changes -
the GPU generation path is gone.

#### Scenario: Ocean parameters survive scene round-trip

- **WHEN** a scene with a configured infinite ocean is saved and reloaded
- **THEN** every ocean parameter and the wave asset reference are restored

## REMOVED Requirements

### (REMOVED) Auto waveSource (GPU vs baked)

The `WaveSource::Auto` enum value and the GPU generation branch are gone.
The component always resolves the baked wave asset. The serialized
`waveSource` integer stays in the sidecar for backward-compat (old
`waveSource: 0` Auto values deserialize as Baked and behave identically
to how they did on this dev box, where the GPU path was always
unavailable).

### (REMOVED) Inspector "resolved source" GPU compute option

The inspector's `resolved source` indicator still shows the bake status
(baked, missing, etc.) but no longer has a "GPU compute" entry. The
"compute disabled with the resolved reason" branch and its scenario are
gone; the remaining "compute unavailable" rationale only applies to
disabled-by-user now (no auto-GPU fallback to compare against).

### (REMOVED) Spectrum section disabled where compute is unavailable

The spectrum (GPU generation) section is gone; the inspector's spec
section no longer needs a compute-disabled indicator.
