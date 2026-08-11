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

uniform sampler2D gbuffer_depth;      // linear depth, 1.0 = sky mask
uniform sampler2D u_skyview_lut;
uniform sampler2D u_transmittance_lut;
uniform sampler2D u_cloud_tex;
uniform sampler2D u_moon_tex;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;      // world -> view (engine misnomer)
uniform float camera_far;

uniform float u_sun_ang_cos;          // cos(sun angular radius)
uniform float u_sun_disc_intensity;
uniform vec3  u_moon_dir;
uniform float u_moon_ang_cos;
uniform float u_moon_frame_scale;     // 1 / tan(moon angular radius)
uniform float u_moon_intensity;
uniform int   u_moon_enabled;
uniform int   u_clouds_enabled;

void main()
{
	// geometry pixels are combine's job (aerial perspective lives there)
	if (texture(gbuffer_depth, out_uv).r < 1.0)
		discard;

	vec2 ndc = out_uv * 2.0 - 1.0;
	vec4 viewPos = inverse(projection_matrix) * vec4(ndc, 1.0, 1.0);
	vec3 ray = normalize((inverse(invert_view_matrix) * vec4(normalize(viewPos.xyz), 0.0)).xyz);

	vec3 up = vec3(0.0, 1.0, 0.0);
	float mu = dot(ray, up);

	// sun-relative azimuth for the skyview lut
	vec3 sunH = u_sun_dir - up * u_sun_dir.y;
	float sunHL = length(sunH);
	vec3 rayH = ray - up * mu;
	float rayHL = length(rayH);
	float cosAzim = (sunHL > 1e-4 && rayHL > 1e-4)
		? clamp(dot(sunH / sunHL, rayH / rayHL), -1.0, 1.0) : 1.0;

	vec3 sky = texture(u_skyview_lut, atm_skyview_uv(mu, cosAzim)).rgb;

	// transmittance along the view ray (for both discs)
	float r = u_bottom_radius + u_view_height;
	vec3 viewT = atm_sun_transmittance(u_transmittance_lut, r, mu);

	// sun disc with cheap limb darkening
	float cosViewSun = dot(ray, u_sun_dir);
	if (cosViewSun > u_sun_ang_cos)
	{
		float t = clamp((cosViewSun - u_sun_ang_cos) / max(1e-8, 1.0 - u_sun_ang_cos), 0.0, 1.0);
		float limb = mix(0.55, 1.0, sqrt(t));
		sky += u_sun_color * (u_sun_disc_intensity * limb) * viewT;
	}

	// moon disc: textured, phase terminator from the sun-moon angle
	if (u_moon_enabled != 0)
	{
		float cosViewMoon = dot(ray, u_moon_dir);
		if (cosViewMoon > u_moon_ang_cos)
		{
			vec3 mRef = abs(u_moon_dir.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : up;
			vec3 t1 = normalize(cross(mRef, u_moon_dir));
			vec3 t2 = cross(u_moon_dir, t1);
			vec2 muv = vec2(dot(ray, t1), dot(ray, t2)) * u_moon_frame_scale;
			if (max(abs(muv.x), abs(muv.y)) < 1.0)
			{
				// sphere normal of the earth-facing hemisphere (toward the
				// viewer); dot with the sun gives the phase terminator
				vec3 mn = normalize(-u_moon_dir + (t1 * muv.x + t2 * muv.y) * 0.09);
				float phase = clamp(dot(mn, u_sun_dir), 0.0, 1.0);
				vec3 albedo = texture(u_moon_tex, muv * 0.5 + 0.5).rgb;
				sky += albedo * phase * u_moon_intensity * viewT;
			}
		}
	}

	// clouds (premultiplied half-res target)
	if (u_clouds_enabled != 0)
	{
		vec4 cloud = texture(u_cloud_tex, out_uv);
		sky = sky * (1.0 - cloud.a) + cloud.rgb;
	}

	fragment_output = vec4(sky, 1.0);
}

#endif
