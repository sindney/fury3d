#include "Fury/ClusterLodAsset.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace fury
{
	namespace
	{
		float Distance(const float a[3], const float b[3])
		{
			const float x = b[0] - a[0];
			const float y = b[1] - a[1];
			const float z = b[2] - a[2];
			return std::sqrt(x * x + y * y + z * z);
		}

		bool IsValid(const ClusterLodSphere &sphere)
		{
			return sphere.radius >= 0.0f && std::isfinite(sphere.radius)
				&& std::isfinite(sphere.center[0]) && std::isfinite(sphere.center[1])
				&& std::isfinite(sphere.center[2]);
		}
	}

	ClusterLodAsset::ClusterLodAsset()
		: merged_indices(std::make_shared<const std::vector<std::uint32_t>>())
	{
	}

	void ClusterLodAsset::SetMergedIndices(std::vector<std::uint32_t> indices)
	{
		merged_indices = std::make_shared<const std::vector<std::uint32_t>>(std::move(indices));
	}

	void ClusterLodAsset::RebuildGpuRecords()
	{
		gpu_clusters.resize(clusters.size());
		for (std::size_t i = 0; i < clusters.size(); ++i)
		{
			const ClusterLodCluster &source = clusters[i];
			ClusterLodGpuCluster &target = gpu_clusters[i];
			std::copy(std::begin(source.bounds.center), std::end(source.bounds.center), target.center);
			target.radius = source.bounds.radius;
			std::copy(std::begin(source.normal_cone.axis), std::end(source.normal_cone.axis), target.cone_axis);
			target.cone_cutoff = source.normal_cone.cutoff;
			target.error = source.error;
			target.parent_error = source.parent_error;
			target.group_index = source.group_index;
			target.parent_group_index = source.parent_group_index;
			target.first_index = source.first_index;
			target.index_count = source.index_count;
			target.material_index = source.material_index;
			target.level = source.level;
		}

		gpu_groups.resize(groups.size());
		for (std::size_t i = 0; i < groups.size(); ++i)
		{
			const ClusterLodGroup &source = groups[i];
			ClusterLodGpuGroup &target = gpu_groups[i];
			std::copy(std::begin(source.bounds.center), std::end(source.bounds.center), target.center);
			target.radius = source.bounds.radius;
			target.local_error = source.local_error;
			target.error = source.error;
			target.parent_error = source.parent_error;
			target.parent_group_index = source.parent_group_index;
			target.input_cluster_offset = source.input_cluster_offset;
			target.input_cluster_count = source.input_cluster_count;
			target.output_cluster_offset = source.output_cluster_offset;
			target.output_cluster_count = source.output_cluster_count;
			target.child_group_offset = source.child_group_offset;
			target.child_group_count = source.child_group_count;
			target.level = source.level;
			target.padding = 0;
		}
	}

	bool ClusterLodContains(const ClusterLodSphere &outer, const ClusterLodSphere &inner, float epsilon)
	{
		if (!IsValid(outer) || !IsValid(inner) || epsilon < 0.0f || !std::isfinite(epsilon))
			return false;
		return Distance(outer.center, inner.center) + inner.radius
			<= outer.radius + epsilon * std::max({ 1.0f, outer.radius, inner.radius });
	}

	ClusterLodSphere ClusterLodMerge(const ClusterLodSphere &a, const ClusterLodSphere &b)
	{
		const float distance = Distance(a.center, b.center);
		if (a.radius >= distance + b.radius)
			return a;
		if (b.radius >= distance + a.radius)
			return b;
		if (distance == 0.0f)
			return a.radius >= b.radius ? a : b;

		ClusterLodSphere result;
		result.radius = (distance + a.radius + b.radius) * 0.5f;
		const float offset = (result.radius - a.radius) / distance;
		for (int i = 0; i < 3; ++i)
			result.center[i] = a.center[i] + (b.center[i] - a.center[i]) * offset;
		result.radius += std::max(1e-5f, result.radius * 1e-4f);
		return result;
	}

	ClusterLodSphere ClusterLodExpand(const ClusterLodSphere &bounds, float error_radius)
	{
		ClusterLodSphere result = bounds;
		result.radius += std::max(0.0f, error_radius);
		return result;
	}
}
