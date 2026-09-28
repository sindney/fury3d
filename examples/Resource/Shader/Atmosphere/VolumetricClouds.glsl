#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;

out vec2 out_uv;

void main()
{
	out_uv = vertex_uv;
	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);
}

#endif

#ifdef FRAGMENT_SHADER

#include "AtmosphereCommon.glsl"

in vec2 out_uv;

out vec4 fragment_output;

uniform sampler3D u_base_noise;      // R perlin-worley, GBA worley fbm octaves
uniform sampler3D u_detail_noise;    // RGB worley fbm octaves
uniform sampler2D u_weather_map;     // R coverage field, G type field
uniform sampler2D u_cirrus_noise;    // procedural wispy streaks (R coverage, G perlin, B worley)
uniform sampler2D u_transmittance_lut;
uniform sampler2D gbuffer_depth;     // linear view depth / far, 1.0 = sky

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;     // world -> view (engine misnomer)
uniform vec3 camera_pos;             // world cm
uniform float camera_far;

uniform float u_cloud_base_km;
uniform float u_cloud_top_km;
uniform float u_cloud_coverage;      // global bias over the weather field
uniform float u_cloud_type_bias;
uniform float u_cloud_base_scale;    // base noise uv per km
uniform float u_cloud_detail_scale;
uniform float u_cloud_erosion;       // detail erosion strength 0..1
uniform float u_cloud_density;       // extinction per km
uniform vec2  u_wind_offset_km;
uniform float u_cloud_fade_km;
uniform float u_daylight;

uniform float u_powder_strength;
uniform float u_hg_g;                // back/base lobe
uniform float u_hg_g_fwd;            // forward lobe (silver lining)
uniform float u_hg_blend;            // 0 = back only, 1 = forward only
uniform float u_ambient_scale;

uniform int u_max_steps;
uniform int u_light_samples;
uniform int u_debug_mode;            // 0 off, 1 steps heatmap, 2 transmittance, 3 cirrus
uniform float u_detail_fade_km;      // detail erosion fades to zero by this distance

uniform int   u_cirrus_enabled;
uniform float u_cirrus_coverage;
uniform float u_cirrus_alt_km;
uniform float u_cirrus_scale;
uniform float u_cirrus_density;

float remap(float v, float lo, float hi, float outLo, float outHi)
{
	return outLo + (outHi - outLo) * clamp((v - lo) / max(1e-5, hi - lo), 0.0, 1.0);
}

float hg_phase(float g, float c)
{
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * ATM_PI * pow(max(1e-4, 1.0 + g2 - 2.0 * g * c), 1.5));
}

// per-type height-density ramps: stratus band low, cumulus bulge mid,
// cumulonimbus full column with a widening top
float height_gradient(float h, float type)
{
	float stratus = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(0.12, 0.32, h));
	float cumulus = smoothstep(0.0, 0.22, h) * (1.0 - smoothstep(0.55, 0.95, h));
	float cb = smoothstep(0.0, 0.12, h) * (0.6 + 0.4 * smoothstep(0.45, 0.9, h));
	float t2 = clamp(type * 2.0, 0.0, 1.0);
	return mix(mix(stratus, cumulus, t2), cb, clamp(type * 2.0 - 1.0, 0.0, 1.0));
}

vec2 weather_at(vec3 worldKm)
{
	// one weather tile spans ~24 km
	return texture(u_weather_map, (worldKm.xz) * 0.04167).rg;
}

// density in [0,1]; full=0 skips detail erosion (cheap sampler)
// distKm drives the mip/erosion LOD: far noise aliases into confetti
float sample_density(vec3 worldKm, float h, vec2 weather, int full, float distKm)
{
	float cov = clamp(weather.x + u_cloud_coverage - 0.5, 0.001, 1.0);
	float type = clamp(weather.y + u_cloud_type_bias, 0.0, 1.0);

	vec3 buvw = (worldKm + vec3(u_wind_offset_km.x, 0.0, u_wind_offset_km.y)) * u_cloud_base_scale;
	float baseLod = clamp(log2(distKm * u_cloud_base_scale * 2.0), 0.0, 4.0);
	vec4 base = textureLod(u_base_noise, buvw, baseLod);
	float lowFbm = dot(base.gba, vec3(0.625, 0.25, 0.125));

	// carve low-density regions with the low-frequency worley, then shape
	float d = clamp((base.r - (1.0 - lowFbm)) / max(1e-4, lowFbm), 0.0, 1.0);
	d *= height_gradient(h, type);

	// coverage as remap erosion: clouds inflate/contract smoothly
	d = clamp((d - (1.0 - cov)) / cov, 0.0, 1.0);
	if (d <= 0.0)
		return 0.0;

	// wispy bottoms
	d *= smoothstep(0.0, 0.1, h);

	if (full != 0)
	{
		float detailLod = clamp(1.0 - distKm / u_detail_fade_km, 0.0, 1.0);
		if (detailLod > 0.0)
		{
			vec3 duvw = (worldKm + vec3(u_wind_offset_km.x, 0.0, u_wind_offset_km.y) * 1.5) * u_cloud_detail_scale;
			float detail = dot(textureLod(u_detail_noise, duvw, baseLod * 0.5).rgb, vec3(0.625, 0.25, 0.125));
			// erode edges and lower regions hardest
			float erode = detail * u_cloud_erosion * (0.25 + 0.75 * (1.0 - h)) * 0.35 * detailLod;
			d = clamp((d - erode) / max(1e-4, 1.0 - erode), 0.0, 1.0);
		}
	}
	return d;
}

// sun optical depth through the slab; 6 cone-ish taps on the cheap sampler
float light_optical_depth(vec3 worldKm)
{
	float thick = u_cloud_top_km - u_cloud_base_km;
	float ds = thick * 0.06;
	float sum = 0.0;
	for (int j = 1; j <= 6; j++)
	{
		if (j > u_light_samples) break;
		vec3 lp = worldKm + u_sun_dir * (ds * float(j));
		float alt = u_view_height + (lp.y - camera_pos.y * CM_TO_KM);
		float lh = (alt - u_cloud_base_km) / thick;
		if (lh < 0.0 || lh > 1.0) continue;
		sum += sample_density(lp, lh, weather_at(lp), 0, 0.5) * ds;
	}
	return sum;
}

void main()
{
	vec2 ndc = out_uv * 2.0 - 1.0;
	vec3 rayViewFar = (inverse(projection_matrix) * vec4(ndc, 1.0, 1.0)).xyz;
	vec3 ray = normalize((inverse(invert_view_matrix) * vec4(normalize(rayViewFar), 0.0)).xyz);

	// scene-occluder distance along the ray (km); sky pixels never clip
	float depth = texture(gbuffer_depth, out_uv).r;
	float tSceneKm = 1e9;
	if (depth < 0.99999)
		tSceneKm = length(rayViewFar * camera_far) * depth * CM_TO_KM;

	float thick = u_cloud_top_km - u_cloud_base_km;
	float rCam = u_bottom_radius + u_view_height;
	float baseR = u_bottom_radius + u_cloud_base_km;
	float topR = u_bottom_radius + u_cloud_top_km;

	// shell bounds; camera above the slab is out of scope (ground view)
	if (rCam > topR)
	{
		fragment_output = vec4(0.0);
		return;
	}

	float b = ray.y * rCam;   // dot(ray, camPos_planetRelative(0, rCam, 0))
	float cIn = rCam * rCam - baseR * baseR;
	float cOut = rCam * rCam - topR * topR;
	float discIn = b * b - cIn;
	float discOut = b * b - cOut;
	if (discOut < 0.0)
	{
		fragment_output = vec4(0.0);
		return;
	}
	float tOut1 = -b + sqrt(discOut);
	float tEnter = 0.0;
	if (cIn < 0.0)
	{
		// below the slab: enter at the inner-sphere exit
		tEnter = -b + sqrt(max(discIn, 0.0));
	}
	else
	{
		// inside the slab: descending rays end at the inner sphere
		float tIn0 = -b - sqrt(max(discIn, 0.0));
		if (tIn0 > 0.0)
			tOut1 = min(tOut1, tIn0);
	}

	float tEnd = min(tOut1, tSceneKm);
	tEnd = min(tEnd, tEnter + u_cloud_fade_km * 1.5);
	if (tEnd <= tEnter)
	{
		fragment_output = vec4(0.0);
		return;
	}

	vec3 camKm = camera_pos * CM_TO_KM;

	// per-pixel start jitter kills banding (deterministic: no time term)
	float jitter = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);

	float span = tEnd - tEnter;
	float dt = span / float(u_max_steps);
	float t = tEnter + dt * jitter;

	float cosViewSun = dot(ray, u_sun_dir);
	float phase = mix(hg_phase(u_hg_g, cosViewSun), hg_phase(u_hg_g_fwd, cosViewSun), u_hg_blend);

	vec3 sunCol = u_sun_color * u_sun_intensity * u_daylight *
		atm_sun_transmittance(u_transmittance_lut, u_bottom_radius + u_cloud_base_km + thick * 0.5, u_sun_dir.y);
	vec3 ambCol = u_ambient_scale * mix(vec3(0.02, 0.025, 0.045), vec3(0.32, 0.4, 0.55), u_daylight)
		* mix(vec3(1.0), u_sun_color, 0.4);

	float T = 1.0;
	vec3 L = vec3(0.0);
	int fineMode = 0;
	int emptyRun = 0;
	int stepsUsed = 0;

	for (int i = 0; i < 160; i++)
	{
		if (t >= tEnd || T < 0.01)
			break;
		stepsUsed = i;

		vec3 posKm = camKm + ray * t;
		float alt = u_view_height + (posKm.y - camKm.y);
		float h = (alt - u_cloud_base_km) / thick;
		if (h < 0.0 || h > 1.0)
		{
			t += dt;
			continue;
		}

		vec2 weather = weather_at(posKm);
		float d = sample_density(posKm, h, weather, fineMode, t);

		if (fineMode == 0)
		{
			if (d <= 0.0)
			{
				t += dt;
				continue;
			}
			// hit the implicit surface: step back, switch to fine sampling
			fineMode = 1;
			t -= dt;
			dt = span / float(u_max_steps) * 0.25;
			t += dt;
			continue;
		}

		if (d <= 0.0)
		{
			// a few empty fine steps are normal inside the surface; a long
			// run means we left it - back to cheap marching
			if (++emptyRun >= 4)
			{
				fineMode = 0;
				emptyRun = 0;
				dt = span / float(u_max_steps);
			}
			t += dt;
			continue;
		}
		emptyRun = 0;

		// fade far samples into haze
		float a = 1.0 - exp(-d * u_cloud_density * dt);
		a *= 1.0 - smoothstep(u_cloud_fade_km * 0.5, u_cloud_fade_km * 1.5, t);

		float dSun = light_optical_depth(posKm);
		float beer = exp(-dSun * u_cloud_density);
		float powder = mix(1.0, 1.0 - exp(-dSun * u_cloud_density * 2.0), u_powder_strength);
		float amb = pow(1.0 - d, 0.5) * mix(0.3, 1.0, h);

		vec3 S = sunCol * (beer * powder * phase * 4.0 * ATM_PI) + ambCol * amb;
		L += T * a * S;
		T *= 1.0 - a;

		t += dt;
	}

	// cirrus deck above the slab, behind whatever the march accumulated
	if (u_cirrus_enabled != 0 && ray.y > 0.015)
	{
		float tc = (u_cirrus_alt_km - u_view_height) / ray.y;
		if (tc > 0.0 && tc < tSceneKm)
		{
			vec3 cp = camKm + ray * tc;
			vec4 cirSamp = textureLod(u_cirrus_noise, (cp.xz + u_wind_offset_km * 2.0) * u_cirrus_scale, 0.0);
			float cir = clamp(cirSamp.r, 0.0, 1.0);
			float ca = clamp((cir - (1.0 - u_cirrus_coverage)) / max(1e-4, u_cirrus_coverage), 0.0, 1.0);
			ca *= 1.0 - smoothstep(u_cloud_fade_km, u_cloud_fade_km * 3.0, tc);
			if (ca > 0.002)
			{
				vec3 cirSun = u_sun_color * u_sun_intensity * u_daylight *
					atm_sun_transmittance(u_transmittance_lut, u_bottom_radius + u_cirrus_alt_km, u_sun_dir.y);
				vec3 cirL = cirSun * (0.5 + 0.5 * ca) + ambCol * 0.6;
				float cirA = 1.0 - exp(-ca * u_cirrus_density);
				// cirrus only shows where the deck did not fully occlude --
				// this stops the cirrus bleeding through solid cumulus and
				// produces the doubled-ghost artifact (two clouds at
				// different altitudes showing parallax in the same pixel)
				float tr = T * T;
				L += tr * cirA * cirL;
				T *= 1.0 - cirA * tr;
			}
		}
	}

	if (u_debug_mode == 1)
	{
		float f = float(stepsUsed) / float(u_max_steps);
		fragment_output = vec4(f, 1.0 - abs(f * 2.0 - 1.0), 1.0 - f, 1.0);
		return;
	}
	if (u_debug_mode == 2)
	{
		fragment_output = vec4(vec3(1.0 - T), 1.0);
		return;
	}
	if (u_debug_mode == 3)
	{
		// dump raw cirrus value at the ray's cirrus-altitude point
		vec3 cpd = camKm + ray * max(0.001, (u_cirrus_alt_km - u_view_height) / max(0.001, ray.y));
		vec4 cs = textureLod(u_cirrus_noise, (cpd.xz + u_wind_offset_km * 2.0) * u_cirrus_scale, 0.0);
		fragment_output = vec4(cs.rgb, 1.0);
		return;
	}

	fragment_output = vec4(L, 1.0 - T);
}

#endif
