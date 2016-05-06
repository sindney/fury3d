#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

uniform float camera_far = 10000;

uniform mat4 projection_matrix;

out vec3 vs_pos;
out vec4 ss_pos;

void main()
{
	vs_pos = (inverse(projection_matrix) * vec4(vertex_position.xy, 1.0, 1.0) * camera_far).xyz;
	ss_pos = vec4(vertex_position.xyz, 1.0);
	gl_Position = ss_pos;
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 fragment_output;

in vec3 vs_pos;
in vec4 ss_pos;

uniform float min_variance = 0.00005;

uniform mat4 light_matrix;

// linear depth
uniform sampler2D gbuffer_depth;
uniform sampler2D shadow_buffer;

float linstep(float min, float max, float v)
{
	return clamp((v - min) / (max - min), 0, 1);
}

float reduce_light_bleeding(float p_max, float amount)
{
	return linstep(amount, 1, p_max);
}

float chebyshev_upper_bound(vec2 moments, float t)
{
	float p = float(t <= moments.x);

	float variance = moments.y - (moments.x * moments.x);
	variance = max(variance, min_variance);

	float d = t - moments.x;
	float p_max = variance / (variance + d * d);

	p_max = reduce_light_bleeding(p_max, 0.2);

	return max(p, p_max);
}

vec3 pos_from_depth(const in vec2 screenUV)
{
	float depth = texture(gbuffer_depth, screenUV).r;
	vec3 view_ray = vs_pos.xyz;
	return view_ray * depth;
}

void main()
{
	vec2 screenUV = ss_pos.xy * 0.5 + 0.5;
	vec4 shadowCoord = light_matrix * vec4(pos_from_depth(screenUV), 1.0);

	vec2 scale = vec2(1, 1) / vec2(textureSize(shadow_buffer, 0));
	vec4 moments = 0.25 * (
		texture(shadow_buffer, shadowCoord.xy + scale * vec2(-1,-1)) + 
		texture(shadow_buffer, shadowCoord.xy + scale * vec2( 1,-1)) + 
		texture(shadow_buffer, shadowCoord.xy + scale * vec2(-1, 1)) + 
		texture(shadow_buffer, shadowCoord.xy + scale * vec2( 1, 1)));

	float contribution = chebyshev_upper_bound(moments.xy, shadowCoord.z / shadowCoord.w);
	fragment_output = vec4(contribution, contribution, contribution, 1);
}

#endif