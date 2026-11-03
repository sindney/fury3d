# Examples

The maintained showcases are FeatureGame and IslandGame. Build the launchers
from the repository root, then run them from this directory so `Resource/` and
project paths resolve correctly.

```bash
cmake -S engine -B build
cmake --build build --target fury furye furye-cli -j2
cd examples
```

## FeatureGame

```bash
./fury FeatureGame.lua hdr=0
./fury FeatureGame.lua hdr=1
```

FeatureGame loads `Projects/feature/feature.bin`: tanks, Fox and james skeletal
animation, fire and smoke particles, directional CSM/point/spot lighting, PBR,
and a small RmlUi overlay. Both HDR modes use the same scene and camera. The
`FEATUREGAME_HDR=0|1` environment variable is an alternative to the argument.

Rebuild the scene from canonical sources with:

```bash
./fury exec Projects/samples/fox/Fox.gltf Projects/feature/setup_feature_scene.lua
```

## IslandGame

```bash
./fury IslandGame.lua
```

IslandGame loads `Projects/ocean/ocean_island.bin`: island terrain and
collision, PalmTree2 vegetation, infinite ocean, sky, and a beach spawn. It
provides the main menu, options, pause menu, HUD, and first-person player.
Settings normally persist to `Projects/ocean/game_settings.cfg`; automation
sets `FURY_GAME_SETTINGS` to an isolated path instead.

Rebuild the island chain with:

```bash
./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_island.lua
./fury Projects/ocean/populate_island_grass.lua /tmp/ocean_island_grass.bin
cp /tmp/ocean_island_grass.bin Projects/ocean/ocean_island.bin
./fury exec Projects/ocean/ocean_island.bin Projects/ocean/setup_ocean_island_trees.lua
```

`Projects/ocean/populate_island_grass.lua` writes the intermediate grass scene
promoted by the rebuild chain; it never overwrites its input directly.

## Editor and retained samples

- `./furye Editor.lua` opens the editor; `./fury Player.lua` is the generic
  player launcher used by play mode.
- `Projects/outdoor/` retains focused terrain, sky, water, buoyancy, particle,
  and physics fixtures plus their setup scripts.
- `Projects/ocean/ocean_base.bin`, `ocean_infinite.bin`, and `ocean_lake.bin`
  retain the smaller ocean authoring stages.
- `Projects/samples/` contains the canonical tank, Fox, james, glass, and
  Sponza source assets. Do not duplicate these models into consuming projects.
- `GenLod.lua` attaches generated LOD chains to a loaded scene and optionally
  saves a new scene.

Shared RmlUi styling and fonts live under `Resource/Ui/`; vegetation, ocean,
pipeline, shader, and other engine resources remain under `Resource/`. Shared
scene references use the `Engine/` prefix so saving a project does not copy
those assets into the project directory.

See `tests/README.md` for headless, editor UI, and deterministic rendering
checks for both showcases.
