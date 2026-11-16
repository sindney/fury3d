#include "Fury/ClusterLodValidation.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "Fury/ClusterLodSelection.h"

namespace fury
{
	namespace
	{
		bool Fail(std::string &error, const std::string &message)
		{
			error = message;
			return false;
		}

		bool ValidBounds(const ClusterLodSphere &bounds)
		{
			return std::isfinite(bounds.radius) && bounds.radius >= 0.0f
				&& std::isfinite(bounds.center[0]) && std::isfinite(bounds.center[1])
				&& std::isfinite(bounds.center[2]);
		}

		bool ValidCone(const ClusterLodNormalCone &cone)
		{
			return std::isfinite(cone.cutoff) && cone.cutoff >= -1.0f && cone.cutoff <= 1.0f
				&& std::isfinite(cone.axis[0]) && std::isfinite(cone.axis[1])
				&& std::isfinite(cone.axis[2]);
		}

		bool ValidateSamples(const ClusterLodAsset &asset, std::string &error)
		{
			ClusterLodSphere root_bounds;
			bool has_bounds = false;
			for (const ClusterLodCluster &cluster : asset.clusters)
			{
				if (cluster.level != 0)
					continue;
				root_bounds = has_bounds ? ClusterLodMerge(root_bounds, cluster.bounds) : cluster.bounds;
				has_bounds = true;
			}
			if (!has_bounds)
				return Fail(error, "asset has no LOD-zero bounds");

			const float thresholds[] = { 0.0f, 0.001f, 0.01f, 0.1f, 1.0f, 10.0f, 100.0f, 1e6f };
			const float directions[][3] = {
				{ 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
				{ 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
			};
			for (const auto &direction : directions)
			{
				for (float distance_scale : { 2.0f, 10.0f })
				{
					ClusterLodSelectionParams params;
					const float distance = root_bounds.radius * distance_scale + 1.0f;
					for (int axis = 0; axis < 3; ++axis)
						params.camera_position[axis] = root_bounds.center[axis] + direction[axis] * distance;
					params.projection_scale = 100.0f;
					for (float threshold : thresholds)
					{
						params.error_threshold = threshold;
						const auto selected = SelectClusterLodCut(asset, params);
						if (!ValidateClusterLodCut(asset, selected, error))
						{
							error = "sampled cut failed: " + error;
							return false;
						}
					}
				}
			}
			return true;
		}
	}

	bool ValidateClusterLodCut(const ClusterLodAsset &asset,
		const std::vector<std::uint32_t> &selected, std::string &error)
	{
		if (asset.clusters.empty())
			return Fail(error, "cut has no source clusters");
		std::vector<bool> selected_clusters(asset.clusters.size(), false);
		std::vector<std::uint32_t> selected_group_counts(asset.groups.size(), 0);
		for (std::uint32_t cluster_index : selected)
		{
			if (cluster_index >= asset.clusters.size())
				return Fail(error, "selected cluster is out of range");
			if (selected_clusters[cluster_index])
				return Fail(error, "selected cluster appears more than once");
			selected_clusters[cluster_index] = true;
			const std::uint32_t group_index = asset.clusters[cluster_index].group_index;
			if (group_index != ClusterLodInvalidIndex)
			{
				if (group_index >= asset.groups.size())
					return Fail(error, "selected cluster has an invalid group");
				++selected_group_counts[group_index];
			}
		}
		for (std::size_t i = 0; i < asset.groups.size(); ++i)
		{
			if (selected_group_counts[i] != 0
				&& selected_group_counts[i] != asset.groups[i].output_cluster_count)
			{
				return Fail(error, "group " + std::to_string(i) + " is only partially selected");
			}
		}

		for (std::size_t leaf_index = 0; leaf_index < asset.clusters.size(); ++leaf_index)
		{
			const ClusterLodCluster &leaf = asset.clusters[leaf_index];
			if (leaf.level != 0)
				continue;
			std::size_t path_selections = selected_clusters[leaf_index] ? 1 : 0;
			std::uint32_t group_index = leaf.parent_group_index;
			std::size_t depth = 0;
			while (group_index != ClusterLodInvalidIndex)
			{
				if (group_index >= asset.groups.size())
					return Fail(error, "leaf has an invalid parent chain");
				if (selected_group_counts[group_index] != 0)
					++path_selections;
				group_index = asset.groups[group_index].parent_group_index;
				if (++depth > asset.groups.size())
					return Fail(error, "leaf parent chain contains a cycle");
			}
			if (path_selections != 1)
			{
				return Fail(error, "leaf " + std::to_string(leaf_index) + " has "
					+ std::to_string(path_selections) + " selected nodes on its root path");
			}
		}
		return true;
	}

	bool ValidateClusterLodAsset(const ClusterLodAsset &asset,
		std::size_t vertex_count, std::string &error)
	{
		if (!asset.merged_indices || asset.merged_indices->empty())
			return Fail(error, "merged index allocation is empty");
		if (asset.clusters.empty())
			return Fail(error, "cluster array is empty");
		if (asset.gpu_clusters.size() != asset.clusters.size()
			|| asset.gpu_groups.size() != asset.groups.size())
			return Fail(error, "GPU record arrays are stale");

		std::vector<std::uint32_t> input_owners(asset.clusters.size(), ClusterLodInvalidIndex);
		std::vector<std::uint32_t> output_owners(asset.clusters.size(), ClusterLodInvalidIndex);
		for (std::size_t i = 0; i < asset.clusters.size(); ++i)
		{
			const ClusterLodCluster &cluster = asset.clusters[i];
			if (cluster.index_count == 0 || cluster.index_count % 3 != 0
				|| cluster.index_count > asset.options.cluster_triangle_limit * 3)
				return Fail(error, "cluster " + std::to_string(i) + " has an invalid index count");
			if (cluster.first_index >= asset.merged_indices->size()
				|| cluster.index_count > asset.merged_indices->size() - cluster.first_index)
				return Fail(error, "cluster " + std::to_string(i) + " has an invalid index range");
			if (cluster.vertex_count == 0 || cluster.vertex_count > asset.options.cluster_vertex_limit)
				return Fail(error, "cluster " + std::to_string(i) + " exceeds the vertex limit");
			if (!ValidBounds(cluster.bounds) || !ValidCone(cluster.normal_cone))
				return Fail(error, "cluster " + std::to_string(i) + " has invalid bounds or cone metadata");
			if (!std::isfinite(cluster.error) || cluster.error < 0.0f)
				return Fail(error, "cluster " + std::to_string(i) + " has an invalid selection error");
			if (cluster.level == 0 && (cluster.group_index != ClusterLodInvalidIndex || cluster.error != 0.0f))
				return Fail(error, "leaf " + std::to_string(i) + " has a producing group or nonzero error");
			if (cluster.level != 0 && cluster.group_index >= asset.groups.size())
				return Fail(error, "cluster " + std::to_string(i) + " has an invalid producing group");
			if (cluster.parent_group_index == ClusterLodInvalidIndex)
			{
				if (!std::isinf(cluster.parent_error))
					return Fail(error, "root cluster " + std::to_string(i) + " lacks the infinite sentinel");
			}
			else
			{
				if (cluster.parent_group_index >= asset.groups.size()
					|| !std::isfinite(cluster.parent_error)
					|| cluster.parent_error < cluster.error)
					return Fail(error, "cluster " + std::to_string(i) + " has an invalid parent error");
			}
			for (std::uint32_t j = 0; j < cluster.index_count; ++j)
			{
				if ((*asset.merged_indices)[cluster.first_index + j] >= vertex_count)
					return Fail(error, "cluster " + std::to_string(i) + " references an invalid vertex");
			}
		}

		for (std::size_t i = 0; i < asset.groups.size(); ++i)
		{
			const ClusterLodGroup &group = asset.groups[i];
			if (group.level == 0 || group.input_cluster_count == 0 || group.output_cluster_count == 0)
				return Fail(error, "group " + std::to_string(i) + " has an invalid level or empty range");
			if (group.input_cluster_offset > asset.group_input_clusters.size()
				|| group.input_cluster_count > asset.group_input_clusters.size() - group.input_cluster_offset
				|| group.output_cluster_offset > asset.clusters.size()
				|| group.output_cluster_count > asset.clusters.size() - group.output_cluster_offset
				|| group.child_group_offset > asset.group_children.size()
				|| group.child_group_count > asset.group_children.size() - group.child_group_offset)
				return Fail(error, "group " + std::to_string(i) + " has an out-of-range record span");
			if (!ValidBounds(group.bounds) || !std::isfinite(group.local_error)
				|| !std::isfinite(group.error) || group.local_error < 0.0f
				|| group.error < group.local_error)
				return Fail(error, "group " + std::to_string(i) + " has invalid error or bounds metadata");
			if (group.parent_group_index == ClusterLodInvalidIndex)
			{
				if (!std::isinf(group.parent_error))
					return Fail(error, "root group " + std::to_string(i) + " lacks the infinite sentinel");
			}
			else if (group.parent_group_index >= asset.groups.size()
				|| asset.groups[group.parent_group_index].level <= group.level
				|| group.parent_error != asset.groups[group.parent_group_index].error)
			{
				return Fail(error, "group " + std::to_string(i) + " has an invalid parent link");
			}

			const std::uint32_t material = asset.clusters[group.output_cluster_offset].material_index;
			for (std::uint32_t j = 0; j < group.input_cluster_count; ++j)
			{
				const std::uint32_t cluster_index = asset.group_input_clusters[group.input_cluster_offset + j];
				if (cluster_index >= asset.clusters.size() || input_owners[cluster_index] != ClusterLodInvalidIndex)
					return Fail(error, "group " + std::to_string(i) + " has a duplicate or invalid input");
				input_owners[cluster_index] = static_cast<std::uint32_t>(i);
				const ClusterLodCluster &input = asset.clusters[cluster_index];
				if (input.parent_group_index != i || input.parent_error != group.error
					|| input.error > group.error || input.material_index != material
					|| !ClusterLodContains(group.bounds, input.bounds))
					return Fail(error, "group " + std::to_string(i) + " violates an input invariant");
			}
			for (std::uint32_t j = 0; j < group.output_cluster_count; ++j)
			{
				const std::uint32_t cluster_index = group.output_cluster_offset + j;
				if (output_owners[cluster_index] != ClusterLodInvalidIndex)
					return Fail(error, "group " + std::to_string(i) + " has a duplicate output");
				output_owners[cluster_index] = static_cast<std::uint32_t>(i);
				const ClusterLodCluster &output = asset.clusters[cluster_index];
				if (output.group_index != i)
					return Fail(error, "group " + std::to_string(i) + " has an invalid output owner");
				if (output.level != group.level)
					return Fail(error, "group " + std::to_string(i) + " has an invalid output level");
				if (output.error != group.error)
					return Fail(error, "group " + std::to_string(i) + " has an invalid output error");
				if (output.material_index != material)
					return Fail(error, "group " + std::to_string(i) + " crosses material boundaries");
				const ClusterLodSphere expanded = ClusterLodExpand(output.bounds, group.local_error);
				if (!ClusterLodContains(group.bounds, expanded))
				{
					const float x = expanded.center[0] - group.bounds.center[0];
					const float y = expanded.center[1] - group.bounds.center[1];
					const float z = expanded.center[2] - group.bounds.center[2];
					const float distance = std::sqrt(x * x + y * y + z * z);
					return Fail(error, "group " + std::to_string(i) + " output " + std::to_string(cluster_index)
						+ " escapes bounds: distance=" + std::to_string(distance)
						+ " outer_radius=" + std::to_string(group.bounds.radius)
						+ " expanded_radius=" + std::to_string(expanded.radius)
						+ " local_error=" + std::to_string(group.local_error));
				}
			}
			std::set<std::uint32_t> children;
			for (std::uint32_t j = 0; j < group.child_group_count; ++j)
			{
				const std::uint32_t child = asset.group_children[group.child_group_offset + j];
				if (child >= asset.groups.size() || !children.insert(child).second
					|| asset.groups[child].parent_group_index != i
					|| asset.groups[child].level >= group.level)
					return Fail(error, "group " + std::to_string(i) + " has an invalid child link");
				for (std::uint32_t k = 0; k < asset.groups[child].output_cluster_count; ++k)
				{
					const std::uint32_t cluster_index = asset.groups[child].output_cluster_offset + k;
					if (input_owners[cluster_index] != i)
						return Fail(error, "child group " + std::to_string(child) + " is split across parents");
				}
			}
		}

		for (std::size_t i = 0; i < asset.clusters.size(); ++i)
		{
			const ClusterLodCluster &cluster = asset.clusters[i];
			if (cluster.parent_group_index != ClusterLodInvalidIndex
				&& input_owners[i] != cluster.parent_group_index)
				return Fail(error, "cluster " + std::to_string(i) + " is missing from its parent inputs");
			if (cluster.level == 0 && output_owners[i] != ClusterLodInvalidIndex)
				return Fail(error, "leaf " + std::to_string(i) + " has a producing output range");
			if (cluster.level != 0 && output_owners[i] != cluster.group_index)
				return Fail(error, "cluster " + std::to_string(i) + " is missing from its group outputs");
		}

		std::vector<unsigned char> state(asset.groups.size(), 0);
		for (std::size_t i = 0; i < asset.groups.size(); ++i)
		{
			std::vector<std::uint32_t> path;
			std::uint32_t current = static_cast<std::uint32_t>(i);
			while (current != ClusterLodInvalidIndex && state[current] != 2)
			{
				if (state[current] == 1)
					return Fail(error, "group DAG contains a cycle");
				state[current] = 1;
				path.push_back(current);
				current = asset.groups[current].parent_group_index;
			}
			for (std::uint32_t group : path)
				state[group] = 2;
		}
		return ValidateSamples(asset, error);
	}
}
