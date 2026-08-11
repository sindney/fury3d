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

uniform sampler2D u_transmittance_lut;
uniform sampler2D u_cloud_noise;   // R coverage fbm, G detail, tileable

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;   // world -> view (engine misnomer)
uniform vec3 camera_pos;           // world cm

uniform float u_cloud_coverage;    // 0..1
uniform float u_cloud_alt_km;      // deck bottom above surface
uniform float u_cloud_thick_km;
uniform float u_cloud_scale;       // noise uv per km
uniform float u_cloud_density;     // extinction per km
uniform vec2  u_wind_offset_km;
uniform float u_daylight;          // 0 night .. 1 day (TOD driven)
uniform float u_cloud_fade_km = 2.5;  // deck dissolves into haze by this distance

float cloud_phase(float c)
{
	float g = 0.55;
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * ATM_PI * pow(max(1e-4, 1.0 + g2 - 2.0 * g * c), 1.5));
}

// coverage at a slab point: warped noise, detail-eroded edges
float cloud_coverage(vec3 worldKm)
{
	vec2 nuv = (worldKm.xz + u_wind_offset_km) * u_cloud_scale;
	vec4 n = texture(u_cloud_noise, nuv);
	float shaped = clamp(n.r * 1.1 - (1.0 - n.g) * 0.3, 0.0, 1.0);
	return smoothstep(1.0 - u_cloud_coverage, 1.0, shaped);
}

// Half-res planar cloud deck. One coverage sample per ray plus one probe
// straight up for the top/bottom shading gradient (taps ALONG the ray band
// horizontally and dilate coverage at grazing angles - this is a 2D deck,
// not a volume). 4 taps toward the sun give the silver lining. Output is
// premultiplied; the sky pass composites as sky*(1-a) + rgb.
void main()
{
	vec2 ndc = out_uv * 2.0 - 1.0;
	vec4 viewPos = inverse(projection_matrix) * vec4(ndc, 1.0, 1.0);
	vec3 ray = normalize((inverse(invert_view_matrix) * vec4(normalize(viewPos.xyz), 0.0)).xyz);

	if (abs(ray.y) < 1e-4)
	{
		fragment_output = vec4(0.0);
		return;
	}

	vec3 camKm = camera_pos * CM_TO_KM;
	float t0 = (u_cloud_alt_km - camKm.y) / ray.y;
	float t1 = (u_cloud_alt_km + u_cloud_thick_km - camKm.y) / ray.y;
	float tEnter = max(min(t0, t1), 0.0);
	float tExit = max(t0, t1);
	if (tExit <= tEnter)
	{
		fragment_output = vec4(0.0);
		return;
	}

	vec3 mid = camKm + ray * ((tEnter + tExit) * 0.5);
	float cov = cloud_coverage(mid);
	if (cov < 0.004)
	{
		fragment_output = vec4(0.0);
		return;
	}

	// vertical gradient: probe half a thickness straight up - more cloud
	// above means we see a bottom (darker), less means a top (lighter)
	float covUp = cloud_coverage(mid + vec3(0.0, u_cloud_thick_km * 0.5, 0.0));
	float shade = clamp(0.9 + (covUp - cov) * 1.6, 0.68, 1.1);

	float pathKm = tExit - tEnter;
	float T = exp(-cov * u_cloud_density * pathKm);

	// light toward the sun: 4 taps through the deck
	float cosViewSun = dot(ray, u_sun_dir);
	float phase = cloud_phase(cosViewSun);
	float Tl = 1.0;
	float ds = u_cloud_thick_km / 4.0;
	for (int j = 1; j <= 4; j++)
	{
		vec3 lq = mid + u_sun_dir * (ds * float(j));
		float hh = (lq.y - u_cloud_alt_km) / u_cloud_thick_km;
		if (hh < 0.0 || hh > 1.0) continue;
		Tl *= exp(-cloud_coverage(lq) * u_cloud_density * ds);
	}

	vec3 sunAtCloud = u_sun_color * u_sun_intensity * u_daylight *
		atm_sun_transmittance(u_transmittance_lut, u_bottom_radius + u_cloud_alt_km, u_sun_dir.y);
	vec3 ambient = mix(vec3(0.02, 0.03, 0.05), u_sun_color * 0.4, u_daylight);
	vec3 lightCol = (sunAtCloud * (Tl * phase * 4.0) + ambient) * shade;

	float alpha = 1.0 - T;
	alpha *= 1.0 - smoothstep(u_cloud_fade_km * 0.15, u_cloud_fade_km, tEnter);
	vec3 L = lightCol * alpha;
	fragment_output = vec4(L, alpha);
}

#endif
