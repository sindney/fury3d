# canonical-scene-comparison

## Why

The scene fingerprint is a hand-written second implementation of scene serialization: roughly 960 of its 1567 lines exist only to re-walk the scene and re-state, per component type and per field, what the archive already knows how to persist. Every new component type or field has to be added twice, and a component type the fingerprint does not recognize contributes an empty object, so its coverage disappears silently while the round-trip test still passes.

The archive output is already the complete description of persistent state, so the fingerprint can be replaced by a comparison of the canonical scene document itself. That removes the duplicate implementation and makes coverage automatic.

## What Changes

- Make scene serialization deterministic: emit the top-level asset arrays, a material's texture and uniform slots, and a render chain entry's uniform overrides in sorted key order instead of in hash iteration order, so two saves of the same state produce the same document.
- Stop persisting the `material_id` uniform. It carries a process-global runtime counter that the loader regenerates, so the value in the file is dead weight that changes on every load.
- Replace the scene fingerprint with a canonical document comparison: parse a saved scene document into a canonical tree and diff two documents, reporting structured field paths.
- **BREAKING** Remove the `SceneFingerprint` module, the `Scene.GetPersistentFingerprint` and `Scene.DiffPersistentFingerprints` Lua bindings, and the `Scene.GetFingerprintCounts` binding; add `Scene.DiffPersistentState`, `FileUtil.CompareSceneFiles`, and `Scene.GetStateCounts`.
- Keep the structural counts check, which is a live-scene summary and needs no per-type maintenance.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `scene-round-trip`: replace the semantic-fingerprint round-trip requirement with a canonical document comparison, and require deterministic document content and the absence of runtime material identifiers.
- `lua-test-scripts`: describe the round-trip test in terms of canonical document comparison.
- `editor-ui-automation`: compare the saved scene document rather than a scene fingerprint.

## Impact

- **Serialization**: `Scene::Serialize` asset arrays, `Material::Serialize` texture and uniform slots, `RenderSettings::Serialize` chain uniform overrides.
- **Format**: saved scene documents no longer contain the `material_id` uniform, and asset arrays are sorted. Both are load-compatible: the loader still regenerates `material_id`, and array order carries no meaning on load.
- **Engine API**: `engine/Fury/SceneFingerprint.{h,cpp}` is replaced by `engine/Fury/SceneCompare.{h,cpp}`.
- **Lua API**: `Scene.GetPersistentFingerprint`, `Scene.DiffPersistentFingerprints`, and `Scene.GetFingerprintCounts` are replaced by `Scene.DiffPersistentState`, `FileUtil.CompareSceneFiles`, and `Scene.GetStateCounts`.
- **Tests**: `tests/lua/scene_archive_roundtrip.lua`, `tests/lua/scene_component_registry.lua`, and `tests/lua/ui_saved_scene_verify.lua` compare documents instead of fingerprints.
- **Out of scope**: the scene file format version, the archive field-policy model, and the engine's rendering behavior.
