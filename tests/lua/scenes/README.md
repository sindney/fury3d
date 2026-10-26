# tests/lua/scenes/ — scene-generation samples and render harnesses

Scripts here are not regression guards: they author a scene asset or drive
a render that the caller (or a shell wrapper) reviews by hand. They are
not run by `./fury exec` from a CI smoke context; each is invoked from
its own setup.

- `gen_lod.lua` — generate LOD chains on every mesh (or one by name).
- `populate_island_grass.lua` — bake the grass HISM into `ocean_island.bin`.
- `cloud_visual_check.lua` — render target for the cloud / PreZ shell suites.
- `tree_preview.lua` — render a kraut-imported tree fragment with
  `FURY_FORCE_LOD` / `FURY_WIND_*` / `FURY_BB_BIAS` env switches.
- `instancing_smoke.lua` — 2500-instance ISM draw-call flatness probe.
- `vegetation_perf.lua` — `ocean_island.bin` 120-frame perf probe.
- `grass_field_perf.lua` — dense grass scatter perf (`FURY_GRASS_N`).
- `grass_island_check.lua` — grass-layer batch composition + perf.
- `lod_transition_check.lua` — palm-wall dithered LOD swap.
- `billboard_pop_check.lua` — single tree LOD -> billboard boundary check.

Shell wrappers that reference these paths:
- `tests/cloud_visual_suite.sh` -> `tests/lua/scenes/cloud_visual_check.lua`
- `tests/prez_ab_suite.sh` -> `tests/lua/scenes/cloud_visual_check.lua`
- `tests/metal_gl_compare.sh` -> `tests/lua/scenes/cloud_visual_check.lua`
- `tests/pak_pipeline.sh` -> `tests/lua/pak_scene_load.lua` (kept under `tests/lua/`)
