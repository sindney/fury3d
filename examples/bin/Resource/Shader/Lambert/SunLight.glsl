#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

out vec3 vs_dir;
out vec3 vs_pos;
out vec4 ss_pos;

uniform float camera_far = 10000;

uniform vec3 light_dir;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;

void main()
{
	vs_dir = normalize(invert_view_matrix * vec4(-light_dir, 0)).xyz;
	vs_pos = (inverse(projection_matrix) * vec4(vertex_position.xy, 1.0, 1.0) * camera_far).xyz;
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

#ifdef CSM

uniform mat4 projection_matrix;
uniform mat4 shadow_matrix[4];
uniform sampler2DArray shadow_buffer;

uniform float bias = 0.002;
uniform vec4 shadow_far;

#endif

#ifdef SHADOW

uniform sampler2D shadow_buffer;
uniform mat4 shadow_matrix;

#endif

vec3 pos_from_depth(const in vec2 screenUV)
{
	float depth = texture(gbuffer_depth, screenUV).r;
	vec3 view_ray = vs_pos.xyz;
	return view_ray * depth;
}

vec4 apply_lighting(const in vec3 normal, const in vec3 surface_pos)
{
	vec3 L = normalize(vs_dir);
	vec3 N = normalize(normal);

	float NdotL = max(0.0, dot(N, L));

	return vec4(
		vec3(1) * light_color * NdotL * light_intensity, 1.0
	);
}

void main()
{
	vec2 screenUV = ss_pos.xy * 0.5 + 0.5;
	vec3 vs_surface_pos = pos_from_depth(screenUV);

	vec4 raw_normal = texture(gbuffer_normal, screenUV);
	vec3 vs_normal = raw_normal.xyz * 2.0 - 1.0;

	fragment_output = apply_lighting(vs_normal, vs_surface_pos);

#ifdef CSM
	
	vec4 pos = projection_matrix * vec4(vs_surface_pos, 1.0);

	int index = 3;
	if (pos.z < shadow_far.x)
		index = 0;
	else if(pos.z < shadow_far.y)
		index = 1;
	else if(pos.z < shadow_far.z)
		index = 2;

	vec4 shadowCoord = shadow_matrix[index] * vec4(vs_surface_pos, 1.0);
	shadowCoord = shadowCoord / shadowCoord.w;
	float depth = shadowCoord.z - bias;
	vec3 crood = vec3(shadowCoord.x, shadowCoord.y, float(index));
	fragment_output *= depth > 1.0 ? 1.0 : float(depth < texture(shadow_buffer, crood).x);

#endif

#ifdef SHADOW

	vec4 shadowCoord = shadow_matrix * vec4(vs_surface_pos, 1.0);
	shadowCoord = shadowCoord / shadowCoord.w;
	float depth = shadowCoord.z - bias;
	fragment_output *= depth > 1.0 ? 1.0 : float(depth < texture(shadow_buffer, shadowCoord.xy).x);

#endif
}

#endif