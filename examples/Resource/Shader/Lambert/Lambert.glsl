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

uniform sampler2D gbuffer_diffuse;
uniform sampler2D gbuffer_light;

// 1 = shader-side sRGB encode (non-sRGB FBO, e.g. editor viewport).
// 0 = GL_FRAMEBUFFER_SRGB encodes (default framebuffer).
uniform int u_gamma_correct;

#ifdef WITH_EDITOR
// Editor LOD-debug tint. The GBuffer pass bakes it into diffuse, so
// the composite re-multiplies the same color here. See GBuffer.glsl.
uniform vec4 lod_debug_color = vec4(0.0, 0.0, 0.0, 0.0);
#endif

void main()
{
	vec4 diffuse = texture(gbuffer_diffuse, out_uv);
	vec4 lighting = texture(gbuffer_light, out_uv);

	vec3 col = lighting.rgb * diffuse.rgb;
#ifdef WITH_EDITOR
	if (lod_debug_color.a > 0.0)
		col *= lod_debug_color.rgb;
#endif
	if (u_gamma_correct != 0)
	{
		// Piecewise sRGB encode — matches GL_FRAMEBUFFER_SRGB so the
		// editor viewport's dark values agree with the default FB.
		col.r = (col.r <= 0.0031308) ? 12.92 * col.r : 1.055 * pow(col.r, 1.0 / 2.4) - 0.055;
		col.g = (col.g <= 0.0031308) ? 12.92 * col.g : 1.055 * pow(col.g, 1.0 / 2.4) - 0.055;
		col.b = (col.b <= 0.0031308) ? 12.92 * col.b : 1.055 * pow(col.b, 1.0 / 2.4) - 0.055;
	}

	fragment_output.rgb = col;
	fragment_output.a = 1.0;
}

#endif