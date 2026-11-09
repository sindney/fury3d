# canonical-scene-comparison

## Context

The engine has two implementations of "what is a scene's persistent state": the `Archive` serializers, which decide what a saved document contains, and `SceneFingerprint`, which re-walks the live scene and re-states the same facts per component type so two scenes can be compared. `SceneFingerprint.cpp` is 1567 lines; roughly 960 of them are per-type builders that exist only to describe fields the archive already knows how to write.

The fingerprint was introduced by the archived change `2026-11-02-refactor-automation-and-sample-scenes`, whose Decision 3 rejected byte-comparing saved scenes because "unordered containers make output order non-semantic and compression output is not a stable interface". The compression half of that reasoning does not hold: `FileUtil::SaveCompressedFile` runs the same `SaveDocument` as the plain-JSON path and LZ4-compresses the result, so a `.bin` file's payload is byte-identical to the `.json` file it would have produced. The unordered-container half does hold, and is addressed below.

Three facts measured against the `feature` showcase scene shape the design:

- The saved document is not byte-stable across a round trip. `gen1.json` and `gen2.json` differ at byte 98 because the top-level `textures` array is emitted in `EntityManager` hash order.
- The only difference beyond ordering is the `material_id` uniform: `12` in one document, `25` in the other.
- After sorting the asset arrays and material maps and dropping `material_id`, the two documents are identical over 7,116,377 characters, floats included. No fixed-point quantization is needed.

## Goals / Non-Goals

**Goals:**

- One implementation of persistent state: the archive.
- Saved documents that are byte-stable for a given scene state, independent of registration order.
- A comparison that reports structured field paths on failure.
- Coverage that cannot silently lapse when a component type is added.

**Non-Goals:**

- Changing the scene file format version, the archive field-policy model, or the loader's semantics.
- Changing how the engine renders, cooks, or packs scenes.
- Tightening the float comparison tolerance below its current documented value.

## Decisions

### Decision 1: Compare saved documents, not live scenes

The comparison operates on two saved scene documents rather than on two live `Scene` objects. The document is what the archive persists, so it is the definition of persistent state, and a change to any serializer is automatically in scope for the comparison.

**Alternatives considered:** Keeping the comparison on live scenes and feeding it from the archive into an in-memory tree was rejected as strictly more machinery for the same coverage: it needs a second archive target, and it still cannot see a field the archive fails to write. Comparing the files directly was chosen because it also proves the on-disk artifact is stable, which is what ships.

### Decision 2: Make the serializer deterministic rather than canonicalizing the comparator

Three places emit hash order: the top-level asset arrays in `Scene::Serialize`, a material's `textures` and `uniforms` slots in `Material::Serialize`, and a render chain entry's `uniforms` in `RenderSettings::Serialize`. Each is changed to emit in sorted key order.

Sorting in the serializer rather than in the comparator keeps the comparator free of type knowledge, which is the property that lets the per-type fingerprint builders go away. It also makes saved scene files reproducible, so scene files diff cleanly in version control and pack identically across machines. `PostProcessEffect::Serialize` already sorts its uniforms into a `std::map` before writing and is left alone.

Arrays whose order carries meaning - node children, node components, joints, animation channels, the postprocess chain - keep their existing order. Sorting those would hide real hierarchy bugs.

**Alternatives considered:** Sorting the arrays inside the comparator was rejected because the comparator would need to know which arrays are hash artifacts, which is the same type knowledge the change removes. Sorting JSON object keys was rejected as unnecessary: the writer emits members in a fixed order per type, and the canonical tree keys objects by name anyway.

### Decision 3: Stop persisting `material_id`

`Material::m_ID` is `++m_GlobalID`, a process-global counter assigned at construction. `Material::Serialize` writes it into the document as a `material_id` uniform, and the loading branch replaces it with the newly constructed material's own identifier. The persisted value is therefore stale on every load, and nothing in the engine reads it. It is dropped from the saving branch.

**Alternatives considered:** Excluding it from the comparison instead of from the document was rejected: a file that carries a value known to be wrong is a defect whether or not a test looks at it. The loading branch is unchanged, so a reloaded material still carries a runtime identifier.

### Decision 4: Keep the canonical tree and the diff, drop the fingerprint builders

`SceneFingerprint`'s tree type, canonical text form, parser, and differ are type-agnostic and are exactly what the new comparison needs. They are kept, renamed to `CanonNode` and `SceneCompare`, and fed from rapidjson instead of from per-type builders. The per-type builders, the live-scene `Build`, and the per-type `Fingerprint*` functions are deleted.

The comparison reports field paths in the shape of the document - `nodes/childs[2]/components[1]/fields/intensity` - which is the shape a reader of the file already knows.

**Alternatives considered:** Comparing raw bytes was rejected because a failure would report a byte offset rather than a field. Building a second tree type from scratch was rejected because the existing one already encodes the tolerance rule and the path syntax.

### Decision 5: Keep the tolerance, do not quantize the document

The documented 1e-4 abs/relative float tolerance stays, applied when comparing real values. The document itself is not quantized: `FileUtil` writes floats with rapidjson's full round-trip precision, every persistent field in the engine is float32, and a float32 value survives the write-read cycle exactly.

Fixed-point quantization in the saved document was rejected because it would destroy exact round-tripping and mask real precision loss, while buying nothing: the comparison is what needs a tolerance, and it already has one.

### Decision 6: The structural counts stay a live-scene summary

The counts check reads the live scene through the `EntityManager` and the node tree. It uses no per-type knowledge and needs no maintenance, so it stays as a cheap first-line failure summary alongside the document comparison.

## Risks / Trade-offs

- [Saved scene files change shape] -> The asset arrays are reordered and `material_id` is dropped. Loading is unaffected: array order carries no meaning, and the loader regenerates `material_id`. Checked-in scenes are re-saved by their authoring scripts; a stale file still loads.
- [A serializer that writes an unsorted array is added later] -> The comparison fails with a path naming the array, and the fix is to sort at the write site, which is the intended habit.
- [Comparing documents requires reading files into memory] -> The `feature` scene document is about 7 MB. The comparison parses both documents; the Lua binding returns only the differences, not the documents.
- [The comparison cannot observe state the archive does not persist] -> Accepted. A field missing from the archive is not persistent state, and the archive is the single place to fix that.
- [`ExtractMemoryBackedTextures` mutates the scene during a save] -> A save rewrites a memory-backed texture's path to its bare sibling filename and writes the file. A test that compares a scene against itself across a save must compare the second and third serializations, or serialize once to establish the baseline first.

## Migration Plan

The change is one commit series, reviewed before it lands:

1. Sort the three unordered write sites and drop `material_id` from the saving branch.
2. Add `SceneCompare` and the rapidjson-to-tree adapter; keep the tree and differ.
3. Replace the Lua bindings and update the three test scripts.
4. Delete `SceneFingerprint` and update `openspec/specs/scene-round-trip`, `openspec/specs/lua-test-scripts`, and `openspec/specs/editor-ui-automation`.

Rollback is a revert of the whole series: the on-disk format change and the API change land together, and no data migration is involved.

## Open Questions

None.
