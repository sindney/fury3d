#ifndef _FURY_CLUSTER_SHADER_SOURCES_H_
#define _FURY_CLUSTER_SHADER_SOURCES_H_

namespace fury
{
	inline const char *ClusterSelectShaderSource = R"GLSL(#version 430 core
layout(local_size_x = 128) in;
struct ClusterInstance
{
	mat4 world;
	vec4 sphere;
	vec4 camera;
	vec4 scaleThreshold;
	uvec4 offsets;
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
struct Group
{
	vec3 center;
	float radius;
	float localError;
	float error;
	float parentError;
	uint parentGroupIndex;
	uint inputClusterOffset;
	uint inputClusterCount;
	uint outputClusterOffset;
	uint outputClusterCount;
	uint childGroupOffset;
	uint childGroupCount;
	uint level;
	uint padding;
};
layout(std430, binding = 0) buffer ClusterInstances { ClusterInstance instances[]; };
layout(std430, binding = 1) buffer Clusters { Cluster clusters[]; };
layout(std430, binding = 2) buffer Groups { Group groups[]; };
layout(std430, binding = 3) buffer SelectedCounts { uint selectedCounts[]; };
layout(std430, binding = 4) buffer SelectedPairs { uvec2 selectedPairs[]; };
layout(std430, binding = 5) buffer FrustumPlanes { vec4 frustumPlanes[6]; };
layout(std430, binding = 10) buffer ClusterStats { uint stats[16]; };
shared bool instanceVisible;
bool OutsideFrustum(vec3 center, float radius)
{
	for (int i = 0; i < 6; ++i)
		if (dot(frustumPlanes[i].xyz, center) + frustumPlanes[i].w < -radius)
			return true;
	return false;
}
float ConservativeDistance(vec3 center, float radius)
{
	return max(distance(instances[gl_WorkGroupID.x].camera.xyz, center) - radius, 0.001);
}
vec3 WorldCenter(vec3 center)
{
	return (instances[gl_WorkGroupID.x].world * vec4(center, 1.0)).xyz;
}
void main()
{
	const uint instanceID = gl_WorkGroupID.x;
	const ClusterInstance instance = instances[instanceID];
	if (gl_LocalInvocationID.x == 0)
	{
		instanceVisible = !OutsideFrustum(instance.sphere.xyz, instance.sphere.w);
		if (instanceVisible)
			atomicAdd(stats[0], 1u);
	}
	barrier();
	if (!instanceVisible)
		return;
	const uint clusterOffset = instance.offsets.x;
	const uint clusterCount = instance.offsets.y;
	const uint selectedOffset = instance.offsets.z;
	const uint leafCount = instance.offsets.w;
	const float scale = instance.scaleThreshold.x;
	const float projectionScale = instance.camera.w;
	const float threshold = instance.scaleThreshold.y;
	for (uint clusterID = gl_LocalInvocationID.x; clusterID < clusterCount; clusterID += 128)
	{
		const Cluster cluster = clusters[clusterOffset + clusterID];
		vec3 selfCenter = cluster.center;
		float selfRadius = cluster.radius;
		if (cluster.groupIndex != 0xffffffffu)
		{
			selfCenter = groups[cluster.groupIndex].center;
			selfRadius = groups[cluster.groupIndex].radius;
		}
		const float selfDistance = ConservativeDistance(WorldCenter(selfCenter), selfRadius * scale);
		const float selfError = cluster.error * scale * projectionScale / selfDistance;
		float parentError = 1e30;
		if (cluster.parentGroupIndex != 0xffffffffu)
		{
			const Group parent = groups[cluster.parentGroupIndex];
			const float parentDistance = ConservativeDistance(WorldCenter(parent.center), parent.radius * scale);
			parentError = cluster.parentError * scale * projectionScale / parentDistance;
		}
		if (selfError <= threshold && parentError > threshold)
		{
			const uint slot = atomicAdd(selectedCounts[instanceID], 1u);
			if (slot < leafCount)
			{
				selectedPairs[selectedOffset + slot] = uvec2(instanceID, clusterOffset + clusterID);
				atomicAdd(stats[1], 1u);
			}
			else
			{
				atomicAdd(stats[4], 1u);
			}
		}
	}
}
)GLSL";

	inline const char *ClusterCullShaderSource = R"GLSL(#version 430 core
layout(local_size_x = 128) in;
struct ClusterInstance
{
	mat4 world;
	vec4 sphere;
	vec4 camera;
	vec4 scaleThreshold;
	uvec4 offsets;
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
struct VisibleRecord
{
	uint instanceID;
	uint clusterID;
	uint meshID;
	uint batchID;
};
struct IndirectArgs
{
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int vertexOffset;
	uint firstInstance;
};
layout(std430, binding = 0) buffer ClusterInstances { ClusterInstance instances[]; };
layout(std430, binding = 1) buffer Clusters { Cluster clusters[]; };
layout(std430, binding = 3) buffer SelectedCounts { uint selectedCounts[]; };
layout(std430, binding = 4) buffer SelectedPairs { uvec2 selectedPairs[]; };
layout(std430, binding = 5) buffer FrustumPlanes { vec4 frustumPlanes[6]; };
layout(std430, binding = 6) buffer ClusterBatches { uvec4 batches[][2]; };
layout(std430, binding = 7) buffer ClusterVisibleRecords { VisibleRecord visibleRecords[]; };
layout(std430, binding = 8) buffer IndirectArguments { IndirectArgs arguments[]; };
layout(std430, binding = 9) buffer IndirectCounts { uint counts[]; };
layout(std430, binding = 10) buffer ClusterStats { uint stats[16]; };
layout(std430, binding = 11) buffer InstanceMaterials { uint instanceMaterials[]; };
layout(std430, binding = 12) buffer ClusterLimits { uint maxDrawIndirectCount; };
bool OutsideFrustum(vec3 center, float radius)
{
	for (int i = 0; i < 6; ++i)
		if (dot(frustumPlanes[i].xyz, center) + frustumPlanes[i].w < -radius)
			return true;
	return false;
}
void main()
{
	const uvec4 batchA = batches[gl_WorkGroupID.x][0];
	const uvec4 batchB = batches[gl_WorkGroupID.x][1];
	const uint clusterOffset = batchA.x;
	const uint materialIndex = batchA.y;
	const uint visibleOffset = batchA.z;
	const uint argumentOffset = batchA.w;
	const uint countOffset = batchB.x;
	const uint instanceCount = batchB.y;
	const uint capacity = batchB.z;
	const uint materialID = batchB.w;
	for (uint instanceID = 0; instanceID < instanceCount; ++instanceID)
	{
		const ClusterInstance instance = instances[instanceID];
		if (instance.offsets.x != clusterOffset || instanceMaterials[instanceID * 8u + materialIndex] != materialID)
			continue;
		const uint selectedOffset = instance.offsets.z;
		const uint selectedCount = min(selectedCounts[instanceID], instance.offsets.w);
		for (uint selectedID = gl_LocalInvocationID.x; selectedID < selectedCount; selectedID += 128)
		{
			const uvec2 pair = selectedPairs[selectedOffset + selectedID];
			const Cluster cluster = clusters[pair.y];
			if (cluster.materialIndex != materialIndex)
				continue;
			const vec3 center = (instance.world * vec4(cluster.center, 1.0)).xyz;
			const float radius = cluster.radius * instance.scaleThreshold.x;
			if (OutsideFrustum(center, radius))
				continue;
			if (instance.scaleThreshold.z != 0u && cluster.coneCutoff > -1.0)
			{
				const vec3 axis = normalize((instance.world * vec4(cluster.coneAxis, 0.0)).xyz);
				if (dot(center - instance.camera.xyz, axis) >= cluster.coneCutoff * length(center - instance.camera.xyz) + radius)
					continue;
			}
			const uint slot = atomicAdd(counts[countOffset], 1u);
			if (slot < capacity)
			{
				visibleRecords[visibleOffset + slot] = VisibleRecord(instanceID, pair.y, clusterOffset, gl_WorkGroupID.x);
				atomicAdd(stats[2], 1u);
				atomicAdd(stats[6], cluster.indexCount / 3u);
			}
			else
			{
				atomicAdd(stats[5], 1u);
			}
		}
	}
	barrier();
	if (gl_LocalInvocationID.x == 0)
	{
		const uint visibleCount = min(counts[countOffset], capacity);
		for (uint chunk = 0; chunk * maxDrawIndirectCount < capacity; ++chunk)
		{
			const uint start = chunk * maxDrawIndirectCount;
			counts[countOffset + chunk] = visibleCount > start
				? min(visibleCount - start, maxDrawIndirectCount) : 0u;
		}
		for (uint command = 0; command < visibleCount; ++command)
		{
			const VisibleRecord record = visibleRecords[visibleOffset + command];
			const Cluster cluster = clusters[record.clusterID];
			arguments[argumentOffset + command] = IndirectArgs(cluster.indexCount, 1u,
				cluster.firstIndex, 0, visibleOffset + command);
		}
		for (uint command = visibleCount; command < capacity; ++command)
			arguments[argumentOffset + command] = IndirectArgs(0u, 0u, 0u, 0, 0u);
		atomicAdd(stats[3], visibleCount);
	}
}
)GLSL";
}

#endif // _FURY_CLUSTER_SHADER_SOURCES_H_
