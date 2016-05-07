#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

out vec4 out_position;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	gl_Position = projection_matrix * invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);
	out_position = gl_Position;
}

#endif

#ifdef FRAGMENT_SHADER

in vec4 out_position;

out vec4 fragment_output;

void main()
{
	float depth = out_position.z / out_position.w;
	depth = depth * 0.5 + 0.5;

	vec2 moments;
	moments.x = depth;

	float dx = dFdx(depth);
	float dy = dFdy(depth);

	moments.y = depth * depth + 0.25 * (dx * dx + dy * dy);

	fragment_output = vec4(moments.x, moments.y, 0.0, 0.0);
}

#endif