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

// LDR color buffer to anti-alias (typically the post-tonemap output).
uniform sampler2D fxaa_input;

// Render-target size in pixels (for texel offsets).
uniform vec2 u_rt_size = vec2(1280.0, 720.0);

// Quality preset: 0 = off, 1 = low, 2 = medium, 3 = high.
// Maps to the blend-span clamp (texels): 4 / 6 / 8. 0 short-circuits
// the pass and returns the input unchanged so a disabled chain entry
// isn't a no-op render. Declared float (not int) because effect JSON
// uniforms bind as Uniform1f — glUniform on a mismatched int uniform
// would silently keep the default.
uniform float u_quality = 3.0;

// 1 = encode sRGB on write. Set by the chain runner when this is the
// FINAL effect (intermediate temps stay linear); see ACES.glsl.
uniform int u_gamma_correct = 0;

vec3 encode_srgb(vec3 c)
{
	c.r = (c.r <= 0.0031308) ? 12.92 * c.r : 1.055 * pow(c.r, 1.0 / 2.4) - 0.055;
	c.g = (c.g <= 0.0031308) ? 12.92 * c.g : 1.055 * pow(c.g, 1.0 / 2.4) - 0.055;
	c.b = (c.b <= 0.0031308) ? 12.92 * c.b : 1.055 * pow(c.b, 1.0 / 2.4) - 0.055;
	return c;
}

// FXAA (Lottes, NVIDIA whitepaper 3.11 — console/quality hybrid, the
// canonical reduced form also shipped as three.js' FXAAShader).
//
// Per pixel: sample the 3x3 luma neighborhood, derive the edge
// direction from the luma GRADIENTS (not a fixed axis), clamp the
// blend span, then blend two 2-tap filtered variants along the edge.
// The luma-range validation is the anti-distortion guard: if the
// stronger filter leaves the neighborhood's luma range, fall back to
// the milder one — the image can soften but never smear.
vec3 fxaa(sampler2D tex, vec2 uv, vec2 rcp_frame, float span_max)
{
	vec3 luma_w = vec3(0.299, 0.587, 0.114);

	vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * rcp_frame).rgb;
	vec3 rgbNE = texture(tex, uv + vec2( 1.0, -1.0) * rcp_frame).rgb;
	vec3 rgbSW = texture(tex, uv + vec2(-1.0,  1.0) * rcp_frame).rgb;
	vec3 rgbSE = texture(tex, uv + vec2( 1.0,  1.0) * rcp_frame).rgb;
	vec3 rgbM  = texture(tex, uv).rgb;

	float lumaNW = dot(rgbNW, luma_w);
	float lumaNE = dot(rgbNE, luma_w);
	float lumaSW = dot(rgbSW, luma_w);
	float lumaSE = dot(rgbSE, luma_w);
	float lumaM  = dot(rgbM,  luma_w);

	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

	// Edge direction: perpendicular to the luma gradient, reduced
	// where local contrast is too low to trust (flat regions blend
	// along an arbitrary axis at span 0 instead of smearing).
	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

	const float reduce_mul = 1.0 / 8.0;
	const float reduce_min = 1.0 / 128.0;
	float dirReduce = max(
		(lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * reduce_mul),
		reduce_min);
	float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = min(vec2(span_max), max(vec2(-span_max), dir * rcpDirMin)) * rcp_frame;

	vec3 rgbA = 0.5 * (
		texture(tex, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
		texture(tex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (
		texture(tex, uv + dir * -0.5).rgb +
		texture(tex, uv + dir *  0.5).rgb);

	float lumaB = dot(rgbB, luma_w);
	if (lumaB < lumaMin || lumaB > lumaMax)
		return rgbA;
	return rgbB;
}

void main()
{
	if (u_quality <= 0.5)
	{
		vec3 passThrough = texture(fxaa_input, out_uv).rgb;
		if (u_gamma_correct != 0)
			passThrough = encode_srgb(passThrough);
		fragment_output = vec4(passThrough, 1.0);
		return;
	}

	int quality = int(u_quality + 0.5);
	float spanMax = (quality == 1) ? 4.0 : (quality == 2 ? 6.0 : 8.0);

	vec3 col = fxaa(fxaa_input, out_uv, 1.0 / u_rt_size, spanMax);

	if (u_gamma_correct != 0)
		col = encode_srgb(col);

	fragment_output = vec4(col, 1.0);
}

#endif
