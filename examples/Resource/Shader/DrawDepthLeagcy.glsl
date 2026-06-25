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

void main()
{
	// gl_FragDepth = gl_FragCoord.z;
}

#endif