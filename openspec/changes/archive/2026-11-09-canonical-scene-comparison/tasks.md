# canonical-scene-comparison

## 1. Deterministic scene serialization

- [x] 1.1 Sort the top-level `textures`, `materials`, `meshes`, `animations`, `particleSystems`, and `heightmaps` arrays by asset path then name in `Scene::Serialize`
- [x] 1.2 Sort a material's `textures` and `uniforms` slot arrays by key in `Material::Serialize`
- [x] 1.3 Sort a render chain entry's `uniforms` array by uniform name in `RenderSettings::Serialize`
- [x] 1.4 Drop the `material_id` uniform from the saving branch of `Material::Serialize`, leaving the loading branch unchanged
- [x] 1.5 Verify that saving a scene, reloading it, and saving it again produces byte-identical documents for the feature, island, and outdoor scenes

## 2. SceneCompare module

- [x] 2.1 Replace `engine/Fury/SceneFingerprint.{h,cpp}` with `engine/Fury/SceneCompare.{h,cpp}`; rename `FpNode` to `CanonNode` and `SceneFingerprintCounts` to `SceneCounts`
- [x] 2.2 Keep the canonical tree, its text form, its parser, and the field-path differ; delete `SceneFingerprint::Build` and every per-type `Fingerprint*` builder
- [x] 2.3 Add a rapidjson-to-`CanonNode` adapter that maps objects, arrays, strings, integers, reals, booleans, and null
- [x] 2.4 Add `SceneCompare::DiffJson` and `SceneCompare::DiffFiles`, where `DiffFiles` reads a `.json` document or the JSON payload of a `.bin` document
- [x] 2.5 Keep `SceneCompare::Counts` as the live-scene structural summary
- [x] 2.6 Update `engine/CMakeLists.txt` for the renamed source file

## 3. Lua API

- [x] 3.1 Remove `Scene.GetPersistentFingerprint`, `Scene.DiffPersistentFingerprints`, and `Scene.GetFingerprintCounts`
- [x] 3.2 Add `Scene.DiffPersistentState(jsonA, jsonB)` returning the differing fields with path and both values
- [x] 3.3 Add `FileUtil.CompareSceneFiles(pathA, pathB)` returning the same shape
- [x] 3.4 Add `Scene.GetStateCounts` returning the structural counts

## 4. Tests

- [x] 4.1 Update `tests/lua/scene_archive_roundtrip.lua` to compare saved documents instead of fingerprints
- [x] 4.2 Update `tests/lua/scene_component_registry.lua` to compare saved documents instead of fingerprints
- [x] 4.3 Update `tests/lua/ui_saved_scene_verify.lua` to compare saved documents instead of fingerprints
- [x] 4.4 Run the headless lane of `tests/run.py` and confirm the round-trip, component-registry, and saved-scene-verify suites pass
- [x] 4.5 Confirm the comparison reports a field path when a saved document is perturbed

## 5. Specs

- [x] 5.1 Update `openspec/specs/scene-round-trip/spec.md` with the deterministic-content, no-runtime-identifier, and document-comparison requirements
- [x] 5.2 Update `openspec/specs/lua-test-scripts/spec.md` to describe document comparison
- [x] 5.3 Update `openspec/specs/editor-ui-automation/spec.md` to compare the saved document
