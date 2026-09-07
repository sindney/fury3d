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
#ifdef SKINNED_MESH
in ivec4 bone_ids;
in vec3 bone_weights;
uniform mat4 bone_matrices[35];
#endif

#if defined(INSTANCED) && !defined(INSTANCE_SSBO)
in vec4 instance_row0;
in vec4 instance_row1;
in vec4 instance_row2;
in vec4 instance_row3;
#endif
#if defined(INSTANCED) && defined(INSTANCE_SSBO)
layout(std430, binding = 2) buffer InstanceBuffer { mat4 instance_matrices[]; };
#endif

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
#ifdef SKINNED_MESH
	mat4 bone_matrix = bone_matrices[bone_ids[0]] * bone_weights[0];
	bone_matrix += bone_matrices[bone_ids[1]] * bone_weights[1];
	bone_matrix += bone_matrices[bone_ids[2]] * bone_weights[2];
	bone_matrix += bone_matrices[bone_ids[3]] * (1.0 - bone_weights[0] - bone_weights[1] - bone_weights[2]);
	vec4 worldPos = world_matrix * bone_matrix * vec4(vertex_position, 1.0);
#else
#if defined(INSTANCED) && defined(INSTANCE_SSBO)
	mat4 worldMat = instance_matrices[gl_InstanceID];
#elif defined(INSTANCED)
	mat4 worldMat = mat4(instance_row0, instance_row1, instance_row2, instance_row3);
#else
	mat4 worldMat = world_matrix;
#endif
	vec4 worldPos = worldMat * vec4(vertex_position, 1.0);
#ifdef WIND
	// sway matches the gbuffer WIND variant so shadows track the canopy
	worldPos.xyz += VegetationWindOffset(vertex_color, worldPos.xyz, u_time, u_wind_params);
#endif
#endif
#ifdef ALPHA_TEST
	out_uv = vertex_uv;
#endif
	gl_Position = projection_matrix * invert_view_matrix * worldPos;
}

#endif

#ifdef FRAGMENT_SHADER

#ifdef ALPHA_TEST
in vec2 out_uv;
uniform sampler2D diffuse_texture;
uniform float transparency = 0.0;
uniform float u_alpha_cutoff = 0.5;
#endif

void main()
{
#ifdef ALPHA_TEST
	// leaf-shaped shadows: cutout foliage discards like the gbuffer MASK path
	vec4 texel = texture(diffuse_texture, out_uv);
	if (texel.a * (1.0 - transparency) < u_alpha_cutoff)
		discard;
#endif
	// gl_FragDepth = gl_FragCoord.z;
}

#endif
