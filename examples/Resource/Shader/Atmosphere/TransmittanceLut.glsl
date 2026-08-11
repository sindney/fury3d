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

// 256x64 transmittance LUT: exp(-optical depth) from (r, mu) to the top of
// the atmosphere; 0 when the ground blocks the ray. 40 samples (reference
// RenderTransmittanceLutPS).
void main()
{
	float r, mu;
	atm_transmittance_inv_uv(out_uv, r, mu);

	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 p = up * r;
	vec3 d = normalize(up * mu + vec3(0.0, 0.0, 1.0) * sqrt(max(0.0, 1.0 - mu * mu)));

	float tGround = atm_ray_ground(p, d);
	if (tGround > 0.0)
	{
		fragment_output = vec4(0.0);
		return;
	}

	float tMax = atm_ray_exit_top(p, d);
	float dt = tMax / 40.0;
	vec3 od = vec3(0.0);
	for (int i = 0; i < 40; i++)
	{
		vec3 q = p + d * (dt * (float(i) + 0.5));
		float h = length(q) - u_bottom_radius;
		vec3 scat, ext;
		atm_medium(h, scat, ext);
		od += ext * dt;
	}
	fragment_output = vec4(exp(-od), 1.0);
}

#endif
