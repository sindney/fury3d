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

// HDR composite (rgba16f) written by pass_combine in the HDR
// pipeline. We just sample + sRGB encode to the default framebuffer
// (or the editor's offscreen RT, when u_gamma_correct=1).
uniform sampler2D hdr_composite;

// 1 = shader-side sRGB encode (non-sRGB FBO, e.g. editor viewport).
// 0 = GL_FRAMEBUFFER_SRGB encodes (default framebuffer).
uniform int u_gamma_correct = 0;

void main()
{
	vec4 inCol = texture(hdr_composite, out_uv);
	vec3 col = inCol.rgb;

	if (u_gamma_correct != 0)
	{
		col.r = (col.r <= 0.0031308) ? 12.92 * col.r : 1.055 * pow(col.r, 1.0 / 2.4) - 0.055;
		col.g = (col.g <= 0.0031308) ? 12.92 * col.g : 1.055 * pow(col.g, 1.0 / 2.4) - 0.055;
		col.b = (col.b <= 0.0031308) ? 12.92 * col.b : 1.055 * pow(col.b, 1.0 / 2.4) - 0.055;
	}

	fragment_output = vec4(col, 1.0);
}

#endif