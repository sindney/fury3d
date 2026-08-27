# Ocean fragment overhaul: Seascape + Water -> OceanSurface.glsl

Sources:
- A = /tmp/shadertoy/Seascape_Ms2SD1.glsl (TDM "Seascape")
- B = /tmp/shadertoy/Water_MdXyzX.glsl (afl_ext "Water")
- Ours = examples/Resource/Shader/Ocean/OceanSurface.glsl

Engine facts verified while writing this:
- gbuffer_normal is rgba16: rgb = view normal *0.5+0.5, **a = roughness** (GBuffer.glsl:86,112 rt0.a = rough).
- pass_ocean writes gbuffer_depth + hdr_composite + gbuffer_normal (DefferedLightingPBR.json:429-446), so postfx SSAO sees water normals/roughness.
- SSR already gates on that alpha: `if (depth < 1.0 && roughness < 0.95)` and `roughFade = 1.0 - smoothstep(0.0, 0.5, roughness)` (SSR.glsl:77,154).
- SSAO reads only .xyz today (SSAO.glsl:79). The gate is one extra component read + one mix.
- u_ssr_roughness is bound as `GetSsrEnabled() ? GetRoughness() : 1.0f` (PrelightPipeline.cpp:1346) - with SSR OFF, water writes roughness 1.0 and a roughness-gated SSAO would keep darkening it. Must change that bind too.
- Component defaults: absorb (0.02,0.10,0.16), scatter (0.05,0.28,0.36), roughness 0.08 (OceanComponent.h:210-212). Scatter is the "bright turquoise" offender.
- Postfx chain in the ocean project: SSAO then SSR (setup_ocean_base.lua:80-81).

---

## 1. Technique inventory (with exact formulas)

### Wave shape

A (Seascape): folded-sine octave with choppy exponent.
```glsl
const mat2 octave_m = mat2(1.6,1.2,-1.2,1.6);          // rotate+scale per octave (line 23)
float sea_octave(vec2 uv, float choppy) {              // lines 66-72
    uv += noise(uv);                                   // domain warp, breaks lattice
    vec2 wv = 1.0-abs(sin(uv));
    vec2 swv = abs(cos(uv));
    wv = mix(wv,swv,wv);
    return pow(1.0-pow(wv.x * wv.y,0.65),choppy);      // sharp peaks via 1-|sin| and pow4
}
// loop (lines 81-87): two counter-drifting sums per octave (uv+SEA_TIME, uv-SEA_TIME),
uv *= octave_m; freq *= 1.9; amp *= 0.22;              // non-integer ratios kill visible period
choppy = mix(choppy,1.0,0.2);                          // only the big octaves are choppy
```
Constants: SEA_HEIGHT 0.6, SEA_CHOPPY 4.0, SEA_FREQ 0.16.

B (Water): exp(sin) with analytic derivative used as domain drag.
```glsl
vec2 wavedx(...) {                                     // lines 15-20
  float wave = exp(sin(x) - 1.0);                      // peaked crest, flat trough, range [1/e, 1]
  float dx = wave * cos(x);
  return vec2(wave, -dx);
}
position += p * res.y * weight * DRAG_MULT;            // line 39, DRAG_MULT 0.38: later octaves
                                                       // sample a dragged domain = cheap Gerstner chop
float wavePhaseShift = length(position) * 0.1;         // line 24: radial phase decorrelation
weight = mix(weight, 0.0, 0.2); frequency *= 1.18; timeMultiplier *= 1.07;  // lines 46-48
iter += 1232.399963;                                   // line 51: pseudo-random direction step
return sumOfValues / sumOfWeights;                     // line 54: weight-normalized, stable range
```

### Normals vs distance

A: epsilon grows with squared distance (line 175):
```glsl
vec3 n = getNormal(p, dot(dist,dist) * EPSILON_NRM);   // EPSILON_NRM = 0.1 / iResolution.x
```
i.e. the finite-difference footprint tracks pixel footprint - high-frequency detail is filtered before it can alias.

B: straight blend to flat (line 198):
```glsl
N = mix(N, vec3(0.0, 1.0, 0.0), 0.8 * min(1.0, sqrt(dist*0.01) * 1.1));  // full flat ~83 m
```

### Fresnel

A: cheap, capped so the body never turns into a pure mirror (lines 109-110):
```glsl
float fresnel = clamp(1.0 - dot(n, -eye), 0.0, 1.0);
fresnel = min(fresnel * fresnel * fresnel, 0.5);
```
B: proper Schlick (line 201):
```glsl
float fresnel = (0.04 + (1.0-0.04)*(pow(1.0 - max(0.0, dot(-N, ray)), 5.0)));
```

### Sky reflection

A: analytic gradient (lines 60-63):
```glsl
vec3 getSkyColor(vec3 e) {
    e.y = (max(e.y,0.0)*0.8+0.2)*0.8;                  // horizon gets bright, never negative
    return vec3(pow(1.0-e.y,2.0), 1.0-e.y, 0.6+(1.0-e.y)*0.4) * 1.1;
}
```
B: reflected ray clamped above the horizon, then evaluated against atmosphere + sun disk (lines 204-208):
```glsl
vec3 R = normalize(reflect(ray, N));
R.y = abs(R.y);                                        // never reflect below the horizon
vec3 reflection = getAtmosphere(R) + getSun(R);
```

### Sun specular / glitter

A: normalized Phong with **distance-adaptive shininess** (lines 54-57, 120):
```glsl
float nrm = (s + 8.0) / (PI * 8.0);
return pow(max(dot(reflect(e,n),l),0.0),s) * nrm;
...
color += specular(n, l, eye, 600.0 * inversesqrt(dot(dist,dist)));  // s falls off with |dist|
```
Shininess 600/|dist| goes ~200 near the camera to ~6 at 100 m. In perceptual-roughness terms ((2/(s+2))^0.25) that is about 0.24 near, 0.71 far: the highlight widens toward the horizon and becomes the elongated glitter path. This is the single most important trick for the "bright path toward the sun".

B: HDR sun disk evaluated on the perturbed reflected ray; ACES turns it into sparkle (line 144):
```glsl
float getSun(vec3 dir) { return pow(max(0.0, dot(dir, getSunDirection())), 720.0) * 210.0; }
```

### Subsurface / crest glow

A: height-above-mean, attenuated by distance (lines 117-118):
```glsl
float atten = max(1.0 - dot(dist, dist) * 0.001, 0.0);
color += SEA_WATER_COLOR * (p.y - SEA_HEIGHT) * 0.18 * atten;
```
B: height-proportional scatter, deep blue base (line 209):
```glsl
vec3 scattering = vec3(0.0293, 0.0698, 0.1717) * 0.1 * (0.2 + (waterHitPos.y + WATER_DEPTH) / WATER_DEPTH);
```

### Body / scattering colors

A: SEA_BASE = vec3(0.0,0.09,0.18), SEA_WATER_COLOR = vec3(0.8,0.9,0.6)*0.6 (lines 20-21), combined as (line 113):
```glsl
vec3 refracted = SEA_BASE + diffuse(n, l, 80.0) * SEA_WATER_COLOR * 0.12;
// diffuse wrap (lines 51-53): pow(dot(n,l) * 0.4 + 0.6, p)
```
Dark constant base + a *small* wrapped-diffuse green term. The water is mostly base color, not lit scatter - this is why A reads deep blue-green while ours reads bright turquoise.

### Fog / horizon blend

A: wide transition zone right at the horizon line (lines 179-182):
```glsl
mix(getSkyColor(dir), getSeaColor(...), pow(smoothstep(0.0,-0.02,dir.y), 0.2));
```
The pow(...,0.2) keeps the factor below 1 for a band below the horizon: far water is deliberately blended into sky.

B: atmosphere itself does horizon brightening: special_trick = 1.0 / (raydir.y * 1.0 + 0.1) and a mie halo sundt * special_trick * 0.2 with sundt = pow(max(dot(sundir,raydir),0), 8) (lines 119-129).

### Tonemap

A: pow(color, vec3(0.65)) (line 203) - a brightening lift, not a real tonemap.
B: fitted ACES + 1/2.2 (lines 148-163).
Ours: chain already auto-appends ACES; do nothing in the ocean shader.

---

## 2. Mapping onto our mesh + FFT-baked pipeline

| Technique | Where it lives for us | Notes |
|---|---|---|
| Wave shape (sea_octave / wavedx) | Already covered - the bake IS the wave function, a real spectrum with horizontal displacement (better than both toys). | Do NOT add analytic waves in the fragment shader. "Waves too minor" is a shading problem, not displacement: no height-based shading, normals flattened by a constant strength, spec lobe too thin to read. |
| Chop/drag (B line 39) | Baked xyz displacement already includes it. | Nothing to do. |
| Anti-repetition (octave_m, freq*1.9/1.18, phase shift) | Partially covered by two tiles (100 m / 8 m). The 8 m ripple still tiles visibly. | Fragment-level fix: second rotated/offset sample of the ripple normal (3.6). No bake change. |
| Normal-from-distance smoothing | Fragment, 3 lines. No equivalent today; u_normal_strength is a constant. | Biggest tiling/shimmer killer per line of code. |
| Fresnel | Fragment. Schlick 0.02 exists; add A's cap so the body survives grazing. | min(fres, 0.65). |
| Sky reflection | Fragment. Ours is a constant skyTint vec3 - why the horizon reads as a dull band. | 2-color gradient on reflect(-viewDir,n).y + B's R.y = abs(R.y). SSR still adds real reflections on top. |
| Sun spec / glitter | Fragment. GGX D is correct but *0.15 clamps it and roughness 0.08 makes the lobe razor-thin: few pixels hit it, no path. | Distance-adaptive roughness (A's 600/|dist| in GGX form), drop the 0.15. |
| Subsurface crest glow / dark troughs | Fragment, needs displaced height. u_water_level is vertex-only; pass a new varying v_height (free - already computed in the VS). | A's height term + sun-through-crest boost. |
| Scattering color | Data defaults (scatter 0.05,0.28,0.36 = the turquoise) + shader structure ((0.5 + sunCol*0.5) lifts everything). | Darken defaults toward B's vec3(0.0293,0.0698,0.1717)-ish; restructure body lighting A-style. |
| Fog/horizon | AP LUT already better than both toys. | Only need the sky-gradient reflection so grazing water converges to sky color before AP applies. |
| Tonemap | Chain ACES. | Nothing. |

---

## 3. Concrete change list for OceanSurface.glsl (impact per risk, highest first)

All constants in cm. viewDist = length(v_view_pos).

### 3.1 Distance-flattened normals (kills far tiling + shimmer)
After the existing u_normal_strength flatten:
```glsl
float viewDist = length(v_view_pos);
// B line 198 rescaled: full flatten ~500 m instead of their ~83 m
float nflat = min(1.0, sqrt(viewDist * 1.65e-5) * 1.1);
n = normalize(mix(n, vec3(0.0, 1.0, 0.0), 0.8 * nflat));
```
Risk: none.

### 3.2 Distance-adaptive specular (the glitter path)
Replace the fixed-roughness GGX block:
```glsl
// A line 120: shininess falls with distance -> path widens toward the horizon.
// GGX form: roughness rises 0.08 -> 0.35 between 30 m and 800 m.
float rough = mix(u_roughness, 0.35, smoothstep(3000.0, 80000.0, viewDist));
float a2 = rough * rough;
float dden = (ndh * ndh * (a2 - 1.0) + 1.0);
float dggx = a2 / (3.14159 * dden * dden);
float spec = dggx * max(dot(n, viewDir), 0.0);   // drop the * 0.15 clamp
```
Keep spec * sunCol * shadow. If hot at close range, clamp with min(spec, 8.0) rather than scaling - preserves the path shape. Optionally raise the near default 0.08 -> 0.12 so the near path has width too.

### 3.3 Sky-gradient reflection (fixes dull horizon band + part of "fake color")
Replace the flat skyTint:
```glsl
vec3 rdir = reflect(-viewDir, n);
rdir.y = abs(rdir.y);                                   // B line 205
float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, viewDir), 0.0), 5.0);
fres = min(fres, 0.65);                                 // A line 110 cap: body survives grazing
// gradient: bright horizon -> deeper zenith; promote to uniforms later
vec3 skyCol = mix(vec3(0.72, 0.78, 0.85), vec3(0.18, 0.38, 0.62), pow(clamp(rdir.y, 0.0, 1.0), 0.55));
```
Use fres * skyCol * 0.5 (the AP LUT hazes it further at range - correct: water and sky share the same haze).

### 3.4 Height-based trough darkening + crest glow (the reference look)
Vertex: add out float v_height; set v_height = wp.y; right after wp += swell... + ripple...; (before the u_water_level add). Fragment:
```glsl
in float v_height;
...
float hN = clamp(v_height * 0.005, -1.0, 1.0);          // 200 cm peak -> 1.0; tune to bake amp
body *= 1.0 - 0.35 * clamp(-hN, 0.0, 1.0);              // trough darkening
// A line 118 + B line 209, plus sun-through-crest transmission:
float towardSun = pow(max(dot(-viewDir, sunDir), 0.0), 3.0);
vec3 sss = u_scatter_color * clamp(hN, 0.0, 1.0) * (0.15 + 0.85 * towardSun) * 0.6 * shadow;
```
Add sss into col before foam. Distance-gate it with A's atten idea if it bands at range: sss *= max(1.0 - viewDist * viewDist * 1e-10, 0.0); (fades by ~1 km).

### 3.5 Body color restructure (kills "bright turquoise")
Current: body * (0.35 + 0.65*ndl*shadow) * (0.5 + sunCol*0.5) - the second factor lifts everything toward white. A-style (line 113):
```glsl
float dif = pow(ndl * 0.4 + 0.6, 6.0);                  // A's wrap shape, saner exponent than 80
vec3 bodyLit = body * (vec3(0.06, 0.08, 0.10) + dif * shadow * sunCol);
```
plus data-side: scatter default (0.05,0.28,0.36) -> ~(0.03,0.14,0.19), absorb (0.02,0.10,0.16) -> ~(0.008,0.05,0.10). Slightly desaturated, clearly darker. (Defaults live in OceanComponent.h:210-211; per-scene values override.)

### 3.6 Ripple anti-tiling double sample (only if 3.1-3.3 leave visible 8 m period)
```glsl
const mat2 tile_rot = mat2(0.8, 0.6, -0.6, 0.8);        // ~37 deg, unit scale (A's octave_m idea)
vec3 nr2 = sample_nrm(u_nrm_ripple, tile_rot * v_world_pos.xz * 1.37 + vec2(913.0, 571.0), u_ripple_tile);
nr.xz += nr2.xz * 0.6;
```
before the fade/flatten. Non-integer scale 1.37 + rotation makes the two reads interfere; cost is 2 extra array fetches.

### 3.7 Foam distance fade (minor, anti-alias)
```glsl
foam *= 1.0 - smoothstep(20000.0, 60000.0, viewDist);   // 200-600 m
```

Do 3.1 + 3.2 + 3.3 first - ~15 lines total, no new uniforms/varyings, and they address three of the four complaints. 3.4 needs one varying. 3.5 touches component defaults (data, not shader).

---

## 4. Killing SSAO-on-water darkening

The design-note suggestion works and is nearly one line. SSAO.glsl:79 already fetches gbuffer_normal; the alpha of that same texel is the roughness channel (GBuffer.glsl:112, OceanSurface.glsl:270).

```glsl
// before:
vec3 normal = normalize(texture(gbuffer_normal, out_uv).xyz * 2.0 - 1.0);
// after:
vec4 nrm = texture(gbuffer_normal, out_uv);
vec3 normal = normalize(nrm.xyz * 2.0 - 1.0);
...
// after the ao pow():
ao = mix(1.0, ao, smoothstep(0.05, 0.3, nrm.a));   // smooth surfaces (water 0.08) get no AO
```
This mirrors SSR's existing roughFade convention, so both effects agree on "smooth = water". No format changes: gbuffer_normal is rgba16, plenty for a 0.05-0.3 threshold.

**Required companion fix:** PrelightPipeline.cpp:1346 binds u_ssr_roughness = GetSsrEnabled() ? GetRoughness() : 1.0f. With SSR off, water writes 1.0 and the gate stops exempting it - AO comes back on water exactly when SSR is disabled. Change the bind to always write GetRoughness() (SSR's own roughness < 0.95 gate is unaffected by a low value; the 1.0-when-off behavior was only safe because nothing else read the channel). One-line C++ change.

Geometry at default roughness (>= ~0.3 effective) keeps full AO; only genuinely smooth materials are exempt, which is physically defensible (contact shadowing under a mirror-like surface is dominated by reflections, which SSR/our fresnel term already provide).

---

## 5. Ring-LOD seams and horizon handling

Both toys raymarch a heightfield, so there are no LOD seams to copy - but their distance handling transfers directly:

- A's EPSILON_NRM = 0.1 / iResolution.x ties normal detail to pixel footprint: never shade detail smaller than a pixel. Our ring LOD does this geometrically (3 static rings), and 3.1 is the shading-side equivalent. Together they mean the outer rings can keep sampling the same world-space baked arrays - no shimmer, and the coarser mesh won't reveal itself through normals because those are already flattened.
- Seam continuity: displacement and normals are both pure functions of world xz + time (wp = u_world_origin + vertex_position, sampled before the level add), so adjacent rings evaluate the identical field at the shared boundary. The only seam risk left is silhouette interpolation density, which the u_y_offset ring tuck addresses; verify with debug view 3 (wireframe) rather than by shading.
- The per-piece SwellFade/RippleFade binds (PrelightPipeline.cpp:1390-1391) already scale both displacement AND the fragment normal (ns.xz *= u_swell_fade; nr.xz *= u_ripple_fade;) - keep that pairing; fading only one of the two is what produces "flat but sparkly" far rings.
- Horizon: A's pow(smoothstep(0.0,-0.02,dir.y),0.2) says the water->sky transition should be a *band*, not a line. We get the same effect for free once 3.3 lands: at grazing angles rdir.y -> 0, the reflection converges to the horizon color, fresnel is at its (capped) max, and the AP LUT (sqrt(distKm/u_ap_range) slice) piles haze on top - far water and sky end up evaluating the same two ingredients, so the blend reads continuous without any explicit distance-to-horizon term. Choose the horizon default to match the sky's horizon output at the same sun elevation, or the line reappears as a value mismatch rather than a hard edge.
- One thing NOT to borrow: B's R.y = abs(R.y) is right for us, but A's uncapped fresnel^3 is not - without a real sky texture behind the reflection, full-mirror grazing angles expose the approximation. Hence the 0.65 cap in 3.3.
