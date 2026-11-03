## ADDED Requirements

### Requirement: Scene-persisted types SHALL serialize through one direction-aware entry point

Each scene-persisted type SHALL expose a `Serialize(Archive&)` entry point that defines its persistent fields once for loading and saving. Named fields SHALL be transferable with `Archive` `<<` operators or an equivalent named-field wrapper built on those operators. Direction-specific code SHALL be limited to object lifetime, resource registration, derived-data rebuilds, and other behavior that cannot be expressed symmetrically; it SHALL use `IsLoading()`, `IsSaving()`, or `IsCooking()` rather than a parallel field schema.

The archive SHALL have exactly one `Loading`, `Saving`, or `Cooking` mode. `IsLoading()`, `IsSaving()`, and `IsCooking()` SHALL report that mode. Cooking SHALL use a reference-collecting archive so metadata discovery does not perform runtime resource decoding, GPU uploads, physics registration, or scene-output writes.

#### Scenario: A component uses the same field list in both directions

- **WHEN** a component containing transform, resource-reference, and scalar fields is saved and loaded
- **THEN** both directions execute the same `Serialize` field list
- **AND** only lifetime or derived-data work is guarded by archive direction

#### Scenario: Cooking collects references without runtime side effects

- **WHEN** a cooking archive traverses a live scene
- **THEN** `IsCooking()` is true while `IsLoading()` and `IsSaving()` are false
- **AND** resource references are collected without decoding textures, rebuilding terrain, or registering physics

### Requirement: Archives SHALL preserve the existing scene envelopes and schema

The JSON archive SHALL read and write the existing named-field scene document. The `.bin` transport SHALL remain the existing LZ4-compressed envelope around that document, and `.json` SHALL remain plain UTF-8 JSON. `FileUtil` load/save functions and extension dispatch SHALL construct the appropriate archive and invoke the shared scene serializer; they SHALL NOT maintain type-specific field logic.

Existing version defaults, rejection of newer unsupported versions, path-keyed asset identity, working-directory resolution, and memory-backed texture extraction SHALL remain in force.

#### Scenario: Existing compressed scene loads through an archive

- **WHEN** an existing supported `.bin` scene is loaded
- **THEN** its LZ4 envelope is decoded before JSON archive deserialization
- **AND** the resulting scene has the same assets and hierarchy as before the serialization migration

#### Scenario: Save by extension selects the envelope only

- **WHEN** the same scene is saved to `.json` and `.bin`
- **THEN** both outputs contain equivalent scene documents
- **AND** only compression and formatting differ

### Requirement: Archive failures SHALL propagate without silently producing a valid result

Archives SHALL track malformed fields, failed conversions, unsupported versions, and I/O failures. The top-level load or save operation SHALL return failure when archive processing fails, and a failed load SHALL NOT install a partially loaded scene as the active scene. Field compatibility rules SHALL distinguish absent optional fields, which receive documented defaults, from present fields with invalid data.

#### Scenario: Invalid required field fails the load

- **WHEN** a scene document contains a present required field with an invalid type or value
- **THEN** the archive records an error identifying the field path
- **AND** the top-level load returns false without activating the partial scene

#### Scenario: Absent optional field uses its legacy default

- **WHEN** an older supported scene omits an optional field
- **THEN** loading applies the field's established legacy default
- **AND** the absence alone does not fail the archive

### Requirement: Cooking SHALL reuse the shared serializer without changing runtime defaults

Cook and package pipelines SHALL use the same `Serialize` implementations as runtime scene persistence. Any cook-only omission, conversion, or derived-data step SHALL be explicit under `IsCooking()`. Normal runtime loading and saving SHALL produce the same persistent values when `IsCooking()` is false.

#### Scenario: Cooking discovers references through the shared schema

- **WHEN** a scene containing terrain, sky, and component resource references is cooked
- **THEN** those references are discovered through the same `Serialize` field lists used by runtime persistence
- **AND** the collected reference set matches the pre-migration cook inventory

#### Scenario: Runtime save is not treated as cooking

- **WHEN** an editor or CLI saves a normal scene
- **THEN** `IsCooking()` is false
- **AND** no cook-only omission or conversion is applied
