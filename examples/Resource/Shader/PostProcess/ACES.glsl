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

// HDR scene-lighting target produced by the lighting pass.
uniform sampler2D hdr_input;

// ACES filmic curve (Stephen Hill / Krzysztof Narkowicz fit).
// Input is HDR (linear, can exceed 1.0); output is tonemapped
// LDR before the sRGB encode below.
vec3 ACESFilm(vec3 x)
{
	float a = 2.51;
	float b = 0.03;
	float c = 2.43;
	float d = 0.59;
	float e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Exposure multiplier (default 1.0). Editors / users can scale
// the input before tonemapping for HDR-bright scenes.
uniform float u_exposure = 1.0;

// 1 = encode sRGB (matches GL_FRAMEBUFFER_SRGB on the default FB;
// needed when the editor writes into a non-sRGB offscreen RT).
// 0 = leave linear (default-FB case where GL_FRAMEBUFFER_SRGB
// does the encode).
uniform int u_gamma_correct = 0;

void main()
{
	vec4 inCol = texture(hdr_input, out_uv);
	vec3 col = inCol.rgb * u_exposure;
	col = ACESFilm(col);

	if (u_gamma_correct != 0)
	{
		col.r = (col.r <= 0.0031308) ? 12.92 * col.r : 1.055 * pow(col.r, 1.0 / 2.4) - 0.055;
		col.g = (col.g <= 0.0031308) ? 12.92 * col.g : 1.055 * pow(col.g, 1.0 / 2.4) - 0.055;
		col.b = (col.b <= 0.0031308) ? 12.92 * col.b : 1.055 * pow(col.b, 1.0 / 2.4) - 0.055;
	}

	fragment_output = vec4(col, 1.0);
}

#endif