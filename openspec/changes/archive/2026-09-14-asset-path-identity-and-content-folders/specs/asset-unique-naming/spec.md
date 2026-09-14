# asset-unique-naming

## Purpose

A `UniqueName` helper shared by assets and nodes that picks the first non-colliding suffix from `base`/`base (1)`/`base (2)`/... up to a hard cap, with a separate `(copy N)` continuation sequence for duplicates. Used by editor flows (asset rename/duplicate, `EditorWindows.cpp`'s `UniqueChildName` static) and by engine-side callers that need collision-free names for non-path-keyed entities. Reusable across asset and node domains via a predicate parameter.

Note: this spec covers the *`UniqueName` helper*, not the EntityManager's broader identity system. The "first-registered-wins name index with one-shot warning" behavior in `EntityManager::IndexName` was the historical dedupe mechanism for asset entities, but is now removed — asset dedupe is enforced structurally by the path-keyed `EntityManager` (see the `asset-path-identity` spec). The `UniqueName` helper remains for non-asset entities (`SceneNode`, `Component`, etc.) where names are arbitrary strings.

## Purpose

A `UniqueName` helper shared by assets and nodes that picks the first non-colliding suffix from `base`/`base (1)`/`base (2)`/... up to a hard cap, with a separate `(copy N)` continuation sequence for duplicates. Used by editor flows (asset rename/duplicate, `EditorWindows.cpp`'s `UniqueChildName` static) and by engine-side callers that need collision-free names for non-path-keyed entities. Reusable across asset and node domains via a predicate parameter.

Note: this spec covers the *`UniqueName` helper*, not the EntityManager's broader identity system. Asset dedupe is enforced structurally by the path-keyed `EntityManager` (see the `asset-path-identity` spec); the `UniqueName` helper remains only for non-asset entities (`SceneNode`, `Component`, etc.) where names are arbitrary strings.

## Requirements

### Requirement: The engine SHALL provide a single `UniqueName` helper shared by non-asset entities and editor flows

The engine SHALL expose a free function `fury::UniqueName(const std::string& base, const std::function<bool(const std::string&)>& exists)` (or an equivalent method on `Entity`/`EntityManager`) that returns the first non-colliding name for `base` using the suffix scheme `base`, `base (1)`, `base (2)`, …, `base (N)`. For duplicate-style requests the caller SHALL pass a `base` already suffixed with ` (copy)`);` the helper then yields ` (copy)`, ` (copy 2)`, ` (copy 3)`, … by treating the numeric suffix as a continuation counter. The helper SHALL scan suffix integers starting at 1 (for the `(N)` form) or 2 (for the `(copy N)` form) up to a hard cap of 100000 and fall back to `base + " (?)"` if exhausted.

The helper's caller population is restricted to **non-path-keyed entity types**: `SceneNode`, `Component`, and any other `Entity` subclass that does not carry a file path. For asset types (`Texture`, `Material`, `Mesh`, `AnimationClip`, `ParticleSystem`, `Heightmap`, `OceanWaves`), uniqueness is enforced structurally by the EM's path-keyed `Add`, not by `UniqueName`. Calling `UniqueName` for an asset path is permitted but pointless — `em->Add<Texture>(tex)` already rejects same-path duplicates at insertion time.

The editor's existing `UniqueChildName(SceneNode* parent, const std::string& base)` static function in `EditorWindows.cpp` SHALL be reimplemented as a thin wrapper that calls `UniqueName(base, [&](const std::string& n){ return parent->FindChild(n) != nullptr; })`. The editor's SceneNode rename/duplicate flows SHALL call `UniqueName` with a predicate backed by `SceneNode::FindChild` (or equivalent).

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

#### Scenario: Node rename uses the same scheme

- **WHEN** a node "Node" is duplicated under a parent that already has "Node" and "Node (1)"
- **AND** the duplicate calls `UniqueChildName(parent, "Node")` which delegates to `UniqueName`
- **THEN** the new node is named `"Node (2)"`

#### Scenario: Hard cap fallback

- **WHEN** `exists` returns `true` for every name from `"Cube"` through `"Cube (100000)"`
- **THEN** the return value is `"Cube (?)"`

#### Scenario: UniqueName is not used as the asset dedupe mechanism

- **WHEN** an asset named "grass.png" is added to the EM via `em->Add<Texture>(tex)` where the path is already registered
- **THEN** the `Add` returns `false` (path-keyed rejection)
- **AND** the caller has no need to call `UniqueName` to find a non-colliding name — the existing one stays put under its path