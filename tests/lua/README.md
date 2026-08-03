# tests/lua/ — runnable Lua smoke tests for `fury exec`

This folder holds Lua scripts invoked via `./fury exec <scene> <script.lua> [args...]`.
They serve a dual purpose: **binding smoke tests** that prove the new Lua
bindings work end-to-end from the `exec` path, and **worked examples** that
AI agents can copy as starting points.

These scripts are intentionally NOT replacements for existing CLI features.
`fury convert` already does headless format conversion, and `fury info`
already does headless scene summarization — replicating those from Lua
would be redundant. The scripts here exercise specific Lua binding surface
that no existing CLI feature touches:

- `smoke_save.lua` — proves `Scene.SaveActive` / `FileUtil.SaveByExtension`
  works from Lua (round-trip a scene to `.json` or `.bin`). NOT a replacement
  for `fury convert`; the conversion feature itself is the `convert`
  subcommand's job.
- `smoke_iterate.lua` — proves `Scene.ForEachMesh` / `ForEachMaterial` /
  `ForEachNode` work from Lua. NOT a replacement for `fury info`; the
  summarization feature is the `info` subcommand's job.
- `dump_nodes.lua` / `dump_ground.lua` — scene-graph + material-uniform
  dump helpers for debugging content regressions (ad-hoc, kept as
  examples of the exec-inspection pattern).
- `gen_lod.lua` — exercises `MeshSimplifier.SimplifyMesh` +
  `Mesh.SetLodMeshes`. Genuinely new — no existing CLI feature generates
  LODs.
- `smoke_particles.lua` — particle-system regression guards: name-based
  renderer→system resolution, save/reload module round-trip, and the
  `Importer.MergeInto` ParticleSystem transfer. Exercises
  `SceneNode.GetParticleRenderer`, `ParticleRenderer.GetSystem(Name)`,
  and `Scene.GetParticleSystem` — surface no CLI subcommand touches.

## Usage

From the repo root:

```bash
# Iterate scene contents (read-only).
./fury exec examples/Projects/tank/scene.bin tests/lua/smoke_iterate.lua

# Round-trip a scene to .bin (proves the save binding works).
./fury exec examples/Projects/tank/scene.bin tests/lua/smoke_save.lua /tmp/out.bin

# Round-trip a scene to .json (the inverse direction).
./fury exec examples/Projects/tank/scene.bin tests/lua/smoke_save.lua /tmp/out.json

# Generate LODs on every mesh and save.
./fury exec examples/Projects/tank/scene.bin tests/lua/gen_lod.lua --all /tmp/out.json

# Generate LODs and just log counts (no save).
./fury exec examples/Projects/tank/scene.bin tests/lua/gen_lod.lua --all

# Target a single mesh by name.
./fury exec examples/Projects/tank/scene.bin tests/lua/gen_lod.lua TankMesh /tmp/out.json
```

## Script reference

### `smoke_iterate.lua`

**Purpose:** exercise the iteration bindings end-to-end.
**Args:** none.
**Sample:**
```bash
./fury exec examples/Projects/tank/scene.bin tests/lua/smoke_iterate.lua
```
**Output:**
```
ForEachMesh callbacks: <n>
ForEachMaterial callbacks: <n>
ForEachNode callbacks: <n>
```
**Notes:** read-only; no file written. NOT a replacement for `fury info`.

### `smoke_save.lua`

**Purpose:** round-trip a scene through the Lua save binding.
**Args:** `<out_path>` — output file; extension `.json` or `.bin` selects
the serializer.
**Sample:**
```bash
./fury exec examples/Projects/tank/scene.bin tests/lua/smoke_save.lua /tmp/out.bin
```
**Notes:** proves the Lua save binding works. The actual format-conversion
feature is `fury convert`'s job.

### `smoke_particles.lua`

**Purpose:** regression-guard the particle system's asset-model invariants.
**Args:** none.
**Sample:**
```bash
./fury exec examples/Projects/outdoor/outdoor_water.bin tests/lua/smoke_particles.lua
```
**Output:**
```
1. name resolution OK (2 renderers -> 2 systems)
2. serialization round-trip OK (systems + renderers + modules survive)
3. MergeInto transfer OK (systems resolve in the merged scene)
smoke_particles: OK
```
**Notes:** hardcodes the outdoor_water scene's two emitters (`FireEmber`,
`SmokePlume`) — run it against that scene. The GPU/GUI-only traps
(dynamic-mesh `SetDirty`, content-browser tile casts) are covered by the
furye screenshot captures, not by this exec script (no GL context there).

### `gen_lod.lua`

**Purpose:** generate LODs via `MeshSimplifier.SimplifyMesh` and attach them
via `Mesh.SetLodMeshes`. Genuinely new — no existing CLI feature does this.
**Args:**
- `<mesh_name|--all>` — which mesh to target (or `--all` for every mesh).
- `[out_path]` — optional output path; if omitted, just logs counts.

**Sample (save):**
```bash
./fury exec examples/Projects/tank/scene.bin tests/lua/gen_lod.lua --all /tmp/out.json
```
**Sample (no save, just log counts):**
```bash
./fury exec examples/Projects/tank/scene.bin tests/lua/gen_lod.lua --all
```

## Exit codes

All scripts follow the `fury exec` convention:

- `0` — script ran to completion.
- `1` — missing arg / no match / save failure (with stderr message).
- `2` — internal C++ exception escaping `DoExec`.

## See also

- [`docs/CLI.md`](../CLI.md) — full `fury exec` reference (syntax, accepted
  extensions, exit codes, no-window / no-engine-boot invariants).
- [`docs/LUA_API.md`](../LUA_API.md) — auto-generated per-binding Lua API
  reference.
- [`engine/Fury/LuaBindings.cpp`](../../engine/Fury/LuaBindings.cpp) — C++
  side of every binding.