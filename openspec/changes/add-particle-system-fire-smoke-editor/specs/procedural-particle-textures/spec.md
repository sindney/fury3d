# procedural-particle-textures

## Purpose

Two 256×256 RGBA PNG textures for the fire + smoke particle emitters,
committed as **static assets** under `examples/Projects/outdoor/` (the
working dir of the scene that uses them — texture paths resolve against it).

> **Scope change (user directive 2026-07-31):** the original capability
> included the committed generator script (`engine/Tools/
> gen_particle_textures.py`). The user ruled texture tooling temporary —
> don't commit it — so this capability is reduced to the static texture
> assets. Re-authoring recipe (for a future throwaway script): fire =
> radial white→yellow→orange→red gradient, 4-octave value-noise FBM
> coordinate warp; smoke = 0.55 gray premultiplied into a radial alpha
> falloff with FBM edge erosion. Final texture (2026-08-03 art pass):
> domain-warped FBM density — two 4-octave value-noise fields warp the
> sample point of a 5-octave density field, threshold carves wispy holes,
> radial smoothstep mask cleans the quad edge.

## ADDED Requirements

### Requirement: The fire texture SHALL be a 256×256 warm-gradient RGBA PNG

`examples/Projects/outdoor/fire.png` SHALL be a 256×256 8-bit RGBA PNG: a
radial warm gradient (white center → yellow → orange → red → alpha 0),
non-tiling, loaded through the existing `Texture::CreateFromImage` path
(sRGB at runtime).

#### Scenario: Fire PNG loads as a sRGB texture

- **WHEN** the scene loads `Material_Fire` with the `DIFFUSE_TEXTURE` slot
  pointing to `fire.png` (relative to the scene's working dir)
- **THEN** the existing `Texture::CreateFromImage` path uploads the PNG to
  GPU
- **AND** the texture is sampled as sRGB at runtime

### Requirement: The smoke texture SHALL be a 256×256 premultiplied-alpha RGBA PNG

`examples/Projects/outdoor/smoke.png` SHALL be a 256×256 8-bit RGBA PNG,
premultiplied alpha: cool gray (~0.55) RGB, density built from
domain-warped FBM (billowy cauliflower structure with wispy holes — a
smooth radial blob reads as fake) faded to 0 at the quad edge by a radial
mask, non-tiling, non-sRGB (loaded as linear data).

#### Scenario: Smoke PNG uses premultiplied alpha

- **WHEN** any pixel of the smoke PNG is sampled
- **THEN** its RGB channels equal `0.55 * alpha` (premultiplied form,
  ±1 LSB rounding)
- **AND** every pixel on the quad's outer edge rows/columns has alpha `0.0`
  (no hard texture border on the billboard)

#### Scenario: Smoke density has interior structure

- **WHEN** the smoke PNG's alpha channel is sampled across the disc
- **THEN** it contains both fully transparent interior holes and dense
  billows (domain-warped FBM threshold structure) — a monotone radial
  falloff (smooth blob) does NOT satisfy this requirement
