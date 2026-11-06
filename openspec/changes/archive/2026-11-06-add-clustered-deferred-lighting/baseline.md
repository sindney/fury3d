# Clustered-off baseline

Source revision: `2669e947c5a79839da6bccd77099518f3495ac69` (`master`).

The normal `build/` configuration compiled successfully before capture. The aggregate render lane then passed all 19 scenarios with no skipped or failed tests:

```sh
python3 -I tests/run.py run --lane render --artifacts /tmp/fury-render-2669e94
```

The checked-in reference images used by the lane are:

| Scenario | OpenGL | Metal | Coverage |
| --- | --- | --- | --- |
| `feature_hdr_off` | `tests/baselines/opengl/feature_hdr_off.png` | `tests/baselines/metal/feature_hdr_off.png` | LDR feature scene |
| `feature_hdr_on` | `tests/baselines/opengl/feature_hdr_on.png` | `tests/baselines/metal/feature_hdr_on.png` | HDR feature scene |
| `feature_lambert` | `tests/baselines/opengl/feature_lambert.png` | `tests/baselines/metal/feature_lambert.png` | True Lambert pipeline and mixed local lights |
| `clustered_materials_hdr` | `tests/baselines/opengl/clustered_materials_hdr.png` | `tests/baselines/metal/clustered_materials_hdr.png` | HDR PBR metallic/roughness grid, emissive input, shadowed/unshadowed point/spot lights |
| `island` | `tests/baselines/opengl/island.png` | `tests/baselines/metal/island.png` | HDR sky, ocean, SSAO, and SSR |

All six original image comparisons passed. Capture metadata and tolerances are stored beside each image in the corresponding `.json` file. The run summary is `/tmp/fury-render-2669e94/summary.json`; per-scenario logs and fresh captures are under `/tmp/fury-render-2669e94/`.

The added Lambert/material scenarios passed repeat captures without baseline updates. OpenGL comparisons were pixel-identical. Metal `clustered_materials_hdr` was pixel-identical; Metal `feature_lambert` had mean absolute difference 0.0702, maximum 40, and 0.001953 fraction above 8, within its approved tolerances. Island passed pixel-identical on both backends with semantic assertions proving SSAO and SSR are enabled. New-scenario capture and verification logs are under `/tmp/fury-cdl-baseline/` and `/tmp/fury-cdl-verify/`.

The material fixture verifies the emissive material input. Deferred emissive appearance currently comes through ambient color/factor because the existing G-buffer has no emissive channel. The fixture is textureless and does not claim normal-map coverage.

The complete writer/reader inventory is in `gbuffer-inventory.md`.

## Compaction and clustered A/B results

The compact `rgba8` octahedral-normal layout preserves the reference images within the measured quantization envelope. Mean differences are below 1.0 level. Sparse max differences reach 64/96/128 levels at specular highlights and foliage edges, so `tests/render_scenarios.json` records scenario-specific max and above-8 tolerances without changing the approved images. The full maintained render lane passes on OpenGL and Metal.

Active clustered/fallback comparisons pass with effective-mode assertions on Metal and Vulkan for the material grid, HDR feature scene, true Lambert scene, and tangent-space normal-map scene. Metal also passes at 960x540 and 1600x900, and both backends pass with 0.5/5000 near/far planes and camera drift. OpenGL requested clustering reports `clustered_effective=0`, produces the fallback image, and performs no assignment work.

The 134-light stress case saturates at 128 lights per cluster and reports 7,370 dropped assignments. Switching back to fallback is valid. `shaderc --all` passes 432 permutations. `tests/pak_pipeline.sh` passes 1,293 artifacts and packaged Metal/Vulkan compute loading with no runtime GLSL compilation.
