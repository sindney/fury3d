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

// Set to 1 when the final composite renders into the editor's offscreen
// viewport render target (a non-sRGB RGBA8 FBO). The default framebuffer
// path leaves this 0 and relies on GL_FRAMEBUFFER_SRGB to gamma-encode;
// that GL path is a no-op for non-sRGB FBOs, so the shader must encode
// itself to keep the viewport from rendering too dark.
uniform int u_gamma_correct;

void main()
{
	vec4 diffuse = texture(gbuffer_diffuse, out_uv);
	vec4 lighting = texture(gbuffer_light, out_uv);

	vec3 col = lighting.rgb * diffuse.rgb;
	if (u_gamma_correct != 0)
	{
		// Exact sRGB encoding curve (matches GL_FRAMEBUFFER_SRGB, which
		// is a no-op for the non-sRGB viewport RT). Using the precise
		// piecewise curve instead of pow(x, 1/2.2) keeps the viewport's
		// dark values matching the default-framebuffer render.
		col.r = (col.r <= 0.0031308) ? 12.92 * col.r : 1.055 * pow(col.r, 1.0 / 2.4) - 0.055;
		col.g = (col.g <= 0.0031308) ? 12.92 * col.g : 1.055 * pow(col.g, 1.0 / 2.4) - 0.055;
		col.b = (col.b <= 0.0031308) ? 12.92 * col.b : 1.055 * pow(col.b, 1.0 / 2.4) - 0.055;
	}

	fragment_output.rgb = col;
	fragment_output.a = 1.0;
}

#endif