#version 330

#include "Lambert/VegetationCommon.glsl"

#ifdef VERTEX_SHADER

in vec3 vertex_position;
#ifdef ALPHA_TEST
in vec2 vertex_uv;
out vec2 out_uv;
#endif
#ifdef WIND
in vec4 vertex_color;
uniform float u_time = 0.0;
uniform vec4 u_wind_params = vec4(1.0, 0.0, 1.0, 1.0);
#endif

out float out_depth;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	vec4 worldPos = world_matrix * vec4(vertex_position, 1.0);
#ifdef WIND
	worldPos.xyz += VegetationWindOffset(vertex_color, worldPos.xyz, u_time, u_wind_params);
#endif
#ifdef ALPHA_TEST
	out_uv = vertex_uv;
#endif
	vec4 viewPos = invert_view_matrix * worldPos;
	out_depth = -viewPos.z;

	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

uniform float camera_far = 10000;

in float out_depth;

#ifdef ALPHA_TEST
in vec2 out_uv;
uniform sampler2D diffuse_texture;
uniform float transparency = 0.0;
uniform float u_alpha_cutoff = 0.5;
#endif

void main()
{
#ifdef ALPHA_TEST
	vec4 texel = texture(diffuse_texture, out_uv);
	if (texel.a * (1.0 - transparency) < u_alpha_cutoff)
		discard;
#endif
	gl_FragDepth = out_depth / camera_far;
}

#endif
