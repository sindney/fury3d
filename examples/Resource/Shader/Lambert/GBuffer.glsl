#version 330

#include "VegetationCommon.glsl"

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;
in vec3 vertex_normal;

#ifdef SKINNED_MESH
in ivec4 bone_ids;
in vec3 bone_weights;
uniform mat4 bone_matrices[35];
#endif

#ifdef WIND
// Kraut wind weights (sway / flutter / phase / variation).
in vec4 vertex_color;
uniform float u_time = 0.0;
uniform vec4 u_wind_params = vec4(1.0, 0.0, 1.0, 1.0);
#endif

#if defined(INSTANCED) && !defined(INSTANCE_SSBO)
// Divisor-VBO instance stream (GL 3.3/4.1 fallback path).
in vec4 instance_row0;
in vec4 instance_row1;
in vec4 instance_row2;
in vec4 instance_row3;
#endif
#if defined(INSTANCED) && defined(INSTANCE_SSBO)
// SSBO instance stream (GL 4.3+ preferred path), indexed by gl_InstanceID.
layout(std430, binding = 2) buffer InstanceBuffer { mat4 instance_matrices[]; };
#endif

#ifdef BILLBOARD
uniform vec3 camera_pos;
uniform vec2 u_billboard_atlas = vec2(8.0, 1.0);
// Shading-normal up bias (0 = camera-facing). Tuned 0.28 default tracks
// the mesh tiers' sun response; per-material override for art direction.
uniform float u_billboard_up_bias = 0.28;
#endif

out vec3 out_normal;
out vec2 out_uv;
out float out_depth;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
#ifdef SKINNED_MESH
	mat4 bone_matrix = bone_matrices[bone_ids[0]] * bone_weights[0];
	bone_matrix += bone_matrices[bone_ids[1]] * bone_weights[1];
	bone_matrix += bone_matrices[bone_ids[2]] * bone_weights[2];
	bone_matrix += bone_matrices[bone_ids[3]] * (1.0f - bone_weights[0] - bone_weights[1] - bone_weights[2]);
	vec4 worldPos = world_matrix * bone_matrix * vec4(vertex_position, 1.0);
	out_normal = normalize(invert_view_matrix * world_matrix * bone_matrix * vec4(vertex_normal, 0.0)).xyz;
	out_uv = vertex_uv;
#else

#if defined(INSTANCED) && defined(INSTANCE_SSBO)
	mat4 worldMat = instance_matrices[gl_InstanceID];
#elif defined(INSTANCED)
	mat4 worldMat = mat4(instance_row0, instance_row1, instance_row2, instance_row3);
#else
	mat4 worldMat = world_matrix;
#endif

#ifdef BILLBOARD
	vec3 bbPos;
	vec3 bbNormal;
	vec2 bbUV;
	VegetationBillboard(worldMat, camera_pos, vertex_position, vertex_uv, u_billboard_atlas, u_billboard_up_bias, bbPos, bbNormal, bbUV);
	vec4 worldPos = vec4(bbPos, 1.0);
	out_normal = normalize(invert_view_matrix * vec4(bbNormal, 0.0)).xyz;
	out_uv = bbUV;
#else
	vec4 worldPos = worldMat * vec4(vertex_position, 1.0);
#ifdef WIND
	worldPos.xyz += VegetationWindOffset(vertex_color, worldPos.xyz, u_time, u_wind_params);
#endif
	out_normal = normalize(invert_view_matrix * worldMat * vec4(vertex_normal, 0.0)).xyz;
	out_uv = vertex_uv;
#endif
#endif

	vec4 viewPos = invert_view_matrix * worldPos;
	out_depth = -viewPos.z;

	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 out_normal;
in vec2 out_uv;
in float out_depth;

uniform float camera_far = 10000;

uniform vec3 ambient_color;

uniform sampler2D diffuse_texture;

uniform float ambient_factor = 1;
uniform float diffuse_factor = 1;

// transparency + alpha_test cutoff gate discard: `transparency` is the per-material see-through fraction (1.0 = fully see-through ALPHA_TEST on translucent mask), `u_alpha_cutoff` is the threshold.
uniform float transparency = 0.0;
#ifdef ALPHA_TEST
uniform float u_alpha_cutoff = 0.5;
#endif

#ifdef PBR
// Metallic-roughness material slots (Material::METALLIC_FACTOR /
// ROUGHNESS_FACTOR), bound by name via Shader::BindMaterial.
// roughness_factor < 0 = slot absent (legacy FBX / Lambert material)
// → derive perceptual roughness from the legacy `shininess` slot.
// The mapping r = (2/(n+2))^0.25 was tuned against the old
// Blinn-Phong light-side exponent; with the GGX light shaders it
// lands on a visually close roughness (no longer an exact inverse).
uniform float metallic_factor = 0.0;
uniform float roughness_factor = -1.0;
uniform float shininess = 32.0;
#endif

#ifdef WITH_DBG_OVERLAY
// LOD-debug tint. Reset to vec4(0) each frame the toggle is off
// (PrelightPipeline::DrawUnit) so prior state doesn't leak.
uniform vec4 lod_debug_color = vec4(0.0, 0.0, 0.0, 0.0);
#endif

// normal.xyz, (PBR: roughness)
layout (location = 0) out vec4 rt0;
// diffuse rgb, (PBR: metallic)
layout (location = 1) out vec4 rt1;

void main()
{
	vec4 texel = texture(diffuse_texture, out_uv);
#ifdef ALPHA_TEST
	if (texel.a * (1.0 - transparency) < u_alpha_cutoff)
		discard;
#endif

	// Two-sided foliage: keep the card's geometric normal for BOTH faces
	// (culling is disabled per-draw for these materials in
	// PrelightPipeline::DrawUnit). Flipping to face the camera reads flat
	// at low sun -- every visible leaf lights uniformly and the canopy
	// loses all depth. The unflipped normal gives dark undersides from
	// below and sun-lit undersides (thin-leaf translucency) when backlit.
	vec3 nrm = out_normal;
	rt0.rgb = (nrm + 1) * 0.5;

	vec3 finalDiffuse = texel.rgb * diffuse_factor + ambient_color * ambient_factor;
#ifdef WITH_DBG_OVERLAY
	if (lod_debug_color.a > 0.0)
		finalDiffuse *= lod_debug_color.rgb;
#endif
	rt1.rgb = finalDiffuse;

#ifdef PBR
	float rough = roughness_factor >= 0.0
		? roughness_factor
		: clamp(pow(2.0 / (shininess + 2.0), 0.25), 0.0, 1.0);
	rt0.a = rough;
	rt1.a = metallic_factor;
#else
	rt0.a = 1.0;
	rt1.a = 1.0;
#endif

	gl_FragDepth = out_depth / camera_far;
}

#endif
