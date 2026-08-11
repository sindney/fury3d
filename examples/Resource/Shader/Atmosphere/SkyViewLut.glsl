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
uniform sampler2D u_multiscatter_lut;

// 192x108 sky-view LUT, re-rendered per frame (viewer-height and sun
// dependent). 30-sample march (reference SkyViewLutPS).
void main()
{
	float mu, cosAzim;
	atm_skyview_inv_uv(out_uv, mu, cosAzim);

	float r = u_bottom_radius + u_view_height;
	vec3 p = vec3(0.0, r, 0.0);

	// rebuild the view dir: zenith mu; azimuth relative to the sun's
	// horizontal projection (azimuth 0 = toward the sun)
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 sunH = u_sun_dir - up * u_sun_dir.y;
	float sunHL = length(sunH);
	vec3 azimRef = sunHL > 1e-4 ? sunH / sunHL : vec3(1.0, 0.0, 0.0);
	vec3 azimSide = normalize(cross(up, azimRef));
	float sinAzim = sqrt(max(0.0, 1.0 - cosAzim * cosAzim));
	vec3 d = normalize(up * mu + azimRef * (cosAzim * sqrt(max(0.0, 1.0 - mu * mu)))
		+ azimSide * (sinAzim * sqrt(max(0.0, 1.0 - mu * mu))));

	float tMax = atm_ray_exit_top(p, d);
	float tGround = atm_ray_ground(p, d);
	bool hitGround = tGround > 0.0 && tGround < tMax;
	if (hitGround) tMax = tGround;

	vec3 T;
	vec3 L = atm_march(u_transmittance_lut, u_multiscatter_lut, p, d, tMax, 30, T);

	if (hitGround)
	{
		// simple ground bounce: albedo * sun transmittance at the hit point
		vec3 g = p + d * tMax;
		vec3 sunT = atm_sun_transmittance(u_transmittance_lut, u_bottom_radius,
			dot(normalize(g), u_sun_dir));
		vec3 ms = texture(u_multiscatter_lut, vec2(dot(normalize(g), u_sun_dir) * 0.5 + 0.5, 0.0)).rgb;
		L += T * u_ground_albedo * (sunT * max(0.0, dot(up, u_sun_dir)) + ms)
			* u_sun_color * u_sun_intensity / ATM_PI;
	}

	fragment_output = vec4(L, 1.0);
}

#endif
