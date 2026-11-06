#version 330

#include "GBufferCommon.glsl"

#ifdef VERTEX_SHADER
in vec3 vertex_position;
out vec2 out_uv;
void main()
{
	out_uv = vertex_position.xy * 0.5 + 0.5;
	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 out_uv;
out vec4 fragment_output;
uniform sampler2D src;
uniform int u_depth;
uniform int u_normal;
void main()
{
	vec4 texel = texture(src, out_uv);
	if (u_depth != 0)
		fragment_output = vec4(vec3(texel.r), 1.0);
	else if (u_normal != 0)
		fragment_output = vec4(GBufferDecodeNormal(texel) * 0.5 + 0.5, 1.0);
	else
		fragment_output = vec4(texel.rgb, 1.0);
}
#endif
