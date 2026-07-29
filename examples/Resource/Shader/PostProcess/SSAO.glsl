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

uniform sampler2D scene;
uniform sampler2D gbuffer_depth;   // linear view depth / camera_far
uniform sampler2D gbuffer_normal;  // view normal *0.5+0.5

uniform mat4 projection_matrix;
uniform float camera_far = 10000;

uniform float u_radius = 30.0;
uniform float u_strength = 1.0;
uniform float u_power = 1.0;
uniform float u_bias = 1.0;

// 1 = encode sRGB on write (set by the chain runner when this is the
// final effect); intermediates stay linear. Same block as ACES/CRT.
uniform int u_gamma_correct = 0;

vec3 encode_srgb(vec3 c)
{
	c.r = (c.r <= 0.0031308) ? 12.92 * c.r : 1.055 * pow(c.r, 1.0 / 2.4) - 0.055;
	c.g = (c.g <= 0.0031308) ? 12.92 * c.g : 1.055 * pow(c.g, 1.0 / 2.4) - 0.055;
	c.b = (c.b <= 0.0031308) ? 12.92 * c.b : 1.055 * pow(c.b, 1.0 / 2.4) - 0.055;
	return c;
}

const int SAMPLE_COUNT = 24;

// Interleaved-gradient-style hash, no noise texture needed.
float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

vec3 view_pos_from_depth(vec2 uv, float depth)
{
	float viewZ = -depth * camera_far;
	vec2 ndc = uv * 2.0 - 1.0;
	// P[0][0] = 1/(aspect*tan(f/2)), P[1][1] = 1/tan(f/2)
	return vec3(ndc.x * -viewZ / projection_matrix[0][0],
				ndc.y * -viewZ / projection_matrix[1][1],
				viewZ);
}

void main()
{
	vec3 col = texture(scene, out_uv).rgb;
	float depth = texture(gbuffer_depth, out_uv).r;

	float ao = 1.0;

	// Sky: no occlusion.
	if (depth < 1.0)
	{
		vec3 pos = view_pos_from_depth(out_uv, depth);
		vec3 normal = normalize(texture(gbuffer_normal, out_uv).xyz * 2.0 - 1.0);

	// Random per-pixel kernel rotation around the normal.
	float angle = hash12(gl_FragCoord.xy) * 6.2831853;
	vec2 noise = vec2(cos(angle), sin(angle));

	float occlusion = 0.0;
	for (int i = 0; i < SAMPLE_COUNT; ++i)
	{
		// Spiral-ish hemisphere taps, denser near the center.
		float fi = float(i) + 0.5;
		float r = (fi / float(SAMPLE_COUNT));
		float theta = fi * 2.399963; // golden angle spiral
		vec2 dir = vec2(cos(theta), sin(theta));
		// rotate by noise
		dir = vec2(dir.x * noise.x - dir.y * noise.y, dir.x * noise.y + dir.y * noise.x);

		vec3 samplePos = pos + vec3(dir * u_radius * r, u_radius * r * 0.5);
		samplePos += normal * u_radius * 0.25; // hemisphere lift

		// project sample back to screen
		vec4 clip = projection_matrix * vec4(samplePos, 1.0);
		vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
		if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
			continue;

		float sampleDepth = texture(gbuffer_depth, sampleUV).r;
		float sampleViewZ = -sampleDepth * camera_far;

		float rangeCheck = smoothstep(0.0, 1.0, u_radius / max(abs(pos.z - sampleViewZ), 1e-4));
		occlusion += (sampleViewZ >= samplePos.z + u_bias ? 1.0 : 0.0) * rangeCheck;
	}

		ao = 1.0 - (occlusion / float(SAMPLE_COUNT)) * u_strength;
		ao = pow(clamp(ao, 0.0, 1.0), u_power);
	}

#ifdef DEBUG_VIEW
	// Debug view: raw AO term as grayscale. White = unoccluded.
	fragment_output = vec4(vec3(ao), 1.0);
	return;
#endif

	col = col * ao;

	if (u_gamma_correct != 0)
		col = encode_srgb(col);

	fragment_output = vec4(col, 1.0);
}

#endif
