# Benchmarks: add-render-thread

Method: fury player, uncapped fps (`FURY_MAX_FPS=0`), steady-state ms/frame
from the delta of two runs (--screenshot-frame 60 vs 660; startup cancels).
Each case ran 3+ times; values are representative mid-runs. macOS arm64, Debug
build (RelWithDebInfo-class engine, Tracy compiled in but disarmed).

| Scene | Threading OFF | Threading ON | Speedup |
|---|---|---|---|
| ocean_island.bin (vegetation-heavy) | 7.05-7.29 ms | 6.32-6.69 ms | ~1.10x |
| ocean_island_stress.bin (island + 12 fire/smoke emitters) | 8.30-8.36 ms | 6.92-7.09 ms | ~1.18x |
| outdoor.bin (light, 43 meshes) | 0.82-0.86 ms | 0.83-0.85 ms | ~1.00x (neutral, no regression) |

Notes:
- Earlier capped runs (default 144 fps cap) showed a false outdoor
  "regression" -- the cap sleeps inside window.display(), which moved to
  the render thread and changed the cadence. Uncapped, outdoor is neutral.
- Tracy timelines captured for both modes (island, 120 frames each):
  threading-off: SubmitFrame 6.97 ms/frame (wraps ExecutePacket 6.0),
  Lua+Render 1.24, GatherFrame 1.2; threading-on: ExecutePacket 5.9,
  SubmitFrame 5.3, Lua+Render 1.45, GatherFrame 1.42. The scene is
  submission-bound (InstancedCulling ~4.1 ms/frame) yet overlap still
  buys ~0.7 ms/frame.
- Stability: 3x 1000-frame threaded runs on the stress scene, exit 0,
  no hangs; RSS steady (445 MB sampled mid-run). Frame queue depth 2 +
  blocking submit held (no growth).
- The stress scene was `examples/Projects/ocean/ocean_island_stress.bin`
  (island + 6 ring sites of fire/smoke emitters transplanted from
  outdoor_water.bin, FireEmber 1024 max/60-rate, SmokePlume 512/15) --
  a benchmark aid, removed before commit. Recreate it any time with
  `fury convert scene` json<->bin round-trips.
