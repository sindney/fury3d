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

uniform sampler2D crt_input;

// Render-target size in pixels (used for scanline frequency + vignette).
uniform vec2 u_rt_size = vec2(1280.0, 720.0);

// Scanline strength (0 = off, 1 = full black interlace lines).
uniform float u_scanline_strength = 0.25;

// Scanline frequency in pixels-per-line (lower = thicker).
uniform float u_scanline_freq = 2.0;

// Barrel curvature strength. 0 = no curvature, 0.05 = subtle bend,
// 0.15 = heavy CRT look. Distorts the UV like a CRT phosphor mask.
uniform float u_curvature = 0.08;

// Vignette darkness at the corners (0 = none, 1 = full black corners).
uniform float u_vignette = 0.35;

// Chromatic aberration amount in pixels (R/G/B sample offset).
uniform float u_chroma = 1.0;

// 1 = encode sRGB on write (final chain effect only); see ACES.glsl.
uniform int u_gamma_correct = 0;

vec3 encode_srgb(vec3 c)
{
	c.r = (c.r <= 0.0031308) ? 12.92 * c.r : 1.055 * pow(c.r, 1.0 / 2.4) - 0.055;
	c.g = (c.g <= 0.0031308) ? 12.92 * c.g : 1.055 * pow(c.g, 1.0 / 2.4) - 0.055;
	c.b = (c.b <= 0.0031308) ? 12.92 * c.b : 1.055 * pow(c.b, 1.0 / 2.4) - 0.055;
	return c;
}

// Apply CRT barrel distortion to a UV centered on (0.5, 0.5).
vec2 distort(vec2 uv)
{
	vec2 c = uv - vec2(0.5);
	float r2 = dot(c, c);
	c *= 1.0 + u_curvature * r2;
	return c + vec2(0.5);
}

void main()
{
	vec2 uv = distort(out_uv);

	// Clamp / discard when the distortion pushes UVs out of the
	// texture (CRT corners go black).
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
	{
		fragment_output = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	// Sub-pixel chromatic aberration: sample R/G/B with horizontal
	// offsets scaled by u_chroma (in pixels).
	vec2 px = vec2(u_chroma) / u_rt_size;
	vec3 col;
	col.r = texture(crt_input, uv + vec2(px.x, 0.0)).r;
	col.g = texture(crt_input, uv).g;
	col.b = texture(crt_input, uv - vec2(px.x, 0.0)).b;

	// Scanlines: square-wave alternating dimming on each row.
	float scan = sin(uv.y * u_rt_size.y / max(u_scanline_freq, 1.0) * 3.14159);
	scan = scan * 0.5 + 0.5; // 0..1
	float scanMask = mix(1.0, scan, u_scanline_strength);
	col *= scanMask;

	// Vignette: smooth falloff from center to corners.
	vec2 vc = out_uv - vec2(0.5);
	float vignette = 1.0 - smoothstep(0.4, 0.85, length(vc) * (1.0 + u_vignette));
	col *= mix(1.0, vignette, u_vignette);

	if (u_gamma_correct != 0)
		col = encode_srgb(col);

	fragment_output = vec4(col, 1.0);
}

#endif