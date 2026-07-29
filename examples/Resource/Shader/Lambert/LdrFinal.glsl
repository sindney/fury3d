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

// LDR composite (rgba8, linear) written by pass_combine; the
// transparent pass blends into the same texture. We just sample +
// sRGB encode to the default framebuffer (or the editor's offscreen
// RT, when u_gamma_correct=1). Mirrors PbrFinal.glsl.
uniform sampler2D ldr_composite;

uniform int u_gamma_correct = 0;

void main()
{
	vec3 col = texture(ldr_composite, out_uv).rgb;

	if (u_gamma_correct != 0)
	{
		col.r = (col.r <= 0.0031308) ? 12.92 * col.r : 1.055 * pow(col.r, 1.0 / 2.4) - 0.055;
		col.g = (col.g <= 0.0031308) ? 12.92 * col.g : 1.055 * pow(col.g, 1.0 / 2.4) - 0.055;
		col.b = (col.b <= 0.0031308) ? 12.92 * col.b : 1.055 * pow(col.b, 1.0 / 2.4) - 0.055;
	}

	fragment_output = vec4(col, 1.0);
}

#endif
