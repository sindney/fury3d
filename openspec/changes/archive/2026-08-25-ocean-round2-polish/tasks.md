# Tasks

Conventions (carried): ASCII-only source (`tests/check_engine_ascii.py`); LF
endings; every sampler bound every draw; headless via `./fury exec` (from
examples/, tests at ../tests/lua/); visuals via the `fury` PLAYER binary with
`--screenshot-frame 30` (furye wraps renders in editor chrome; frame ~2
captures the pre-scene shell); solo Bash call per shot; scene builders
re-runnable, never write their source; DO NOT archive until the user's
interactive pass confirms.

## 1. Ring geometry continuity (user: "mesh is not connected by skirts")

- [x] 1.1 UpdateCameraFollow: snap every piece origin to the CENTER cell
  size (finest), not its own - all ring cells are multiples of it, so
  shared edges coincide exactly (kills the grazing slits; wave field is
  world-pure so nothing swims).
- [x] 1.2 Fade ranges: ripple [R0/2, R0] (fully out AT the ring-1 boundary
  so the 8 m band never straddles a T-junction); swell [lastRingInner,
  lastRingOuter] with floor 0 (flat skirt; the 0.3 floor was subpixel at
  6 km and only fed the far glint dashes).
- [x] 1.3 Skirt inner radius 0.95x the last ring outer half-extent (tucks
  under the square everywhere, incl. edge midpoints).
- [x] 1.4 Verify: grazing wireframe + shaded shots (low camera, ~3-6 m)
  show no slits/gaps between pieces; wireframe still shows ring colors;
  headless suite green.

## 2. Foam + repetition

- [x] 2.1 Crest foam: threshold smoothstep(0.35,0.9) -> (0.5,1.0); default
  foamAmount 1.0 -> 0.6.
- [x] 2.2 Anti-tile foam: modulate with the swell nrm array's alpha (bake
  stores a foam copy there) at rot37(uv*3.73)+0.37 offset:
  `foam *= 0.55 + 0.9 * foam2`.
- [x] 2.3 Anti-tile swell normals: second fetch at rot37(uv*0.62)+0.37,
  0.35 weight, before the band fade.
- [x] 2.4 Verify: island high oblique + lake shots show broken-up foam and
  no obvious 100 m repeat; foam mask debug view consistent.

## 3. Darker plate

- [x] 3.1 Constants: ambient -> (0.015,0.03,0.045); wrapped diffuse sunCol
  -> sunCol*0.45; fresnel cap 0.65 -> 0.55; sky reflection weight 0.5 ->
  0.4; scatter default -> (0.025,0.11,0.15); absorb -> (0.006,0.04,0.08).
- [x] 3.2 Rebuild the 4 demo scenes (base first) + verify noon/golden-hour
  infinite shots vs screenshots/plate reference (deep dark body, glitter
  path, grazing reflection).

## 4. Droplist UX

- [x] 4.1 ImGuiComboFlags_HeightLargest on the Debug combo (no scrollbar
  when there is room).
- [x] 4.2 ImGuiSelectableFlags_DontClosePopups on every row incl. the Ocean
  + PostProcess submenu items (popup closes only on outside click).

## 5. Plank spin (buoyancy angular drag stability)

- [x] 5.1 BuoyancyComponent::TickBuoyancy: read the real local inverse
  inertia via BodyLockRead; apply angular drag per body-local axis with the
  per-tick factor clamped to 0.9 (stable for any shape; ~unchanged where
  already stable).
- [x] 5.2 Headless probe: drop the plank flat-water, step 20 s, assert
  |omega| settles < 0.5 rad/s (add to tests/lua/buoyancy_smoke.lua or a new
  plank probe); buoyancy_smoke stays green (cube leveling, settle heights).

## 6. OceanWaves asset flow + inspector row

- [x] 6.1 MergeInto: transfer OceanWaves entities (Heightmap stranding
  precedent) - Content Browser tile + inspector picker populate.
- [x] 6.2 Inspector wave-asset row: Change/->/x + "OceanWaves: <name>" +
  stats line (bands, frames x loop, tiles); drop the raw path InputText.
- [x] 6.3 Verify in the editor: picker lists the asset, content browser OW
  tile appears, -> jumps, x clears.

## 7. Wave-data previews

- [x] 7.1 gen_ocean_assets.py: stdlib PNG writer (zlib+struct); per band
  write <band>_nrm_preview.png (frame 0) + <band>_foam_preview.png (max
  projection, grayscale) next to the payloads; note in the sidecar.
- [x] 7.2 Rebake examples/Projects/ocean/Ocean (hashes change: sidecar
  gains preview filenames; payload bytes unchanged - verify sha of the
  .f16/.u8 matches the pre-change bake).
- [x] 7.3 Content Browser OW tile thumbnail: the preview PNG when present
  (lazy Texture load, cached per path, GL-guarded), else the placeholder.

## 8. Hygiene + verify (USER GATE)

- [x] 8.1 check_engine_ascii + LF + full tests/lua suite both compute modes.
- [x] 8.2 docs/OCEAN.md (snapping, fade ranges, foam/normal anti-tile,
  plate constants, droplist behavior, drag clamp, previews, asset flow).
- [ ] 8.3 Final screenshot set. USER interactive verify -> only then
  sync + archive.

## 9. User review round 2 (2026-08-21)

- [x] 9.1 Inspector: remove the ocean Debug section (debug views live in
  the viewport droplist now).
- [x] 9.2 Foam gated by absolute strength, not the per-bake normalized
  peak: calm seas (maxFoam ~0.15) show NO crest foam; threshold the raw
  bake value smoothstep(0.25, 0.6) - foam appears only in storm-strength
  bakes. Shore foam unchanged. (Root cause: u_foam_max normalization
  rescaled calm water to full range.)
- [x] 9.3 Storm demo: bake OceanStorm (wind 2200 cm/s, choppiness 1.5,
  swell tile 150 m) into examples/Projects/ocean/OceanStorm/ (PROJECT
  sample - the engine root gets only the calm default) +
  setup_ocean_storm.lua -> ocean_storm.bin; foam visible there.
- [x] 9.4 Default ocean assets moved to the engine resource root
  (examples/Resource/Ocean/, referenced "Engine/Ocean/ocean.json" like the
  sky textures); component default path updated; demo scenes rebuilt.
- [x] 9.5 Ring-count-scaled fog: fog start follows the last ring's outer
  radius (was fixed 1 km - at ringCount 5 the 1-6 km skirt fog band showed
  as a hard band against unfogged 1 km ring water).
- [x] 9.6 Far-field fetch skip: VS skips both band displacement fetches
  when swellFade+rippleFade ~ 0 (the skirt stops animating entirely - flat
  fogged sheet, saves the textureLod work); FS skips the normal/foam pack
  fetches likewise.
- [x] 9.7 Lazy wave re-resolve: EnsureWaves() ran only in OnAttaching, so
  the picker's SetWaveAssetPath left the ocean flat until reload (the
  user's "inspector for changing it still unmodified" report). GetWaves()
  now lazily re-resolves when dirty.
- [x] 9.8 Verify: 5-ring scene shot shows smooth fog to the horizon; storm
  scene shot shows crest foam; calm scenes show shore foam only; inspector
  wave-asset Change applies live.
- [x] 9.9 THE SKIRT NEVER RENDERED (root cause of the "far ring no fog"
  report): BuildSkirt's triangles wound DOWNWARD and pass_ocean culls
  backfaces - the far "water" in every earlier shot was the sky's
  below-horizon row showing through. Winding flipped; the horizon
  converges. Also: fog converges to the sky-view LUT one row above the
  geometric horizon (the boundary row bilinearly mixes the ground-bounce
  row - a beige band at noon).

## 10. Third cascade (chop band) + white-out fix

Root cause of the noon down-sun white-out AND the "waves too gentle" note:
the bake's fetch-relative high-frequency cutoff sat inside the small
tiles' passband - the 8 m "ripple" band was a single-mode PLANE WAVE (the
dash repetition), a 3 m tile degenerated entirely, and the normals were
too smooth to break the noon half-vector lobe.

- [x] 10.1 Bake tool: the spectrum cutoff is now tile-relative (2 texels),
  restoring full-band spectra; the wrap self-check now compares against
  the field's own max adjacency (3x-mean false-positives on full spectra).
- [x] 10.2 Third cascade "chop" band (3 m tile, 2 cm amp): normals + foam
  only (no vertex displacement - buoyancy unaffected). Bake flags
  --chop-resolution/--chop-tile-cm/--chop-amplitude-cm. Engine + storm
  assets rebaked (3 bands; mini fixture stays 2-band, both load).
- [x] 10.3 Engine: OceanWaves sorts bands tile-ascending at load (2-band
  [ripple, swell], 3-band [chop, ripple, swell]); DrawOcean binds roles by
  that order + u_chop_valid; the shader blends chop normals (4x weight -
  the raw 2.5 deg slopes can't scatter the noon lobe) with its own
  rotated anti-tile fetch, plus a 0.3 foam contribution. GPU generation
  stays 2-band (documented).
- [x] 10.4 Sun-elevation-scaled specular cap: mix(1.2, 0.35, smoothstep
  (0.3, 0.9, sunDir.y)) - a high sun's near-vertical half-vector sits the
  lobe at any cap over the whole down-sun field; the cap + chop glints
  break it up. Low sun keeps the bright glitter path.
- [x] 10.5 Foam re-thresholded on the new distributions: smoothstep(0.40,
  0.60) on the absolute summed foam (calm p99 ~0.30 -> zero whitecaps;
  storm p90 ~0.42 -> crest streaks); gates softened to 0.75+0.5g /
  0.8+0.4g (wide gates read as blobs, not streaks).

## 11. User review round 3 (2026-08-24)

- [x] 11.1 Remaining ring dashes: most were the demo props' buoyancy debug
  crosses at range (intentional content, not artifacts); the real slivers
  are T-junction cracks at ring boundaries - the -2 cm/ring tuck RESTORED
  (removing it opened grazing slivers; the full-spectrum swell's short
  modes make the mismatch bigger than the old smooth spectrum).
- [x] 11.2 Storm flicker, ROOT-CAUSED via the new series capture: the
  specular field MORPHED instead of flowing - temporal aliasing. The 32
  frame/12 s loop samples at 2.67 fps, but the detail bands' short modes
  oscillate at 1.3-4+ Hz, so the frame lerp crossfades unrelated phases.
  Fix: bake-side temporal Nyquist rolloff (omega_max = pi*frames/loop*0.7)
  + the chop band plays at 1/4 speed in-shader (sample_pack_ts). Per-frame
  delta metric: 2.61 -> 1.22, max spike 3.22 -> 1.77. (The intermediate
  soft-knee spec cap didn't address this and was REVERTED to the plain
  elevation-scaled min().)
- [x] 11.3 Play-mode far ocean culled: Player.lua's free-fly camera far
  defaulted to 5000 cm (50 m) - now 500000 (matches the editor default);
  the AABB-based floor likewise. Verified with a no-FURY_CAM run (the
  auto-framed free-fly path).
- [x] 11.4 LUT sample guard: safe_skyview() falls back to the analytic
  gradient on NaN/hot texels (>64) in the grazing rows.

## 12. Temporal capture tool

- [x] 12.1 `--screenshot-series "path,N,interval"` launcher flag: captures
  N back-buffer frames every `interval` frames (start at --screenshot-frame)
  and writes ONE contact-sheet atlas PNG. Engine-side (Engine.cpp readback
  + atlas + stb write); LuaBindings.h / Engine.h option plumbing; docs
  updated (CLI.md). Built to diagnose the flicker above - keep for all
  future temporal debugging.

## 13. Ring LOD stitching (user review round 4)

- [x] 13.1 BuildStitchRing: transition band between every LOD pair
  (center/rings), 3 triangles per coarse segment + 2 per corner fan -
  the pieces previously just abutted, so the fine edge's midpoint
  vertices displaced past the coarse edge's chord and grazing views
  showed slivers (the user's "4 verts to 2 verts, connect with 3
  triangles" diagnosis, exactly). Every stitch vertex sits on a
  neighbor's edge: watertight. Ring frames shift out one coarse cell to
  make room; the ring tucks are GONE (watertight now; the skirt keeps
  its -2 cm overlap tuck). +792 verts total (3 stitch rings).
- [x] 13.2 Verify: grazing shaded + 45 deg diagonal (the old V-line
  corners) + wireframe show connected bands with no slivers; suite
  green; ocean probe vertex count 14546 -> 15338.

## 14. Foam realism + night lighting (user review round 5)

- [x] 14.1 Night lighting: SkyAtmosphere moon direction + enabled +
  intensity bound into the ocean shader (u_moon_dir, u_moon_intensity).
  Wrapped moon diffuse (cool 0.85,0.92,1.05 tint, 0.45 weight) + a
  capped moon glint path (0.3 product cap) keep the water from going
  pitch black at night. Verified at FURY_TOD=0.5.
- [x] 14.2 Crest foam: dropped the swell-scale foam gate (it painted
  373 m blobs - the "fake large" complaint), kept the mild ripple
  gate (0.85+0.3g) for crest-to-crest de-repetition. The crest shape
  now comes straight from the bake's Jacobian channel; the threshold
  (0.40, 0.60) keeps only the strongest crests. Crest foam fades
  150-350 m (subpixel whitecaps only shimmer at range). Shore foam
  unchanged.
- [x] 14.3 Lit foam: foam is now shaded (white * (ambient + sun wrap*0.9
  + moon wrap*0.5)) so foam doesn't glow white in the dark. Verified
  storm at midnight: faint moonlit sheen, no glow.
- [x] 14.4 Final screenshot set retaken (r2_noon, r2_golden,
  r2_horizon, r2_island_high, r2_storm).
