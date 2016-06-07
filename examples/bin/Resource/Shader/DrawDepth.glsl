#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

out float out_depth;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	vec4 viewPos = invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);
	out_depth = -viewPos.z;

	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

uniform float camera_far = 10000;

in float out_depth;

void main()
{
	gl_FragDepth = out_depth / camera_far;
}

#endif