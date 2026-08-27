// Ocean surface (change: add-fft-ocean; polish: polish-ocean-visuals-ux).
// Replays baked or GPU-generated looping FFT bands: vertex shader displaces
// a grid (finite or ring-LOD pieces) as a pure function of world xz + u_time,
// with camera-radial band fades (u_fade_ranges) applied to displacement and
// band normals together via v_band_fade. Fragment shades: distance-flattened
// normals, distance-adaptive sun GGX glitter, sky-view-LUT reflection,
// trough/crest height tint, distance fog converging on the sky horizon,
// crest foam from the baked Jacobian channel and shore foam from the
// pre-pass scene depth copy. Writes hdr_composite (color0, alpha blend) and
// gbuffer_normal (color1, replace) so SSR treats water as a smooth reflector
// and SSAO gates off it via the roughness alpha.

#ifdef VERTEX_SHADER

in vec3 vertex_position;

uniform mat4 invert_view_matrix;
uniform mat4 projection_matrix;

uniform vec3 u_world_origin;   // node world pos (finite) or snapped ring origin
uniform float u_y_offset;      // ring tuck (cm)
uniform float u_water_level;   // world-space surface height (node y + level)

uniform float u_time;
uniform float u_loop_seconds;
uniform float u_frames;

uniform sampler2DArray u_disp_swell;
uniform sampler2DArray u_disp_ripple;
uniform float u_swell_tile;
uniform float u_ripple_tile;
// per-ocean fade multipliers (default 1); the actual fade shape is the
// camera-radial smoothstep below (change: polish-ocean-visuals-ux)
uniform float u_swell_fade;
uniform float u_ripple_fade;
uniform vec2 u_cam_xz;        // camera world xz (radial fade center)
uniform vec4 u_fade_ranges;   // rippleStart, rippleEnd, swellStart, swellEnd (cm)
uniform int u_waves_valid;

out vec3 v_world_pos;
out vec3 v_view_pos;
out vec4 v_band_fade;    // [swell, ripple disp, ripple nrm, chop nrm]
out float v_height;      // displaced height, pre water level (trough/crest shading)

vec3 sample_disp(sampler2DArray arr, vec2 xz, float tile)
{
	vec2 uv = xz / tile;
	float ft = u_time / u_loop_seconds * u_frames;
	float f0 = floor(ft);
	float tl = ft - f0;
	float i0 = mod(f0, u_frames);
	float i1 = mod(f0 + 1.0, u_frames);
	vec4 a = textureLod(arr, vec3(uv, i0), 0.0);
	vec4 b = textureLod(arr, vec3(uv, i1), 0.0);
	return mix(a, b, tl).xyz;
}

void main()
{
	vec3 wp = u_world_origin + vertex_position;

	// Camera-radial band fades: smooth functions of world xz distance, so
	// ring-LOD boundaries never step the displacement or the band normals
	// (per-piece constant fades read as V-shaped seams at the square ring
	// corners). Both bands fade to 0 at their ranges' ends: ripple
	// displacement is done at the ring-1 boundary (never straddles a
	// T-junction) and swell is done at the skirt (its 250 m fan sampling
	// only aliases the 100 m band). Ripple NORMALS outlive the displacement
	// 4x (8 cm chop is subpixel long before 8 m wavelet normals stop being
	// useful detail - without this the mid-field goes glassy).
	float camDist = length(wp.xz - u_cam_xz);
	float rippleFade = (1.0 - smoothstep(u_fade_ranges.x, u_fade_ranges.y, camDist)) * u_ripple_fade;
	float rippleNrmFade = (1.0 - smoothstep(u_fade_ranges.y, u_fade_ranges.y * 4.0, camDist)) * u_ripple_fade;
	float swellFade = (1.0 - smoothstep(u_fade_ranges.z, u_fade_ranges.w, camDist)) * u_swell_fade;
	// chop (3 m) normal fade: subpixel by ~50 m, so it dies early; it never
	// displaces vertices, so no T-junction rule applies
	float chopFade = (1.0 - smoothstep(u_fade_ranges.x, u_fade_ranges.x * 3.0, camDist)) * u_ripple_fade;
	v_band_fade = vec4(swellFade, rippleFade, rippleNrmFade, chopFade);
	v_height = 0.0;

	if (u_waves_valid != 0)
	{
		vec2 xz0 = wp.xz; // displacement is baked against the undisplaced grid
		vec3 swell = sample_disp(u_disp_swell, xz0, u_swell_tile);
		vec3 ripple = sample_disp(u_disp_ripple, xz0, u_ripple_tile);
		vec3 disp = swell * swellFade + ripple * rippleFade;
		wp += disp;
		v_height = disp.y;
	}

	wp.y += u_water_level + u_y_offset;

	v_world_pos = wp;
	vec4 vp = invert_view_matrix * vec4(wp, 1.0);
	v_view_pos = vp.xyz;
	gl_Position = projection_matrix * vp;
}

#endif

#ifdef FRAGMENT_SHADER

layout(location = 0) out vec4 rt0; // hdr_composite
layout(location = 1) out vec4 rt1; // gbuffer_normal (view normal, roughness)

in vec3 v_world_pos;
in vec3 v_view_pos;
in vec4 v_band_fade;
in float v_height;

uniform mat4 invert_view_matrix;
uniform vec3 camera_pos;
uniform float camera_near;
uniform float camera_far;
uniform vec2 u_rt_size;

uniform float u_time;
uniform float u_loop_seconds;
uniform float u_frames;
uniform sampler2DArray u_nrm_swell;
uniform sampler2DArray u_nrm_ripple;
uniform sampler2DArray u_nrm_chop;   // third cascade: normals + foam only
uniform float u_swell_tile;
uniform float u_ripple_tile;
uniform float u_chop_tile;
uniform int u_chop_valid;
uniform int u_waves_valid;

uniform vec3 u_absorb_color;
uniform vec3 u_scatter_color;
uniform float u_roughness;
uniform float u_normal_strength;
uniform float u_foam_amount;
uniform float u_shore_foam_depth;
uniform float u_wind_speed;  // cm/s; modulates crest coverage
uniform float u_ssr_roughness; // always the component roughness (SSAO gate reads it)

uniform float u_fog_start;     // cm; distance fog fades in from ~1 km
uniform float u_fog_end;       // cm; fully fogged at the skirt radius

uniform sampler2D u_scene_depth; // pre-pass copy (never the attachment)

uniform vec3 light_dir;      // BindLight: world, points DOWN the light
uniform vec3 light_color;    // color / pi
uniform float light_intensity;
uniform int u_light_valid;

uniform vec3 u_moon_dir;     // toward the moon (sky-driven, else down)
uniform float u_moon_intensity;

uniform sampler2DArray shadow_buffer_csm;
uniform mat4 shadow_matrix_csm[4];
uniform vec4 shadow_far;
uniform int u_shadow_type;   // 0 none, 3 CSM

uniform sampler3D u_ap_volume;
uniform float u_ap_range;
uniform int u_atmosphere_enabled;

// sky-view LUT (per-frame, from SkyAtmosphere): true sky color for the
// reflection gradient and the fog convergence target. Mapping mirrors
// atm_skyview_uv in AtmosphereCommon.glsl, inlined to keep this shader
// self-contained; bound only when u_atmosphere_enabled.
uniform sampler2D u_skyview_lut;
uniform float u_bottom_radius;   // planet radius, km
uniform float u_view_height;     // camera altitude, km

uniform int u_debug_view;    // 0 off, 1 foam, 2 displacement, 3 wireframe
uniform float u_disp_debug_scale;
uniform vec3 u_debug_color;  // per-piece wireframe color (debug 3)

vec4 sample_pack(sampler2DArray arr, vec2 xz, float tile)
{
	vec2 uv = xz / tile;
	float ft = u_time / u_loop_seconds * u_frames;
	float f0 = floor(ft);
	float tl = ft - f0;
	float i0 = mod(f0, u_frames);
	float i1 = mod(f0 + 1.0, u_frames);
	vec4 a = texture(arr, vec3(uv, i0));
	vec4 b = texture(arr, vec3(uv, i1));
	return mix(a, b, tl); // xyz = normal *0.5+0.5, a = foam copy
}

// time-scaled variant: the chop band's short modes move too fast for the
// baked frame rate even after the bake-side Nyquist rolloff, so its
// detail plays at 1/4 speed - reads as flowing texture instead of
// morphing (temporal aliasing)
vec4 sample_pack_ts(sampler2DArray arr, vec2 xz, float tile, float tScale)
{
	vec2 uv = xz / tile;
	float ft = u_time * tScale / u_loop_seconds * u_frames;
	float f0 = floor(ft);
	float tl = ft - f0;
	float i0 = mod(f0, u_frames);
	float i1 = mod(f0 + 1.0, u_frames);
	vec4 a = texture(arr, vec3(uv, i0));
	vec4 b = texture(arr, vec3(uv, i1));
	return mix(a, b, tl);
}

// anti-tiling: second fetches rotate world xz by 37 deg and use incommensurate
// tile scales, so the blend's repeat period is many times the 100 m band tile
const mat2 kRot37 = mat2(0.7986, 0.6018, -0.6018, 0.7986);

// sky-view LUT uv: x = sun-relative azimuth folded to [0,1], y = zenith
// mu with the quadratic horizon warp (0.5 = horizon). dir world-space.
vec2 skyview_uv(vec3 dir, vec3 sunDir)
{
	float r = u_bottom_radius + u_view_height;
	float muH = -sqrt(max(0.0, r * r - u_bottom_radius * u_bottom_radius)) / r;
	float mu = clamp(dir.y, -1.0, 1.0);
	float v;
	if (mu >= muH)
	{
		float t = clamp((mu - muH) / (1.0 - muH), 0.0, 1.0);
		v = 0.5 + 0.5 * t * t;
	}
	else
	{
		float t = clamp((mu + 1.0) / (muH + 1.0), 0.0, 1.0);
		v = 0.5 * t * t;
	}
	vec3 sunH = sunDir - vec3(0.0, sunDir.y, 0.0);
	vec3 dirH = dir - vec3(0.0, dir.y, 0.0);
	float cosAzim = (length(sunH) > 1e-4 && length(dirH) > 1e-4)
		? clamp(dot(normalize(sunH), normalize(dirH)), -1.0, 1.0) : 1.0;
	return vec2(acos(cosAzim) / 3.14159265, v);
}

float csm_shadow(vec3 worldPos)
{
	if (u_shadow_type != 3)
		return 1.0;
	vec4 vp = invert_view_matrix * vec4(worldPos, 1.0);
	float vz = vp.z;
	int index = 3;
	if (vz > shadow_far.x) index = 0;
	else if (vz > shadow_far.y) index = 1;
	else if (vz > shadow_far.z) index = 2;
	vec4 sc = shadow_matrix_csm[index] * vec4(worldPos, 1.0);
	sc = sc / sc.w;
	if (sc.z > 1.0)
		return 1.0;
	float bias = 0.0005;
	return sc.z - bias < texture(shadow_buffer_csm, vec3(sc.xy, float(index))).x ? 1.0 : 0.0;
}

void main()
{
	// The gbuffer stores LINEAR view depth (gl_FragDepth = -viewZ / far);
	// every pass that shares gbuffer_depth must write it (Forward/Particle
	// precedent). Written before any early return: undefined otherwise.
	gl_FragDepth = -v_view_pos.z / camera_far;

	if (u_debug_view == 2) // displacement heatmap: R = crest, B = trough (full at 80 cm)
	{
		rt0 = vec4(clamp(v_height * 0.0125, 0.0, 1.0), 0.0,
			clamp(-v_height * 0.0125, 0.0, 1.0), 1.0);
		rt1 = vec4(0.5, 1.0, 0.5, 1.0);
		return;
	}

	float viewDist = length(v_view_pos);

	// detail normal: band normals blended (camera-radial fades from the VS,
	// so displacement and normals fade together), then flattened by strength
	vec3 n = vec3(0.0, 1.0, 0.0);
	vec4 packS = vec4(0.5, 1.0, 0.5, 0.0);
	vec4 packR = vec4(0.5, 1.0, 0.5, 0.0);
	float foamGateRipple = 1.0;
	float foamChop = 0.0;
	// skip the band fetches entirely where both fades are ~0 (far skirt:
	// flat + fogged; normals would be up and foam zero anyway)
	if (u_waves_valid != 0 && v_band_fade.x + v_band_fade.z > 0.0001)
	{
		packS = sample_pack(u_nrm_swell, v_world_pos.xz, u_swell_tile);
		// anti-tile: blend a rotated 0.62x-tile copy so the 100 m swell
		// period stops reading from elevated views
		vec3 ns2 = sample_pack(u_nrm_swell,
			kRot37 * v_world_pos.xz + vec2(51700.0, 29300.0), u_swell_tile * 0.62).xyz * 2.0 - 1.0;
		packR = sample_pack(u_nrm_ripple, v_world_pos.xz, u_ripple_tile);
		vec3 ns = packS.xyz * 2.0 - 1.0;
		vec3 nr = packR.xyz * 2.0 - 1.0;
		ns.xz = mix(ns.xz, ns2.xz, 0.35);
		ns.xz *= v_band_fade.x;
		nr.xz *= v_band_fade.z; // normals outlive the ripple displacement
		vec2 nxz = ns.xz + nr.xz;

		// third cascade: the 3 m chop band supplies the fine normal detail
		// that breaks the sun glint into sparkle (normals + foam only - it
		// never displaces vertices, so buoyancy is unaffected). The 4x
		// weight: the raw chop slopes (~2.5 deg) can't scatter the noon
		// half-vector enough on their own - the boost is what breaks the
		// down-sun white-out into glints.
		if (u_chop_valid != 0 && v_band_fade.w > 0.0001)
		{
			vec4 packC = sample_pack_ts(u_nrm_chop, v_world_pos.xz, u_chop_tile, 0.25);
			vec3 nc = packC.xyz * 2.0 - 1.0;
			vec3 nc2 = sample_pack_ts(u_nrm_chop,
				kRot37 * v_world_pos.xz + vec2(73100.0, 38900.0), u_chop_tile * 1.37, 0.25).xyz * 2.0 - 1.0;
			nc.xz = mix(nc.xz, nc2.xz, 0.4);
			nxz += nc.xz * v_band_fade.w * 4.0;
			foamChop = packC.a * v_band_fade.w;
		}
		n = normalize(vec3(nxz.x, 1.0, nxz.y));

		// foam gate: a rotated rescaled fetch of the ripple band's foam copy
		// breaks the 8 m crest-to-crest regularity (the big swell-scale gate
		// used to paint the 373 m blobs the user flagged as fake)
		foamGateRipple = sample_pack(u_nrm_ripple,
			kRot37 * v_world_pos.xz + vec2(17700.0, 62300.0), u_ripple_tile * 1.37).a;
	}
	n = normalize(vec3(n.x * u_normal_strength, 1.0, n.z * u_normal_strength));
	// distance flatten: detail never exceeds the pixel footprint (~80% flat
	// by 500 m); kills far-field tiling and specular shimmer
	n = normalize(mix(n, vec3(0.0, 1.0, 0.0),
		0.8 * min(1.0, sqrt(viewDist * 1.65e-5) * 1.1)));

	// water column depth from the pre-pass scene depth copy; the gbuffer
	// depth is already linear (viewZ / far), so scene depth = d * far
	vec2 screenUV = gl_FragCoord.xy / u_rt_size;
	float sceneLinear = texture(u_scene_depth, screenUV).x * camera_far;
	float waterLinear = -v_view_pos.z;
	float depthDiff = max(sceneLinear - waterLinear, 0.0);

	float shore = 1.0 - clamp(depthDiff / u_shore_foam_depth, 0.0, 1.0);
	// alpha from submersion depth: fully opaque at the shore-foam range,
	// ~55% at the very waterline so submerged banks still show through
	float alpha = clamp(depthDiff / u_shore_foam_depth, 0.0, 1.0);
	alpha = 0.55 + 0.45 * alpha;

	// foam: crests from the baked jacobian copies in the normal arrays'
	// alpha + shoreline contact (depth-based). Ripple-band foam is weighted
	// down (8 m chop stripes read as tiling) and both gates de-repeat the
	// crest pattern. Fades out with view distance (subpixel foam only
	// shimmers, ~200-600 m).
	float foamBase = packS.a * v_band_fade.x + packR.a * 0.45 * v_band_fade.y
		+ foamChop * 0.3;
	// absolute threshold, tuned from the bakes' foam distributions (sum of
	// the three bands' weighted foam): the calm baseline's p99 sits at ~0.30
	// (zero whitecaps), the storm sample's p90 ~0.42 / p99 ~0.73 - the
	// (0.40, 0.60) band keeps only the strongest crests, so storms read as
	// whitecap streaks, not blankets. The crest SHAPE comes straight from
	// the bake's jacobian channel; the ripple gate only breaks the 8 m
	// crest-to-crest regularity (a swell-scale gate painted fake blobs).
	float windMod = clamp(u_wind_speed / 800.0, 0.25, 1.75);
	float crest = smoothstep(0.40, 0.60, foamBase) * u_foam_amount * windMod;
	crest *= 0.85 + 0.3 * foamGateRipple;
	// crest foam is a near-field detail: fades out by ~150-350 m (subpixel
	// whitecaps only shimmer at range). Shore foam is depth-based and stays.
	float foam = clamp(crest * (1.0 - smoothstep(15000.0, 35000.0, viewDist))
		+ shore * shore * u_foam_amount, 0.0, 1.0);

	vec3 viewDir = normalize(camera_pos - v_world_pos);
	vec3 sunDir = u_light_valid != 0 ? normalize(-light_dir) : vec3(0.0, 1.0, 0.0);
	vec3 sunCol = u_light_valid != 0 ? light_color * light_intensity : vec3(0.0);

	// moonlight: the sun light dims to zero at night; the water keeps a cool
	// moon glint (weak, capped) instead of going pitch black
	vec3 moonDir = u_moon_intensity > 0.0001 ? normalize(u_moon_dir) : vec3(0.0, 1.0, 0.0);
	vec3 moonCol = vec3(0.85, 0.92, 1.05) * u_moon_intensity;

	float shadow = csm_shadow(v_world_pos);

	// wave height term: troughs darken the body, crests glow (below)
	float hN = clamp(v_height * 0.005, -1.0, 1.0);

	// water body color: shallow scatter -> deep absorb, trough-darkened
	vec3 body = mix(u_scatter_color, u_absorb_color, clamp(depthDiff / 4000.0, 0.0, 1.0));
	body *= 1.0 - 0.35 * clamp(-hN, 0.0, 1.0);

	// wrapped diffuse: pow-shaped ndl keeps the body mostly-base colored
	// (kills the bright turquoise of a straight lambert term); the sun
	// diffuse weight is low - water is reflection-dominated, not diffuse
	float ndl = max(dot(n, sunDir), 0.0);
	float dif = pow(ndl * 0.4 + 0.6, 6.0);
	float ndlM = max(dot(n, moonDir), 0.0);
	float difM = pow(ndlM * 0.4 + 0.6, 6.0);
	vec3 col = body * (vec3(0.015, 0.03, 0.045) + dif * shadow * sunCol * 0.45
		+ difM * moonCol * 0.45);

	// sun-through-crest scatter glow, strongest looking toward the sun,
	// distance-gated beyond ~1 km (detail normals are gone there anyway)
	float towardSun = max(dot(-viewDir, sunDir), 0.0);
	float sssGate = 1.0 - smoothstep(60000.0, 100000.0, viewDist);
	col += u_scatter_color * clamp(hN, 0.0, 1.0)
		* (0.15 + 0.85 * towardSun * towardSun * towardSun) * 0.6 * shadow * sssGate;

	// sun GGX specular with a distance-adaptive lobe: wider at range so
	// the glitter path reads toward the sun instead of aliasing away.
	// Cap the final product: with a near-zenith sun the broad ndh~1 region
	// whites out the whole down-sun view otherwise
	vec3 h = normalize(viewDir + sunDir);
	float ndh = max(dot(n, h), 0.0);
	float rough = mix(u_roughness, 0.35, smoothstep(3000.0, 80000.0, viewDist));
	float a2 = rough * rough;
	float dden = (ndh * ndh * (a2 - 1.0) + 1.0);
	float dggx = a2 / (3.14159 * dden * dden);
	float spec = min(dggx * max(dot(n, viewDir), 0.0), 8.0);
	// sun-elevation-scaled product cap: a high sun's half-vector is near
	// vertical over the whole down-sun region, so the lobe saturates it at
	// any fixed cap - cap harder then (the chop glints break it up); a low
	// sun's glitter path stays bright
	float specCap = mix(1.2, 0.35, smoothstep(0.3, 0.9, sunDir.y));
	col += min(spec * sunCol, vec3(specCap)) * shadow;

	// moon glint path: same distance-adaptive lobe, cool and capped low -
	// a subtle shimmer at night instead of pitch black
	if (u_moon_intensity > 0.0001)
	{
		vec3 hm = normalize(viewDir + moonDir);
		float ndhm = max(dot(n, hm), 0.0);
		float ddenm = (ndhm * ndhm * (a2 - 1.0) + 1.0);
		float dggxm = a2 / (3.14159 * ddenm * ddenm);
		float specm = min(dggxm * max(dot(n, viewDir), 0.0), 8.0);
		col += min(specm * moonCol, vec3(0.3));
	}

	// sky-gradient reflection (SSR adds screen-space on top): reflect ray
	// always looks up, fresnel capped so the body survives grazing angles.
	// With the atmosphere up, the sky-view LUT gives the true sky color
	// (bright horizon -> deeper zenith, warm at sunset, dark at night);
	// the analytic 2-color gradient is the no-atmosphere fallback.
	vec3 rdir = reflect(-viewDir, n);
	// sky-view LUT sampling stays one row above the horizon boundary: the
	// boundary row bilinearly mixes the below-horizon ground bounce (reads
	// as a warm band at noon); the warp concentrates texels at the horizon
	// so this still reads as the horizon glow
	rdir.y = max(abs(rdir.y), 0.08);
	float fres = min(0.02 + 0.98 * pow(1.0 - max(dot(n, viewDir), 0.0), 5.0), 0.55);
	float dayF = smoothstep(-0.05, 0.25, sunDir.y);
	vec3 sunTint = sunCol / max(dot(sunCol, vec3(0.3333)), 1e-3);
	vec3 horizonCol = mix(vec3(0.55, 0.68, 0.82), sunTint * 0.8, 0.35) * mix(0.03, 1.0, dayF);
	vec3 zenithCol = vec3(0.10, 0.22, 0.45) * mix(0.03, 1.0, dayF);
	vec3 skyCol = mix(horizonCol, zenithCol, clamp(rdir.y, 0.0, 1.0));
	vec3 fogCol = horizonCol;
	if (u_atmosphere_enabled != 0)
	{
		skyCol = texture(u_skyview_lut, skyview_uv(rdir, sunDir)).rgb;
		// fog target: the horizon glow row toward the fragment. muH + 0.05:
		// the boundary row itself bilinearly mixes the below-horizon ground
		// bounce (a warm band at noon)
		vec3 fragH = v_world_pos - camera_pos;
		fragH.y = 0.0;
		vec3 dirH = normalize(fragH);
		float r = u_bottom_radius + u_view_height;
		float muH = -sqrt(max(0.0, r * r - u_bottom_radius * u_bottom_radius)) / r;
		fogCol = texture(u_skyview_lut,
			skyview_uv(normalize(vec3(dirH.x, muH + 0.05, dirH.z)), sunDir)).rgb;
	}
	col += fres * skyCol * 0.4;

	// foam is lit like the water: sun wrap by day, a moonlit hint at night
	// (plain white foam glowed in the dark)
	vec3 foamLit = vec3(0.92, 0.94, 0.96)
		* (vec3(0.015, 0.03, 0.045) + dif * shadow * sunCol * 0.9 + difM * moonCol * 0.5);
	col = mix(col, foamLit, foam);

	// distance fog + aerial perspective. With the atmosphere up: apply AP
	// first, then converge to the far-slice inscatter (fogCol = hazeFar
	// above) - exactly what an infinitely-distant object converges to, so
	// the far water, the far shore, and the horizon haze share one color.
	// Without the atmosphere: analytic horizon fallback.
	float fogF = smoothstep(u_fog_start, u_fog_end, viewDist);
	if (u_atmosphere_enabled != 0)
	{
		float distKm = length(v_view_pos) * 0.00001;
		float w = sqrt(clamp(distKm / u_ap_range, 0.0, 1.0));
		vec4 ap = texture(u_ap_volume, vec3(screenUV, w));
		col = col * ap.a + ap.rgb;
	}
	col = mix(col, fogCol, fogF);

	if (u_debug_view == 1) // foam mask
	{
		rt0 = vec4(vec3(foam), 1.0);
		rt1 = vec4(0.5, 1.0, 0.5, 1.0);
		return;
	}
	if (u_debug_view == 3) // ring wireframe (C++ draws with GL_LINE)
	{
		rt0 = vec4(u_debug_color, 1.0);
		rt1 = vec4(0.5, 1.0, 0.5, 1.0);
		return;
	}

	vec3 nView = normalize((invert_view_matrix * vec4(n, 0.0)).xyz);
	rt0 = vec4(col, alpha);
	rt1 = vec4(nView * 0.5 + 0.5, u_ssr_roughness);
}

#endif
