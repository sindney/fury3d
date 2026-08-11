#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;

out vec2 out_uv;
out vec3 vs_pos;

uniform mat4 projection_matrix;
uniform float camera_far = 10000;

void main()
{
	out_uv = vertex_uv;
	// view-space ray to the far plane (depth reconstruction for aerial
	// perspective, same convention as SunLight.glsl)
	vs_pos = (inverse(projection_matrix) * vec4(vertex_position.xy, 1.0, 1.0) * camera_far).xyz;
	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);
}

#endif

#ifdef FRAGMENT_SHADER

in vec2 out_uv;
in vec3 vs_pos;

out vec4 fragment_output;

// Accumulated HDR radiance from the light pass (rgba16f). Unlike the
// Lambert pipeline — where the light buffer holds albedo-free
// irradiance and this pass multiplies by diffuse — the PBR light
// shaders output full radiance (albedo-aware BRDF), so here we only
// add the ambient floor.
uniform sampler2D hdr_light;

// Albedo (+ metallic in alpha); used for the ambient floor only.
uniform sampler2D gbuffer_diffuse;

// linear depth (1.0 = far plane = sky mask)
uniform sampler2D gbuffer_depth;

// aerial-perspective volume (96x54x32 rgba16f: rgb inscatter, a mean
// transmittance); dummy-bound when no sky is active
uniform sampler3D u_ap_volume;

// Constant ambient floor, same role (and value) as the LDR pipeline's
// pass_light clearColor [0.01]: a faint lift so unlit regions aren't
// pure black. Multiplied by albedo so dark surfaces stay dark.
uniform float u_ambient = 0.01;

uniform int u_atmosphere_enabled = 0;
uniform float u_ap_range = 5.0;   // km, squared slice distribution

#ifdef WITH_EDITOR
// Editor LOD-debug tint (see Lambert.glsl / GBuffer.glsl).
uniform vec4 lod_debug_color = vec4(0.0, 0.0, 0.0, 0.0);
#endif

void main()
{
	vec3 lighting = texture(hdr_light, out_uv).rgb;
	vec3 albedo = texture(gbuffer_diffuse, out_uv).rgb;

	vec3 col = lighting + albedo * u_ambient;
#ifdef WITH_EDITOR
	if (lod_debug_color.a > 0.0)
		col *= lod_debug_color.rgb;
#endif

	// aerial perspective on opaque pixels: transmittance + inscatter from
	// the camera volume (sky pixels keep the ambient floor only; the sky
	// pass overwrites them)
	if (u_atmosphere_enabled != 0)
	{
		float depth = texture(gbuffer_depth, out_uv).r;
		if (depth < 1.0)
		{
			float distKm = length(vs_pos) * depth * 0.00001;
			float w = sqrt(clamp(distKm / u_ap_range, 0.0, 1.0));
			vec4 ap = texture(u_ap_volume, vec3(out_uv, w));
			col = col * ap.a + ap.rgb;
		}
	}

	// Output stays linear HDR (rgba16f) — tonemap + sRGB encode happen
	// downstream in the postprocess chain (ACES) or pbr_final_shader.
	fragment_output = vec4(col, 1.0);
}

#endif
