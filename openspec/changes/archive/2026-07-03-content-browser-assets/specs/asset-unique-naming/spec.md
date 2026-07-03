## ADDED Requirements

### Requirement: The engine SHALL provide a single `UniqueName` helper shared by assets and nodes

The engine SHALL expose a free function `fury::UniqueName(const std::string& base, const std::function<bool(const std::string&)>& exists)` (or an equivalent method on `Entity`/`EntityManager`) that returns the first non-colliding name for `base` using the suffix scheme `base`, `base (1)`, `base (2)`, …, `base (N)`. For duplicate-style requests the caller SHALL pass a `base` already suffixed with ` (copy)`; the helper then yields ` (copy)`, ` (copy 2)`, ` (copy 3)`, … by treating the numeric suffix as a continuation counter. The helper SHALL scan suffix integers starting at 1 (for the `(N)` form) or 2 (for the `(copy N)` form) up to a hard cap of 100000 and fall back to `base + " (?)"` if exhausted.

The editor's existing `UniqueChildName(SceneNode* parent, const std::string& base)` static function in `EditorWindows.cpp` SHALL be reimplemented as a thin wrapper that calls `UniqueName(base, [&](const std::string& n){ return parent->FindChild(n) != nullptr; })`. The editor's asset rename/duplicate flows SHALL call `UniqueName` with a predicate backed by `EntityManager::Get<T>(name) != nullptr`.

The helper SHALL be declared in a non-editor header (e.g. `engine/Fury/EntityUtil.h`) so engine-side code can use it without depending on `WITH_EDITOR`.

#### Scenario: No collision returns the base unchanged

- **WHEN** `UniqueName("Cube", exists)` is called and `exists("Cube")` returns `false`
- **THEN** the return value is exactly `"Cube"`

#### Scenario: First collision yields `(1)`

- **WHEN** `UniqueName("Cube", exists)` is called and `exists("Cube")` returns `true` but `exists("Cube (1)")` returns `false`
- **THEN** the return value is `"Cube (1)"`

#### Scenario: Multiple collisions skip to the next free integer

- **WHEN** `UniqueName("Cube", exists)` is called with `exists` returning `true` for `"Cube"`, `"Cube (1)"`, `"Cube (2)"`, and `false` for `"Cube (3)"`
- **THEN** the return value is `"Cube (3)"`

#### Scenario: Duplicate-style base yields the `(copy N)` sequence

- **WHEN** `UniqueName("Cube (copy)", exists)` is called with `exists` returning `true` for `"Cube (copy)"` and `"Cube (copy 2)"` and `false` for `"Cube (copy 3)"`
- **THEN** the return value is `"Cube (copy 3)"`

#### Scenario: Node rename uses the same scheme as asset rename

- **WHEN** a node "Node" is duplicated under a parent that already has "Node" and "Node (1)"
- **AND** the duplicate calls `UniqueChildName(parent, "Node")` which delegates to `UniqueName`
- **THEN** the new node is named `"Node (2)"`
- **AND** the same call with an asset-naming predicate (`EntityManager::Get<Mesh>`) would produce the identical suffix for a mesh named "Node" under the same collisions

#### Scenario: Hard cap fallback

- **WHEN** `exists` returns `true` for every name from `"Cube"` through `"Cube (100000)"`
- **THEN** the return value is `"Cube (?)"`
