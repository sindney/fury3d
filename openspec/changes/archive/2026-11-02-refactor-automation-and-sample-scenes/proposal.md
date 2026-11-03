## Why

The automation suite, example scenes, and scene persistence code have evolved independently: coverage is split across one-off Lua and shell harnesses, the examples do not provide a small canonical set of feature-complete fixtures, and duplicated `Load`/`Save` implementations can drift without a reliable round-trip safety net. A unified serialization path and two reusable showcase scenes will make editor, rendering, UI, and persistence regressions reproducible from a smaller set of maintained tests.

## What Changes

- **BREAKING (internal C++ API)**: Replace paired per-type scene `Load`/`Save` serialization logic with Unreal-style `Serialize(Archive&)` functions and `<<` field operators. Archives expose `IsLoading()`, `IsSaving()`, and `IsCooking()` for the exceptional paths that genuinely need direction-specific behavior.
- Preserve the existing `.json` and compressed `.bin` scene formats, version gate, path identity rules, and `FileUtil` transport entry points while routing both directions through the shared serialization implementation. Existing external-format importers are not replaced.
- Expand headless scene round-trip tests from structural counts to semantic fingerprints covering hierarchy, transforms, components, resources, animation, lighting, particles, terrain/ocean data, and render settings.
- Integrate a pinned Dear ImGui Test Engine dependency into an opt-in editor automation configuration. Add stable automation IDs and windowed, injected-input tests for hierarchy selection, property editing, mesh preview, and profiler windows; keep file-format round trips and other non-UI checks in the headless CLI.
- Add a canonical feature-showcase scene containing tanks, point/spot/directional-CSM lighting, outdoor fire and smoke particles, fox and james skeletal meshes, and an RmlUi overlay. Exercise it with HDR disabled and enabled.
- Recreate the island fixture with an island terrain, kraut vegetation, surrounding infinite ocean, and the existing IslandGame menu/player flow. Keep `examples/IslandGame.lua` as its entry point.
- Add deterministic, manifest-driven render scenarios that produce screenshots and machine-readable results. Use semantic checks plus per-backend, tolerance-based image comparisons rather than cross-GPU pixel equality.
- Reorganize examples around the two canonical scenes and the source assets already grouped under `examples/Projects/samples`. Consolidate redundant test/sample harnesses, update all references, and remove only fixtures whose unique coverage has been migrated or verified elsewhere.

## Capabilities

### New Capabilities

- `archive-serialization`: shared direction-aware archives and `Serialize`/`<<` contracts for scene-persisted types, including cooking branches, error handling, versioning, and format compatibility.
- `editor-ui-automation`: Dear ImGui Test Engine integration, stable editor test references, command-line execution, JUnit output, and the editor UI coverage matrix.
- `rendering-automation`: deterministic rendering scenarios, HDR/backend matrices, screenshot and semantic verification, result artifacts, and consolidation of existing render harnesses.
- `showcase-scenes`: the canonical feature-showcase and island scenes, their reusable assets and entry points, and the examples-organization contract.

### Modified Capabilities

- `scene-round-trip`: round-trip verification uses the shared serialization path and checks semantic state in addition to structural counts, including both canonical showcase scenes.
- `game-ui-demo`: IslandGame boots the recreated canonical island scene and its UI assets are organized for reuse by the feature-showcase overlay, without regressing menu, HUD, options, or player behavior.

## Impact

- Scene, node, component, material, mesh, animation, terrain, ocean, particle, and render-settings serialization under `engine/Fury/`, plus cook/package callers that construct archives.
- `FileUtil`, editor scene commands, Lua bindings, and CLI conversion/execution paths that currently reach separate load/save implementations.
- Editor build configuration, Dear ImGui integration, editor windows and inspectors, and new editor automation tests and result exporters.
- `tests/lua/`, rendering shell wrappers, new test manifests/runners, screenshot baselines, and test documentation.
- `examples/IslandGame.lua`, a feature-showcase entry point, canonical projects under `examples/Projects/`, shared RmlUi documents/styles, and source assets under `examples/Projects/samples`.
- Existing scene files remain readable; test script and example paths that are renamed or removed require coordinated updates to wrappers, documentation, and sample launchers.
