#version 330

#include "../GBufferCommon.glsl"

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec3 vertex_normal;

#ifdef SKINNED_MESH
in ivec4 bone_ids;
in vec3 bone_weights;
uniform mat4 bone_matrices[35];
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

#ifdef CLUSTER_BASE_INSTANCE
#extension GL_ARB_shader_draw_parameters : require
struct ClusterInstance
{
	mat4 world;
	vec4 sphere;
	vec4 camera;
	vec4 scaleThreshold;
	uvec4 offsets;
};
struct ClusterVisibleRecord
{
	uint instanceID;
	uint clusterID;
	uint meshID;
	uint batchID;
};
struct Cluster
{
	vec3 center;
	float radius;
	vec3 coneAxis;
	float coneCutoff;
	float error;
	float parentError;
	uint groupIndex;
	uint parentGroupIndex;
	uint firstIndex;
	uint indexCount;
	uint materialIndex;
	uint level;
};
layout(std430, binding = 0) buffer ClusterInstances { ClusterInstance instances[]; };
layout(std430, binding = 1) buffer ClusterVisibleRecords { ClusterVisibleRecord records[]; };
layout(std430, binding = 2) buffer Clusters { Cluster clusters[]; };
#endif

out vec3 out_normal;
out float out_depth;
#ifdef CLUSTER_BASE_INSTANCE
flat out uint out_cluster_id;
flat out uint out_cluster_level;
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
	bone_matrix += bone_matrices[bone_ids[3]] * (1.0f - bone_weights[0] - bone_weights[1] - bone_weights[2]);
	vec4 worldPos = world_matrix * bone_matrix * vec4(vertex_position, 1.0);
	out_normal = normalize(invert_view_matrix * world_matrix * bone_matrix * vec4(vertex_normal, 0.0)).xyz;
#else
#if defined(CLUSTER_BASE_INSTANCE)
	ClusterVisibleRecord clusterRecord = records[gl_BaseInstance];
	out_cluster_id = clusterRecord.clusterID;
	out_cluster_level = clusters[clusterRecord.clusterID].level;
	mat4 worldMat = instances[clusterRecord.instanceID].world;
#elif defined(INSTANCED) && defined(INSTANCE_SSBO)
	mat4 worldMat = instance_matrices[gl_InstanceID];
#elif defined(INSTANCED)
	mat4 worldMat = mat4(instance_row0, instance_row1, instance_row2, instance_row3);
#else
	mat4 worldMat = world_matrix;
#endif
	vec4 worldPos = worldMat * vec4(vertex_position, 1.0);
	out_normal = normalize(invert_view_matrix * worldMat * vec4(vertex_normal, 0.0)).xyz;
#endif
	
	vec4 viewPos = invert_view_matrix * worldPos;
	out_depth = -viewPos.z;
	
	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 out_normal;
in float out_depth;
#ifdef CLUSTER_BASE_INSTANCE
flat in uint out_cluster_id;
flat in uint out_cluster_level;
uniform int cluster_debug_view = 0;
vec3 ClusterLodColor(uint level)
{
	if (level == 0u) return vec3(0.15, 0.85, 0.25);
	if (level == 1u) return vec3(0.15, 0.45, 0.95);
	if (level == 2u) return vec3(0.95, 0.75, 0.15);
	if (level == 3u) return vec3(0.9, 0.25, 0.2);
	return vec3(0.8, 0.2, 0.85);
}
vec3 ClusterIdColor(uint id)
{
	uint h = id * 1664525u + 1013904223u;
	h ^= h >> 16u;
	return vec3(float(h & 255u), float((h >> 8u) & 255u), float((h >> 16u) & 255u)) / 255.0;
}
#endif

uniform float camera_far = 10000;

uniform vec3 ambient_color;
uniform vec3 diffuse_color;

uniform float ambient_factor = 1;
uniform float diffuse_factor = 1;

// Color-only path: alpha-test still gates discard against (1 - transparency). See GBuffer.glsl for variable semantics.
uniform float transparency = 0.0;
#ifdef ALPHA_TEST
uniform float u_alpha_cutoff = 0.5;
#endif

#ifdef PBR
// See GBuffer.glsl — same metallic-roughness packing for
// color-only materials.
uniform float metallic_factor = 0.0;
uniform float roughness_factor = -1.0;
uniform float shininess = 32.0;
#endif

#ifdef WITH_EDITOR
// Editor LOD-debug tint. See GBuffer.glsl.
uniform vec4 lod_debug_color = vec4(0.0, 0.0, 0.0, 0.0);
#endif

// normal.xyz, (PBR: roughness)
layout (location = 0) out vec4 rt0;
// diffuse rgb, (PBR: metallic)
layout (location = 1) out vec4 rt1;

void main()
{
#ifdef ALPHA_TEST
	if ((1.0 - transparency) < u_alpha_cutoff)
		discard;
#endif

	vec3 nrm = out_normal;
#ifdef TWO_SIDED
	// Back faces: flip the normal before packing (see GBuffer.glsl).
	if (!gl_FrontFacing)
		nrm = -nrm;
#endif

	vec3 finalDiffuse = diffuse_color.rgb * diffuse_factor + ambient_color * ambient_factor;
#ifdef WITH_EDITOR
	if (lod_debug_color.a > 0.0)
		finalDiffuse *= lod_debug_color.rgb;
#endif
#ifdef CLUSTER_BASE_INSTANCE
	if (cluster_debug_view == 1)
		finalDiffuse = ClusterLodColor(out_cluster_level);
	else if (cluster_debug_view == 2)
		finalDiffuse = ClusterIdColor(out_cluster_id);
#endif

#ifdef PBR
	float rough = roughness_factor >= 0.0
		? roughness_factor
		: clamp(pow(2.0 / (shininess + 2.0), 0.25), 0.0, 1.0);
	rt0 = GBufferPackNormal(nrm, rough);
	rt1 = GBufferPackDiffuse(finalDiffuse, metallic_factor);
#else
	rt0 = GBufferPackNormal(nrm, 1.0);
	rt1 = GBufferPackDiffuse(finalDiffuse, 1.0);
#endif

	gl_FragDepth = GBufferLinearDepth(out_depth, camera_far);
}

#endif