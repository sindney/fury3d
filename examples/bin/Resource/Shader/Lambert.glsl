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

uniform sampler2D gbuffer_diffuse;
uniform sampler2D gbuffer_light;

void main()
{
	vec4 diffuse = texture(gbuffer_diffuse, out_uv);
	vec4 lighting = texture(gbuffer_light, out_uv);

	fragment_output.rgb = lighting.rgb * diffuse.rgb;
	fragment_output.a = 1.0;
}

#endif