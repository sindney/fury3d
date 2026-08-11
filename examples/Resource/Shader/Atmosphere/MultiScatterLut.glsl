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

// 32x32 multi-scattering LUT: isotropic incoming radiance field from 2nd+
// order scattering (sun irradiance folded to 1; the view march multiplies
// sun color/intensity). Fragment-port of the reference compute shader: the
// groupshared tree reduction becomes a plain per-fragment accumulation over
// the same 64 stratified sphere directions; the infinite-bounce tail is the
// geometric series 1/(1-r).
void main()
{
	float sunMu = out_uv.x * 2.0 - 1.0;
	float h = out_uv.y * (u_top_radius - u_bottom_radius);
	float r = u_bottom_radius + h;
	vec3 p = vec3(0.0, r, 0.0);
	vec3 sunDir = normalize(vec3(sqrt(max(0.0, 1.0 - sunMu * sunMu)), sunMu, 0.0));

	vec3 Lacc = vec3(0.0);
	vec3 facc = vec3(0.0);
	const int DIRS = 64;
	for (int i = 0; i < DIRS; i++)
	{
		// stratified sphere directions (reference: 8x8 lat-long cells with
		// fixed in-cell offset 0.3/0.7)
		float fi = float(i);
		float z = 1.0 - 2.0 * (mod(fi, 8.0) + 0.3) / 8.0;
		float a = 2.0 * ATM_PI * (floor(fi / 8.0) + 0.7) / 8.0;
		float sz = sqrt(max(0.0, 1.0 - z * z));
		vec3 dir = vec3(sz * cos(a), z, sz * sin(a));

		float tMax = atm_ray_exit_top(p, dir);
		float tGround = atm_ray_ground(p, dir);
		if (tGround > 0.0 && tGround < tMax) tMax = tGround;
		if (tMax <= 0.0) continue;

		float dt = tMax / 20.0;
		vec3 T = vec3(1.0);
		vec3 L1 = vec3(0.0);
		vec3 f1 = vec3(0.0);
		float cosDirSun = dot(dir, sunDir);
		float phaseR = atm_phase_rayleigh(cosDirSun);
		float phaseM = atm_phase_mie(cosDirSun);
		for (int s = 0; s < 20; s++)
		{
			vec3 q = p + dir * (dt * (float(s) + 0.5));
			float rq = max(length(q), u_bottom_radius);
			float hq = rq - u_bottom_radius;
			vec3 scat, ext;
			atm_medium(hq, scat, ext);
			vec3 sunT = atm_sun_transmittance(u_transmittance_lut, rq, dot(normalize(q), sunDir));
			L1 += T * sunT * (u_rayleigh_scat * atm_density_rayleigh(hq) * phaseR
				+ vec3(u_mie_scat * atm_density_mie(hq) * phaseM)) * dt;
			f1 += T * scat * dt;
			T *= exp(-ext * dt);
		}
		Lacc += L1;
		facc += f1;
	}
	vec3 Lin = Lacc / float(DIRS);
	vec3 r2 = facc / float(DIRS);
	vec3 result = Lin / max(vec3(1e-5), vec3(1.0) - r2);
	fragment_output = vec4(result, 1.0);
}

#endif
