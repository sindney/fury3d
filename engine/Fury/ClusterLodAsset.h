#ifndef _FURY_CLUSTERLODASSET_H_
#define _FURY_CLUSTERLODASSET_H_

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "Fury/Macros.h"

namespace fury
{
	constexpr std::uint32_t ClusterLodInvalidIndex = std::numeric_limits<std::uint32_t>::max();
	constexpr std::uint32_t ClusterLodDescriptorSchemaVersion = 1;
	constexpr float ClusterLodInfiniteError = std::numeric_limits<float>::infinity();

	struct ClusterLodBuildOptions
	{
		std::uint32_t cluster_triangle_limit = 128;
		std::uint32_t cluster_vertex_limit = 128;
		std::uint32_t group_cluster_limit = 8;
		float reduction_ratio = 0.5f;
		float target_error = 0.05f;
	};

	struct ClusterLodDescriptor
	{
		std::uint32_t schema_version = ClusterLodDescriptorSchemaVersion;
		std::uint64_t content_hash = 0;
		ClusterLodBuildOptions options;
		bool enabled = false;
	};

	struct ClusterLodSphere
	{
		float center[3] = { 0.0f, 0.0f, 0.0f };
		float radius = 0.0f;
	};

	struct ClusterLodNormalCone
	{
		float axis[3] = { 0.0f, 0.0f, 0.0f };
		float cutoff = -1.0f;
	};

	struct ClusterLodCluster
	{
		std::uint32_t first_index = 0;
		std::uint32_t index_count = 0;
		std::uint32_t vertex_count = 0;
		std::uint32_t material_index = 0;
		std::uint32_t group_index = ClusterLodInvalidIndex;
		std::uint32_t parent_group_index = ClusterLodInvalidIndex;
		std::uint32_t level = 0;
		ClusterLodSphere bounds;
		ClusterLodNormalCone normal_cone;
		float error = 0.0f;
		float parent_error = ClusterLodInfiniteError;
	};

	struct ClusterLodGroup
	{
		std::uint32_t input_cluster_offset = 0;
		std::uint32_t input_cluster_count = 0;
		std::uint32_t output_cluster_offset = 0;
		std::uint32_t output_cluster_count = 0;
		std::uint32_t child_group_offset = 0;
		std::uint32_t child_group_count = 0;
		std::uint32_t parent_group_index = ClusterLodInvalidIndex;
		std::uint32_t level = 0;
		ClusterLodSphere bounds;
		float local_error = 0.0f;
		float error = 0.0f;
		float parent_error = ClusterLodInfiniteError;
	};

	struct alignas(16) ClusterLodGpuCluster
	{
		float center[3] = { 0.0f, 0.0f, 0.0f };
		float radius = 0.0f;
		float cone_axis[3] = { 0.0f, 0.0f, 0.0f };
		float cone_cutoff = -1.0f;
		float error = 0.0f;
		float parent_error = ClusterLodInfiniteError;
		std::uint32_t group_index = ClusterLodInvalidIndex;
		std::uint32_t parent_group_index = ClusterLodInvalidIndex;
		std::uint32_t first_index = 0;
		std::uint32_t index_count = 0;
		std::uint32_t material_index = 0;
		std::uint32_t level = 0;
	};

	struct alignas(16) ClusterLodGpuGroup
	{
		float center[3] = { 0.0f, 0.0f, 0.0f };
		float radius = 0.0f;
		float local_error = 0.0f;
		float error = 0.0f;
		float parent_error = ClusterLodInfiniteError;
		std::uint32_t parent_group_index = ClusterLodInvalidIndex;
		std::uint32_t input_cluster_offset = 0;
		std::uint32_t input_cluster_count = 0;
		std::uint32_t output_cluster_offset = 0;
		std::uint32_t output_cluster_count = 0;
		std::uint32_t child_group_offset = 0;
		std::uint32_t child_group_count = 0;
		std::uint32_t level = 0;
		std::uint32_t padding = 0;
	};

	class FURY_API ClusterLodAsset
	{
	public:

		ClusterLodBuildOptions options;
		std::shared_ptr<const std::vector<std::uint32_t>> merged_indices;
		std::vector<ClusterLodCluster> clusters;
		std::vector<ClusterLodGroup> groups;
		std::vector<std::uint32_t> group_input_clusters;
		std::vector<std::uint32_t> group_children;
		std::vector<ClusterLodGpuCluster> gpu_clusters;
		std::vector<ClusterLodGpuGroup> gpu_groups;

		ClusterLodAsset();

		void SetMergedIndices(std::vector<std::uint32_t> indices);

		void RebuildGpuRecords();
	};

	FURY_API bool ClusterLodContains(const ClusterLodSphere &outer, const ClusterLodSphere &inner,
		float epsilon = 1e-4f);

	FURY_API ClusterLodSphere ClusterLodMerge(const ClusterLodSphere &a, const ClusterLodSphere &b);

	FURY_API ClusterLodSphere ClusterLodExpand(const ClusterLodSphere &bounds, float error_radius);
}

#endif // _FURY_CLUSTERLODASSET_H_
