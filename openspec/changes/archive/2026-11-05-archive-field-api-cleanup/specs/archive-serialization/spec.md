## MODIFIED Requirements

### Requirement: Archive failures SHALL propagate without silently producing a valid result

Archives SHALL track malformed fields, failed conversions, unsupported versions, and I/O failures. The top-level load or save operation SHALL return failure when archive processing fails, and a failed load SHALL NOT install a partially loaded scene as the active scene. Field compatibility rules SHALL distinguish absent optional fields, which receive the default declared by the field's own type, from present fields with invalid data.

#### Scenario: Invalid required field fails the load

- **WHEN** a scene document contains a present required field with an invalid type or value
- **THEN** the archive records an error identifying the field path
- **AND** the top-level load returns false without activating the partial scene

#### Scenario: Absent optional field uses the default declared by its type

- **WHEN** an older supported scene omits an optional field
- **THEN** loading leaves the field at the default its type declares
- **AND** the absence alone does not fail the archive

### Requirement: Scene-persisted types SHALL serialize through one direction-aware entry point

Each scene-persisted type SHALL expose a `Serialize(Archive&)` entry point that defines its persistent fields once for loading and saving. Named fields SHALL be transferable with `Archive` `<<` operators or an equivalent named-field wrapper built on those operators. A field wrapper SHALL NOT carry a default value; the default belongs to the field's type declaration. Direction-specific code SHALL be limited to object lifetime, resource registration, derived-data rebuilds, and other behavior that cannot be expressed symmetrically; it SHALL use `IsLoading()`, `IsSaving()`, or `IsCooking()` rather than a parallel field schema.

The archive SHALL have exactly one `Loading`, `Saving`, or `Cooking` mode. `IsLoading()`, `IsSaving()`, and `IsCooking()` SHALL report that mode. Cooking SHALL use a reference-collecting archive so metadata discovery does not perform runtime resource decoding, GPU uploads, physics registration, or scene-output writes.

An array field SHALL be transferable as a single named field. For each element, the archive SHALL use the element type's own `Serialize(Archive&)` when the element type has one, and SHALL transfer the element as a plain value otherwise. The archive SHALL frame an element that has its own `Serialize(Archive&)` as an object and SHALL NOT frame a plain value.

#### Scenario: A component uses the same field list in both directions

- **WHEN** a component containing transform, resource-reference, and scalar fields is saved and loaded
- **THEN** both directions execute the same `Serialize` field list
- **AND** only lifetime or derived-data work is guarded by archive direction

#### Scenario: Cooking collects references without runtime side effects

- **WHEN** a cooking archive traverses a live scene
- **THEN** `IsCooking()` is true while `IsLoading()` and `IsSaving()` are false
- **AND** resource references are collected without decoding textures, rebuilding terrain, or registering physics

#### Scenario: An array of structured elements serializes as one field

- **WHEN** a type holds an array whose element type defines `Serialize(Archive&)`
- **THEN** the array is written and read through a single named field transfer
- **AND** each element is written as an object holding that element type's fields

#### Scenario: An array of plain values serializes as one field

- **WHEN** a type holds an array of plain values
- **THEN** the array is written and read through a single named field transfer
- **AND** each element is written as a plain value
