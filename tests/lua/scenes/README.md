# tests/lua/scenes/ - render drivers and retained probes

These scripts require a window/display unless their entry in
`tests/manifest.json` explicitly marks them headless. They are not invoked with
`fury exec` unless that manifest entry says so. User-facing authoring scripts
live beside their examples instead.

- `cloud_visual_check.lua` - deterministic target for the named cloud, PreZ,
  and OpenGL/Metal parity cases in `tests/render_regressions.json`.
- `tree_preview.lua` - render a Kraut tree with `FURY_FORCE_LOD`, `FURY_WIND_*`,
  and `FURY_BB_BIAS` switches.
- `instancing_smoke.lua` - 2500-instance ISM draw-call flatness probe.
- `vegetation_perf.lua` - island vegetation timing probe.
- `grass_field_perf.lua` - dense grass scatter timing (`FURY_GRASS_N`).
- `grass_island_check.lua` - grass-layer batch composition and timing.
- `lod_transition_check.lua` - palm-wall dithered LOD visual review.
- `billboard_pop_check.lua` - tree LOD-to-billboard boundary visual review.

The performance scripts and `tests/prez_perf.sh` are standalone tools, not
functional gates. Use the aggregate runner for automated cloud, PreZ, backend,
and baseline checks. `examples/GenLod.lua` and
`examples/Projects/ocean/populate_island_grass.lua` are the maintained scene
authoring entry points.
