# Proposal: cloud-quality-veg-prepass

## Why

The physically-based SkyAtmosphere reads as real, but its cloud layer does not: the current deck evaluates coverage with a single sample per ray at the slab midpoint, so clouds render as flat, uniformly-white cutouts with hard edges - no volume, no self-shadowing, no silver lining structure, no multi-deck layering. Reference material (Guerrilla's GDC/SIGGRAPH cloud talks) shows the fix is not "more raymarch steps" but a better density model (3D noise + height gradients + detail erosion) plus proper lighting (Beer-Powder + phase function), which mobile-class titles also achieve from ground view at modest sample counts. Separately, alpha-tested vegetation (kraut trees, grass) pays full gbuffer shading for every overdrawn layer of foliage; a vegetation depth prepass removes most of that wasted shading.

## What Changes

- Replace the single-sample 2D cloud deck with a ray-marched volumetric cloud slab (ground-view only; no fly-through) using a Horizon-style density model: weather map (coverage/type) x height gradient x base Perlin-Worley 3D noise, eroded by detail Worley noise, lit with Beer-Powder + dual-lobe Henyey-Greenstein and cone-stepped sun samples.
- Generate the required noise textures (tileable Perlin-Worley 3D, Worley 3D, 2D weather map) at load time via fragment passes into the existing `TEXTURE_3D`/2D render-target infra (GL 3.3 baseline; no compute), with an on-disk cache under the content dir so steady-state startup is one file read.
- Render clouds at half resolution into the existing cloud target with a frustum-aware step budget (distance-grown steps, empty-space skip, early exit), composited over the sky as today; add a temporal checkerboard option if profiling shows it is needed.
- Add a second, cheap high-altitude 2D cirrus layer above the volumetric deck so scenes read as multi-layered (low plump cumulus + high thin cirrus) without a second raymarch.
- Extend the `SkyAtmosphere` component cloud settings (type gradients, noise scales, light samples, step budget, cirrus settings) with scene round-trip and inspector UI; keep old fields loading (mapped onto the new model where sensible, defaults otherwise).
- Add a vegetation pre-z pass: foliage materials (MASK + wind-tagged, opt-in per material) draw alpha-tested depth-only before the gbuffer pass; the gbuffer pass then tests those draws with `EQUAL`+no depth write so only the front-most foliage layer is shaded.
- Instrument the new passes with the existing Tracy zones and add `FURY_`-style debug/verify hooks for headless screenshots (cloud compare views, pre-z on/off).

## Capabilities

### New Capabilities
- `volumetric-clouds`: the ray-marched cloud slab - noise generation/caching, density model, lighting model, march schedule, half-res composite, cirrus layer, quality presets, and perf guards for ground-view rendering.

### Modified Capabilities
- `sky-atmosphere`: the "2D cloud layer" requirement is replaced by the volumetric cloud model (settings schema changes, `pass_sky` composites cloud target from the new renderer); sky/sun/moon/aerial-perspective behavior otherwise unchanged.
- `vegetation-rendering`: adds the vegetation pre-z pass requirement (material opt-in flag, pass ordering, `EQUAL` depth reuse, interaction with wind/billboards/shadow alpha-test variants).

## Impact

- **Code**: `SkyAtmosphere` component (fields, Load/Save/Clone, inspector), sky/cloud shaders (`examples/Resource/Shader*` sky pass + new cloud shaders), pipeline JSON (cloud pass wiring), gbuffer/depth shader variant selection (prez variants), render queue/pass sequencing (veg prez before gbuffer), Tracy zones, Lua/Lua-test exposure for verification.
- **Assets**: generated noise textures cached on disk; no new hand-authored art required.
- **Performance**: clouds target <= ~1.5 ms at 1080p half-res on desktop GL 3.3-class GPUs via step budget + half-res; vegetation prez trades one cheap depth-only draw set for eliminated gbuffer shading overdraw (expected net win on foliage-dense scenes like ocean island; verified via Tracy GPU timings).
- **Compatibility**: GL 3.3 baseline preserved (no compute); scenes with old cloud fields load with sensible defaults; clouds disabled = zero cost as today.
