#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

out vec4 world_position;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	world_position = world_matrix * vec4(vertex_position, 1.0);
	gl_Position = projection_matrix * invert_view_matrix * world_position;
}

#endif

#ifdef FRAGMENT_SHADER

uniform float light_far = 10000;
uniform vec3 light_pos;

in vec4 world_position;

void main()
{
	gl_FragDepth = length(world_position.xyz - light_pos) / light_far;
}

#endif