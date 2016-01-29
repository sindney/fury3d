#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

out vec3 vs_dir;
out vec3 vs_pos;
out vec4 ss_pos;

uniform vec3 light_dir;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;

void main()
{
	vs_dir = normalize(invert_view_matrix * vec4(light_dir, 0)).xyz;
	vs_pos = (inverse(projection_matrix) * vec4(vertex_position, 1.0)).xyz;
	ss_pos = vec4(vertex_position.xyz, 1.0);
	gl_Position = ss_pos;
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 fragment_output;

in vec3 vs_dir;
in vec3 vs_pos;
in vec4 ss_pos;

uniform vec3 light_color;
uniform float light_intensity;

// linear depth
uniform sampler2D gbuffer_depth;
// normal, shniness
uniform sampler2D gbuffer_normal;

vec3 pos_from_depth(const in vec2 screenUV)
{
	const vec4 bit_shift = vec4(1.0 / 16777216.0, 1.0 / 65536.0, 1.0 / 256.0, 1.0);

	vec4 rgba_depth = texture(gbuffer_depth, screenUV);
    float depth = dot(rgba_depth, bit_shift);

	vec3 view_ray = vs_pos.xyz;

	return view_ray * depth;
}

vec4 apply_lighting(const in vec3 normal, const in vec3 surface_pos, const in float shininess)
{
	vec3 L = normalize(vs_dir);
	vec3 V = normalize(-surface_pos);
	
	vec3 N = normalize(normal);
	vec3 H = normalize(L + V);

	float NdotL = max(0.0, dot(N, L));
	float NdotH = max(0.0, dot(N, H));

	return vec4(
		vec3(1) * light_color * NdotL * light_intensity, 
		pow(NdotH, shininess) * light_intensity
	);
}

void main()
{
	vec2 screenUV = ss_pos.xy * 0.5 + 0.5;
	vec3 vs_surface_pos = pos_from_depth(screenUV);

	vec4 raw_normal = texture(gbuffer_normal, screenUV);
	vec3 vs_normal = raw_normal.xyz * 2.0 - 1.0;
	float shininess = raw_normal.w * 100;

	fragment_output = apply_lighting(vs_normal, vs_surface_pos, shininess);
}

#endif