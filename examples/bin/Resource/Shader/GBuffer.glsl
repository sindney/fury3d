#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;
in vec3 vertex_normal;

out vec3 out_normal;
out vec2 out_uv;
out float out_depth;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	vec4 worldPos = world_matrix * vec4(vertex_position, 1.0);
	vec4 viewPos = invert_view_matrix * worldPos;

	out_depth = -viewPos.z;
	out_normal = normalize(invert_view_matrix * world_matrix * vec4(vertex_normal, 0.0)).xyz;
	out_uv = vertex_uv;
	
	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 out_normal;
in vec2 out_uv;
in float out_depth;

uniform float camera_far = 10000;
uniform float shininess = 0;

uniform sampler2D diffuse_texture;

uniform float diffuse_factor = 1;
uniform float specular_factor = 1;

// normal.xyz, shininess
layout (location = 0) out vec4 rt0;
// linear depth
layout (location = 1) out vec4 rt1;
// diffuse rgb, specular intensity
layout (location = 2) out vec4 rt2;

vec4 pack_depth(const in float depth)
{
    const vec4 bit_shift = vec4(16777216.0, 65536.0, 256.0, 1.0);
    const vec4 bit_mask  = vec4(0.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
    vec4 res = fract(depth * bit_shift);
    res -= res.xxyz * bit_mask;
    return res;
}

void main()
{
	rt0.rgb = (out_normal.rgb + 1) * 0.5;
	rt0.a = shininess / 100;

	rt1 = pack_depth(out_depth / camera_far);

	rt2 = texture(diffuse_texture, out_uv) * diffuse_factor;
	rt2.a = dot(rt2.rgb, vec3(0.2126, 0.7152, 0.0722)) * specular_factor;
}

#endif