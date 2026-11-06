#version 430 core

#include "ClusterCommon.glsl"
#include "ClusterLightCommon.glsl"

#ifdef COMPUTE_SHADER
layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly buffer ClusterLightBuffer { ClusterLight clusterLights[]; };
layout(std430, binding = 1) buffer ClusterCountBuffer { uint clusterCounts[]; };
layout(std430, binding = 2) buffer ClusterIndexBuffer { uint clusterLightIndices[]; };
layout(std430, binding = 3) buffer ClusterOverflowBuffer { uint clusterOverflow[]; };

uniform int u_cluster_light_count;
uniform vec2 u_cluster_near_far;
uniform vec2 u_cluster_tan_half_fov;

void main()
{
	int cluster = int(gl_GlobalInvocationID.x);
	if (cluster >= CLUSTER_COUNT) return;
	uvec3 coord = uvec3(cluster % CLUSTER_TILES_X,
		(cluster / CLUSTER_TILES_X) % CLUSTER_TILES_Y,
		cluster / (CLUSTER_TILES_X * CLUSTER_TILES_Y));
	vec3 boxMin;
	vec3 boxMax;
	ClusterBounds(coord, u_cluster_near_far, u_cluster_tan_half_fov, boxMin, boxMax);

	uint count = 0u;
	uint dropped = 0u;
	for (int i = 0; i < u_cluster_light_count; ++i)
	{
		ClusterLight light = clusterLights[i];
		bool intersects = light.params.w < 1.5
			? ClusterSphereIntersects(light.positionRange.xyz, light.positionRange.w, boxMin, boxMax)
			: ClusterSpotIntersects(light.positionRange.xyz, light.directionCosOuter.xyz,
				light.positionRange.w, light.directionCosOuter.w, boxMin, boxMax);
		if (!intersects) continue;
		if (count < uint(CLUSTER_MAX_LIGHTS))
		{
			clusterLightIndices[cluster * CLUSTER_MAX_LIGHTS + int(count)] = uint(i);
			++count;
		}
		else
			++dropped;
	}
	clusterCounts[cluster] = count;
	if (dropped > 0u) atomicAdd(clusterOverflow[0], dropped);
}
#endif
