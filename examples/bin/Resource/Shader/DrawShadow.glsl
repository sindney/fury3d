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

uniform mat4 light_matrix;

// linear depth
uniform sampler2D gbuffer_depth;
uniform sampler2D shadow_buffer;

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
	
	float compare = 1.0 - float(texture(shadow_buffer, shadowCoord.xy).x < shadowCoord.z);
	fragment_output = vec4(compare ,compare ,compare ,1);
}

#endif