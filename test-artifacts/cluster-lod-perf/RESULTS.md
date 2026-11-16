# Cluster LOD perf comparison (task 5.6)

Current support note: Vulkan/MoltenVK has no indirect-count support and is not a
supported GPU cluster backend. Vulkan numbers below are historical bring-up
measurements, not evidence of a current supported rendering path.

Host: macOS Apple M4 Pro. All runs uncapped (`FURY_MAX_FPS=0`), bench line
averages frames 1..120 (`FURY_CLUSTER_BENCH=1`); screenshot at frame 120 ends
each bench run. Cluster stats from `--gpu-dump` at frame 30.

Cross-run CPU variance on this host is high (single 1..120 average per
launch; e.g. Vulkan Sponza legacy 5.0 ms vs cluster 2.4 ms with byte-identical
work - both modes full legacy fallback). Treat frame_cpu_ms below as
+/-50% unless replicated; the stable signals are draw_calls, cluster
diagnostics counts, and the Tracy GPU-zone means (1400+ frames each).

**GPU time policy**: Metal backend - GPU time unmeasured (no Tracy GPU
zone wiring for Metal; Metal GPU timestamps unavailable on this host).
Vulkan backend - `pass_gbuffer` GPU zones captured via Tracy; timings are
**MoltenVK-emulated** (CPU-emulated command processing on Apple Silicon),
NOT native Vulkan hardware performance. Do not cite as Vulkan MDI numbers.

## Backend: metal

native Metal (Apple M4 Pro)

### feature - `Projects/feature/feature.bin`

Camera `15,8,17,41.43,-15.82,5000` (deterministic near tank pose (matches cluster_lod_render.py CAM_NEAR))

```
/Users/sindney/Documents/git/furyengine/fury3d/examples/fury Player.lua Projects/feature/feature.bin --rhi=metal --screenshot frame120.png --screenshot-frame 120  (env: FURY_MAX_FPS=0 FURY_CLUSTER_BENCH=1 FURY_CLUSTER_LOD=<0|1> FURY_CAM=15,8,17,41.43,-15.82,5000)
```

| mode | frame_cpu_ms (1..120 avg) | gbuffer_cpu_ms | draw_calls |
|---|---|---|---|
| legacy | 8.169 | 0.847 | 56 |
| cluster | 8.288 | 2.982 | 36 |

Cluster stats (`--gpu-dump` frame 30):

| mode | diagnostics | batches | indirect commands | selected | drawn triangles | fallback instances |
|---|---|---|---|---|---|---|
| legacy | none (cluster path inactive in legacy mode (no diagnostics expected)) | - | - | - | - | - |
| cluster | yes | 5 | 1527 | 1527 | 128880 | 0 |

GPU time:

- legacy: unmeasured - Metal backend has no Tracy GPU zone wiring (GpuZoneScope covers Vulkan/GL only) and Metal GPU timestamps are unavailable on this host
- cluster: unmeasured - Metal backend has no Tracy GPU zone wiring (GpuZoneScope covers Vulkan/GL only) and Metal GPU timestamps are unavailable on this host

### sponza - `Projects/samples/sponza/Sponza.bin`

Camera `-48,3000,300,0,-75` (bird's-eye over the courtyard; scene has one dim sun (intensity 0.3), mesh Mesh_0 has 103 submeshes > MaxSubmeshes(8): full legacy fallback)

```
/Users/sindney/Documents/git/furyengine/fury3d/examples/fury Player.lua Projects/samples/sponza/Sponza.bin --rhi=metal --screenshot frame120.png --screenshot-frame 120  (env: FURY_MAX_FPS=0 FURY_CLUSTER_BENCH=1 FURY_CLUSTER_LOD=<0|1> FURY_CAM=-48,3000,300,0,-75)
```

| mode | frame_cpu_ms (1..120 avg) | gbuffer_cpu_ms | draw_calls |
|---|---|---|---|
| legacy | 8.053 | 1.838 | 120 |
| cluster | 8.032 | 1.743 | 120 |

Cluster stats (`--gpu-dump` frame 30):

| mode | diagnostics | batches | indirect commands | selected | drawn triangles | fallback instances |
|---|---|---|---|---|---|---|
| legacy | none (cluster path inactive in legacy mode (no diagnostics expected)) | - | - | - | - | - |
| cluster | none (no accepted cluster instances: Sponza Mesh_0 has 103 submeshes > ClusterRenderer MaxSubmeshes(8), whole mesh uses legacy fallback) | - | - | - | - | - |

GPU time:

- legacy: unmeasured - Metal backend has no Tracy GPU zone wiring (GpuZoneScope covers Vulkan/GL only) and Metal GPU timestamps are unavailable on this host
- cluster: unmeasured - Metal backend has no Tracy GPU zone wiring (GpuZoneScope covers Vulkan/GL only) and Metal GPU timestamps are unavailable on this host

## Backend: vulkan

MoltenVK emulation on Apple Silicon macOS - NOT native Vulkan hardware; indirect count unsupported (single-draw emulation path, not MDI)

### feature - `Projects/feature/feature.bin`

Camera `15,8,17,41.43,-15.82,5000` (deterministic near tank pose (matches cluster_lod_render.py CAM_NEAR))

```
/Users/sindney/Documents/git/furyengine/fury3d/examples/fury Player.lua Projects/feature/feature.bin --rhi=vulkan --screenshot frame120.png --screenshot-frame 120  (env: FURY_MAX_FPS=0 FURY_CLUSTER_BENCH=1 FURY_CLUSTER_LOD=<0|1> FURY_CAM=15,8,17,41.43,-15.82,5000)
```

| mode | frame_cpu_ms (1..120 avg) | gbuffer_cpu_ms | draw_calls |
|---|---|---|---|
| legacy | 1.341 | 0.523 | 56 |
| cluster | 2.042 | 1.236 | 36 |

Cluster stats (`--gpu-dump` frame 30):

| mode | diagnostics | batches | indirect commands | selected | drawn triangles | fallback instances |
|---|---|---|---|---|---|---|
| legacy | none (cluster path inactive in legacy mode (no diagnostics expected)) | - | - | - | - | - |
| cluster | yes | 5 | 1527 | 1527 | 128880 | 0 |

GPU time:

- legacy: pass_gbuffer GPU mean=0.524 ms, median=0.497 ms (MoltenVK-emulated Vulkan GPU timestamp (not native GPU perf), 1779 frames, warmup drop 30) [/Users/sindney/Documents/git/furyengine/fury3d/test-artifacts/cluster-lod-perf/vulkan/tracy_feature_legacy.tracy]
- cluster: pass_gbuffer GPU mean=1.484 ms, median=1.664 ms (MoltenVK-emulated Vulkan GPU timestamp (not native GPU perf), 1779 frames, warmup drop 30) [/Users/sindney/Documents/git/furyengine/fury3d/test-artifacts/cluster-lod-perf/vulkan/tracy_feature_cluster.tracy]

### sponza - `Projects/samples/sponza/Sponza.bin`

Camera `-48,3000,300,0,-75` (bird's-eye over the courtyard; scene has one dim sun (intensity 0.3), mesh Mesh_0 has 103 submeshes > MaxSubmeshes(8): full legacy fallback)

```
/Users/sindney/Documents/git/furyengine/fury3d/examples/fury Player.lua Projects/samples/sponza/Sponza.bin --rhi=vulkan --screenshot frame120.png --screenshot-frame 120  (env: FURY_MAX_FPS=0 FURY_CLUSTER_BENCH=1 FURY_CLUSTER_LOD=<0|1> FURY_CAM=-48,3000,300,0,-75)
```

| mode | frame_cpu_ms (1..120 avg) | gbuffer_cpu_ms | draw_calls |
|---|---|---|---|
| legacy | 4.997 | 2.374 | 120 |
| cluster | 2.389 | 1.744 | 120 |

Cluster stats (`--gpu-dump` frame 30):

| mode | diagnostics | batches | indirect commands | selected | drawn triangles | fallback instances |
|---|---|---|---|---|---|---|
| legacy | none (cluster path inactive in legacy mode (no diagnostics expected)) | - | - | - | - | - |
| cluster | none (no accepted cluster instances: Sponza Mesh_0 has 103 submeshes > ClusterRenderer MaxSubmeshes(8), whole mesh uses legacy fallback) | - | - | - | - | - |

GPU time:

- legacy: pass_gbuffer GPU mean=0.847 ms, median=0.802 ms (MoltenVK-emulated Vulkan GPU timestamp (not native GPU perf), 1415 frames, warmup drop 30) [/Users/sindney/Documents/git/furyengine/fury3d/test-artifacts/cluster-lod-perf/vulkan/tracy_sponza_legacy.tracy]
- cluster: pass_gbuffer GPU mean=0.870 ms, median=0.804 ms (MoltenVK-emulated Vulkan GPU timestamp (not native GPU perf), 1406 frames, warmup drop 30) [/Users/sindney/Documents/git/furyengine/fury3d/test-artifacts/cluster-lod-perf/vulkan/tracy_sponza_cluster.tracy]

## Exact bench command template

```bash
# from /Users/sindney/Documents/git/furyengine/fury3d/examples, one run per (scene, mode); FURY_CLUSTER_LOD=0 legacy, =1 cluster
FURY_CLUSTER_BENCH=1 FURY_MAX_FPS=0 FURY_CLUSTER_LOD=<0|1> FURY_CAM=<pose> \
  /Users/sindney/Documents/git/furyengine/fury3d/examples/fury Player.lua <scene.bin> --rhi=<backend> --screenshot frame120.png --screenshot-frame 120
```
