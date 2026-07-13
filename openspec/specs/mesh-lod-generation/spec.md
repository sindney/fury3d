# mesh-lod-generation

## Purpose

User-selectable mesh simplification method for the editor's "Generate LODs…" action. The simplifier dispatches to one of three meshoptimizer entry points based on a `Method` enum carried on `MeshSimplifyOptions`. The default is the border-preserving quadric method so out-of-the-box LOD generation does not aggressively collapse UV seams.

## Requirements

### Requirement: `MeshSimplifyOptions` SHALL carry a user-selectable simplification method

`MeshSimplifyOptions` (`engine/Fury/MeshSimplifier.h`) SHALL expose a `Method` enum with at least three values and a field selecting the active method:

| Method | Underlying call | Character |
| --- | --- | --- |
| `Quadric` (default) | `meshopt_simplify` with `meshopt_SimplifyLockBorder` when `lock_borders` is set | Quadric error metric; preserves borders / UV seams; least aggressive |
| `Sloppy` | `meshopt_simplifySloppy` | Grid-based; ignores borders; most aggressive |
| `QuadricLegacy` | `meshopt_simplify` without border locking (or the legacy entry point) | Quadric without seam preservation |

The default SHALL be `Quadric` (border-preserving), not `Sloppy`, so out-of-the-box generation is not over-aggressive. The existing `lod_count`, `reduction_ratio`, `target_error`, and `lock_borders` fields SHALL remain and continue to apply.

#### Scenario: Default method is the border-preserving quadric

- **WHEN** a `MeshSimplifyOptions` is default-constructed
- **THEN** its method is `Quadric`

#### Scenario: Options struct exposes all three methods

- **WHEN** code sets `opts.method` to `Sloppy`, `Quadric`, or `QuadricLegacy`
- **THEN** the field retains the assigned value and is readable by `MeshSimplifier`

### Requirement: `MeshSimplifier` SHALL dispatch on the selected method instead of a hard-coded path

`MeshSimplifier` SHALL select the meshoptimizer entry point from `opts.method` for every submesh, replacing the hard-coded `const bool use_sloppy = true`. When `method == Sloppy` it SHALL call `meshopt_simplifySloppy`; otherwise it SHALL call `meshopt_simplify`, passing `meshopt_SimplifyLockBorder` when `method == Quadric && lock_borders` and no lock flag when `method == QuadricLegacy`. The dead `else` branch that was previously unreachable SHALL become live.

#### Scenario: Quadric method preserves borders

- **WHEN** `SimplifyMesh` runs with `method == Quadric` and `lock_borders == true`
- **THEN** the submesh is simplified via `meshopt_simplify` with the `meshopt_SimplifyLockBorder` option
- **AND** `meshopt_simplifySloppy` is not called

#### Scenario: Sloppy method uses the grid-based simplifier

- **WHEN** `SimplifyMesh` runs with `method == Sloppy`
- **THEN** the submesh is simplified via `meshopt_simplifySloppy`

#### Scenario: Legacy quadric method omits the border lock

- **WHEN** `SimplifyMesh` runs with `method == QuadricLegacy`
- **THEN** the submesh is simplified via `meshopt_simplify` with no `meshopt_SimplifyLockBorder` flag

### Requirement: The "Generate LODs…" dialog SHALL let the user choose the simplification method

The editor's "Generate LODs…" dialog (`engine/Fury/Editor/EditorAssetWindows.cpp`) SHALL present a **Method** dropdown alongside the existing Total levels / Reduction ratio / Target error fields. The dropdown's options SHALL map to the `MeshSimplifyOptions::Method` values with human-readable labels (e.g. "Quadric (preserve borders)", "Sloppy (aggressive)", "Quadric (legacy)"). The selected method SHALL be copied into `MeshSimplifyOptions` before `SimplifyMesh` is invoked, and the dialog's default selection SHALL be the border-preserving Quadric method.

#### Scenario: Dialog defaults to the border-preserving method

- **WHEN** the "Generate LODs…" dialog is opened
- **THEN** the Method dropdown shows the border-preserving Quadric option selected

#### Scenario: Selected method reaches the simplifier

- **WHEN** the user selects "Sloppy (aggressive)" and clicks Generate
- **THEN** the `MeshSimplifyOptions` passed to `SimplifyMesh` has `method == Sloppy`
- **AND** the generated LOD chain reflects the sloppy simplification

#### Scenario: Choosing the quadric method yields less-aggressive LODs

- **WHEN** the user selects the border-preserving Quadric method and generates LODs on a mesh with UV seams
- **THEN** the generated LOD meshes retain their border/seam vertices
- **AND** the reduction is gated by `target_error` rather than collapsing seams unconditionally