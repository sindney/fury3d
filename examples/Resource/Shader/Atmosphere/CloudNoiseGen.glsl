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

uniform float u_slice_id;       // z slice for 3D modes
uniform float u_slice_count;
uniform int u_gen_mode;         // 0 = base Perlin-Worley, 1 = detail Worley, 2 = weather map, 3 = cirrus
uniform float u_weather_bias;   // coverage field offset
uniform float u_weather_type_contrast;

float hash1(vec3 p)
{
	return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

vec3 hash3(vec3 p)
{
	return fract(sin(vec3(dot(p, vec3(127.1, 311.7, 74.7)),
		dot(p, vec3(269.5, 183.3, 246.1)),
		dot(p, vec3(113.5, 271.9, 124.6)))) * 43758.5453123);
}

vec3 grad_hash(vec3 p)
{
	float h = hash1(p) * 6.2831853;
	float z = hash1(p + 19.19) * 2.0 - 1.0;
	float r = sqrt(max(0.0, 1.0 - z * z));
	return vec3(r * cos(h), r * sin(h), z);
}

// tileable 3D perlin: lattice wraps mod period
float perlin3(vec3 uvw, float period)
{
	vec3 p = uvw * period;
	vec3 i = floor(p);
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	float n = mix(
		mix(mix(dot(grad_hash(mod(i, period)), f),
			dot(grad_hash(mod(i + vec3(1, 0, 0), period)), f - vec3(1, 0, 0)), u.x),
			mix(dot(grad_hash(mod(i + vec3(0, 1, 0), period)), f - vec3(0, 1, 0)),
				dot(grad_hash(mod(i + vec3(1, 1, 0), period)), f - vec3(1, 1, 0)), u.x), u.y),
		mix(mix(dot(grad_hash(mod(i + vec3(0, 0, 1), period)), f - vec3(0, 0, 1)),
			dot(grad_hash(mod(i + vec3(1, 0, 1), period)), f - vec3(1, 0, 1)), u.x),
			mix(dot(grad_hash(mod(i + vec3(0, 1, 1), period)), f - vec3(0, 1, 1)),
				dot(grad_hash(mod(i + vec3(1, 1, 1), period)), f - vec3(1, 1, 1)), u.x), u.y), u.z);
	return n * 0.5 + 0.5;
}

float perlin_fbm(vec3 uvw, float freq, int octaves)
{
	float sum = 0.0;
	float amp = 0.5;
	for (int o = 0; o < 8; o++)
	{
		if (o >= octaves) break;
		sum += perlin3(uvw, freq) * amp;
		freq *= 2.0;
		amp *= 0.5;
	}
	return sum;
}

// tileable 3D worley: feature cells wrap mod period; 0 at feature points
float worley3(vec3 uvw, float period)
{
	vec3 p = uvw * period;
	vec3 i = floor(p);
	vec3 f = fract(p);
	float mind = 8.0;
	for (int x = -1; x <= 1; x++)
	for (int y = -1; y <= 1; y++)
	for (int z = -1; z <= 1; z++)
	{
		vec3 g = vec3(float(x), float(y), float(z));
		vec3 feat = hash3(mod(i + g, period));
		mind = min(mind, length(g + feat - f));
	}
	return clamp(mind, 0.0, 1.0);
}

// inverted worley fbm: billows
float worley_fbm(vec3 uvw, float freq)
{
	float w = (1.0 - worley3(uvw, freq)) * 0.625
		+ (1.0 - worley3(uvw, freq * 2.0)) * 0.25
		+ (1.0 - worley3(uvw, freq * 4.0)) * 0.125;
	return clamp(w, 0.0, 1.0);
}

// worley billows raise the perlin floor: connected structure with puffy bulges
float perlin_worley(vec3 uvw, float freq)
{
	float wfbm = worley_fbm(uvw, freq);
	float p = perlin_fbm(uvw, freq, 4);
	return clamp(wfbm + p * (1.0 - wfbm), 0.0, 1.0);
}

void main()
{
	if (u_gen_mode == 2)
	{
		// weather map: R coverage field, G cloud-type field (2D, z fixed)
		vec3 uvw = vec3(out_uv, 0.35);
		float cov = perlin_fbm(uvw, 4.0, 5);
		cov = clamp((cov - 0.5) * 1.8 + 0.5 + u_weather_bias, 0.0, 1.0);
		float type = perlin_fbm(vec3(out_uv, 0.73), 3.0, 4);
		type = clamp((type - 0.5) * u_weather_type_contrast + 0.5, 0.0, 1.0);
		fragment_output = vec4(cov, type, 0.0, 1.0);
		return;
	}

	if (u_gen_mode == 3)
	{
		// cirrus: anisotropic streaks, low-frequency worley + perlin banding
		// (different character than the cumulus deck - reads as wispy)
		vec2 uv = out_uv * vec2(2.4, 1.0);   // stretch horizontally
		vec3 uvw = vec3(uv, 0.55);
		float streaks = perlin_fbm(uvw, 6.0, 5);
		float w = (1.0 - worley3(vec3(uv, 0.55), 3.0)) * 0.7;
		float c = clamp(streaks * 0.6 + w * 0.5, 0.0, 1.0);
		// high contrast so a coverage=0.85 reads as obvious streaks
		fragment_output = vec4(c, streaks, w, 1.0);
		return;
	}

	vec3 uvw = vec3(out_uv, (u_slice_id + 0.5) / u_slice_count);

	if (u_gen_mode == 0)
	{
		// base: R perlin-worley, GBA worley fbm at rising frequency
		float pw = perlin_worley(uvw, 4.0);
		float w0 = worley_fbm(uvw, 4.0);
		float w1 = worley_fbm(uvw, 8.0);
		float w2 = worley_fbm(uvw, 16.0);
		fragment_output = vec4(pw, w0, w1, w2);
	}
	else
	{
		// detail: RGB worley fbm at rising frequency
		float w0 = worley_fbm(uvw, 4.0);
		float w1 = worley_fbm(uvw, 8.0);
		float w2 = worley_fbm(uvw, 16.0);
		fragment_output = vec4(w0, w1, w2, 1.0);
	}
}

#endif
