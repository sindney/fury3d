#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	gl_Position = projection_matrix * invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 fragment_output;

uniform vec3 color = vec3(0,1,0);

void main()
{
	fragment_output.rgb = color;
	fragment_output.a = 1.0;
}

#endif