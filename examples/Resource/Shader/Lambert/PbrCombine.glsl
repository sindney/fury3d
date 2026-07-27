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

in vec2 out_uv;

out vec4 fragment_output;

// Accumulated HDR radiance from the light pass (rgba16f). Unlike the
// Lambert pipeline — where the light buffer holds albedo-free
// irradiance and this pass multiplies by diffuse — the PBR light
// shaders output full radiance (albedo-aware BRDF), so here we only
// add the ambient floor.
uniform sampler2D hdr_light;

// Albedo (+ metallic in alpha); used for the ambient floor only.
uniform sampler2D gbuffer_diffuse;

// Constant ambient floor, same role (and value) as the LDR pipeline's
// pass_light clearColor [0.01]: a faint lift so unlit regions aren't
// pure black. Multiplied by albedo so dark surfaces stay dark.
uniform float u_ambient = 0.01;

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

	// Output stays linear HDR (rgba16f) — tonemap + sRGB encode happen
	// downstream in the postprocess chain (ACES) or pbr_final_shader.
	fragment_output = vec4(col, 1.0);
}

#endif
