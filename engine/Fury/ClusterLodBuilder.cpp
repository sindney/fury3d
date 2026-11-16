#include "Fury/ClusterLodBuilder.h"

#include "Fury/ClusterLodValidation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#ifndef MESHOPTIMIZER_API
#define MESHOPTIMIZER_API
#endif
#include "meshoptimizer.h"

namespace fury
{
	namespace
	{
		struct WorkingUnit
		{
			std::vector<std::uint32_t> clusters;
			std::uint32_t group_index = ClusterLodInvalidIndex;
			std::uint32_t material_index = 0;
		};

		ClusterLodBuildResult Failure(const std::string &message)
		{
			ClusterLodBuildResult result;
			result.error = message;
			return result;
		}

		bool ValidateOptions(const ClusterLodBuildOptions &options, std::string &error)
		{
			if (options.cluster_triangle_limit == 0 || options.cluster_triangle_limit > 512)
			{
				error = "cluster triangle limit must be in [1, 512]";
				return false;
			}
			if (options.cluster_vertex_limit < 3 || options.cluster_vertex_limit > 128)
			{
				error = "cluster vertex limit must be in [3, 128]";
				return false;
			}
			if (options.group_cluster_limit < 2 || options.group_cluster_limit > 64)
			{
				error = "group cluster limit must be in [2, 64]";
				return false;
			}
			if (!(options.reduction_ratio > 0.0f && options.reduction_ratio < 1.0f))
			{
				error = "reduction ratio must be in (0, 1)";
				return false;
			}
			if (options.target_error < 0.0f || options.target_error > 1.0f
				|| !std::isfinite(options.target_error))
			{
				error = "target error must be in [0, 1]";
				return false;
			}
			return true;
		}

		bool AppendMeshlets(const ClusterLodMeshView &mesh, const ClusterLodBuildOptions &options,
			const unsigned int *indices, std::size_t index_count, std::uint32_t material_index,
			std::uint32_t level, std::uint32_t group_index, float error,
			ClusterLodAsset &asset, std::vector<std::uint32_t> &merged_indices,
			std::string &failure)
		{
			if (index_count == 0 || index_count % 3 != 0
				|| merged_indices.size() > std::numeric_limits<std::uint32_t>::max()
				|| index_count > std::numeric_limits<std::uint32_t>::max() - merged_indices.size())
			{
				failure = "invalid or oversized cluster index range";
				return false;
			}
			const std::size_t meshlet_bound = meshopt_buildMeshletsBound(index_count,
				options.cluster_vertex_limit, options.cluster_triangle_limit);
			if (meshlet_bound == 0
				|| meshlet_bound > std::numeric_limits<std::size_t>::max() / options.cluster_vertex_limit
				|| meshlet_bound > std::numeric_limits<std::size_t>::max()
					/ (options.cluster_triangle_limit * 3))
			{
				failure = "meshlet allocation size overflow";
				return false;
			}

			std::vector<meshopt_Meshlet> meshlets(meshlet_bound);
			std::vector<unsigned int> meshlet_vertices(meshlet_bound * options.cluster_vertex_limit);
			std::vector<unsigned char> meshlet_triangles(meshlet_bound * options.cluster_triangle_limit * 3);
			const std::size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(),
				meshlet_vertices.data(), meshlet_triangles.data(), indices, index_count,
				mesh.positions, mesh.vertex_count, sizeof(float) * 3,
				options.cluster_vertex_limit, options.cluster_triangle_limit, 0.5f);
			if (meshlet_count == 0)
			{
				failure = "meshoptimizer produced no clusters";
				return false;
			}

			for (std::size_t i = 0; i < meshlet_count; ++i)
			{
				const meshopt_Meshlet &meshlet = meshlets[i];
				if (meshlet.triangle_count == 0
					|| meshlet.triangle_count > options.cluster_triangle_limit
					|| meshlet.vertex_count == 0
					|| meshlet.vertex_count > options.cluster_vertex_limit
					|| meshlet.vertex_offset > meshlet_vertices.size()
					|| meshlet.vertex_count > meshlet_vertices.size() - meshlet.vertex_offset
					|| meshlet.triangle_offset > meshlet_triangles.size()
					|| meshlet.triangle_count * 3 > meshlet_triangles.size() - meshlet.triangle_offset)
				{
					failure = "meshoptimizer returned an invalid meshlet range";
					return false;
				}

				ClusterLodCluster cluster;
				cluster.first_index = static_cast<std::uint32_t>(merged_indices.size());
				cluster.index_count = meshlet.triangle_count * 3;
				cluster.vertex_count = meshlet.vertex_count;
				cluster.material_index = material_index;
				cluster.group_index = group_index;
				cluster.level = level;
				cluster.error = error;
				for (std::size_t triangle = 0; triangle < meshlet.triangle_count; ++triangle)
				{
					for (std::size_t corner = 0; corner < 3; ++corner)
					{
						const unsigned int local_index = meshlet_triangles[
							meshlet.triangle_offset + triangle * 3 + corner];
						if (local_index >= meshlet.vertex_count)
						{
							failure = "meshlet local index is out of range";
							return false;
						}
						const unsigned int source_index = meshlet_vertices[
							meshlet.vertex_offset + local_index];
						if (source_index >= mesh.vertex_count)
						{
							failure = "meshlet source index is out of range";
							return false;
						}
						merged_indices.push_back(source_index);
					}
				}

				const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
					meshlet_vertices.data() + meshlet.vertex_offset,
					meshlet_triangles.data() + meshlet.triangle_offset,
					meshlet.triangle_count, mesh.positions, mesh.vertex_count, sizeof(float) * 3);
				std::copy(std::begin(bounds.center), std::end(bounds.center), cluster.bounds.center);
				cluster.bounds.radius = bounds.radius + std::max(1e-5f, bounds.radius * 1e-4f);
				std::copy(std::begin(bounds.cone_axis), std::end(bounds.cone_axis), cluster.normal_cone.axis);
				cluster.normal_cone.cutoff = std::max(-1.0f, bounds.cone_cutoff - 1e-4f);
				asset.clusters.push_back(cluster);
			}
			return true;
		}

		std::vector<std::vector<std::uint32_t>> PartitionUnits(
			const std::vector<WorkingUnit> &units, const ClusterLodAsset &asset,
			const std::vector<std::uint32_t> &merged_indices, std::uint32_t cluster_limit)
		{
			using Edge = std::pair<std::uint32_t, std::uint32_t>;
			std::map<Edge, std::uint32_t> edge_owners;
			std::vector<std::set<std::uint32_t>> adjacency(units.size());
			for (std::uint32_t unit_index = 0; unit_index < units.size(); ++unit_index)
			{
				for (std::uint32_t cluster_index : units[unit_index].clusters)
				{
					const ClusterLodCluster &cluster = asset.clusters[cluster_index];
					for (std::uint32_t i = 0; i < cluster.index_count; i += 3)
					{
						const std::uint32_t *triangle = merged_indices.data() + cluster.first_index + i;
						for (std::uint32_t corner = 0; corner < 3; ++corner)
						{
							std::uint32_t a = triangle[corner];
							std::uint32_t b = triangle[(corner + 1) % 3];
							if (a > b)
								std::swap(a, b);
							const auto inserted = edge_owners.emplace(Edge(a, b), unit_index);
							const std::uint32_t other = inserted.first->second;
							if (other == unit_index || units[other].material_index != units[unit_index].material_index)
								continue;
							adjacency[unit_index].insert(other);
							adjacency[other].insert(unit_index);
						}
					}
				}
			}

			std::vector<std::vector<std::uint32_t>> partitions;
			std::vector<bool> used(units.size(), false);
			for (std::uint32_t seed = 0; seed < units.size(); ++seed)
			{
				if (used[seed])
					continue;
				used[seed] = true;
				partitions.push_back({ seed });
				std::vector<std::uint32_t> pending = { seed };
				std::size_t cluster_count = units[seed].clusters.size();
				for (std::size_t i = 0; i < pending.size(); ++i)
				{
					for (std::uint32_t neighbor : adjacency[pending[i]])
					{
						if (used[neighbor]
							|| cluster_count + units[neighbor].clusters.size() > cluster_limit)
							continue;
						used[neighbor] = true;
						cluster_count += units[neighbor].clusters.size();
						partitions.back().push_back(neighbor);
						pending.push_back(neighbor);
					}
				}
			}
			return partitions;
		}
	}

	ClusterLodBuildResult BuildClusterLodLevelZero(const ClusterLodMeshView &mesh,
		const ClusterLodBuildOptions &options)
	{
		std::string validation_error;
		if (!ValidateOptions(options, validation_error))
			return Failure(validation_error);
		if (mesh.skinned)
			return Failure("skinned meshes are not eligible");
		if (!mesh.positions || mesh.vertex_count == 0)
			return Failure("mesh has no positions");
		if (mesh.vertex_count > std::numeric_limits<std::uint32_t>::max())
			return Failure("mesh has too many vertices");
		if (!mesh.submeshes || mesh.submesh_count == 0)
			return Failure("mesh has no canonical submeshes");

		for (std::size_t i = 0; i < mesh.vertex_count * 3; ++i)
		{
			if (!std::isfinite(mesh.positions[i]))
				return Failure("mesh contains a non-finite position");
		}

		std::size_t total_index_count = 0;
		for (std::size_t i = 0; i < mesh.submesh_count; ++i)
		{
			const ClusterLodSubmeshView &submesh = mesh.submeshes[i];
			if (submesh.index_count == 0)
				continue;
			if (!submesh.indices || submesh.index_count % 3 != 0)
				return Failure("submesh has non-triangle topology");
			if (submesh.index_count > std::numeric_limits<std::size_t>::max() - total_index_count)
				return Failure("mesh index count overflows addressable memory");
			total_index_count += submesh.index_count;
			for (std::size_t j = 0; j < submesh.index_count; ++j)
			{
				if (submesh.indices[j] >= mesh.vertex_count)
					return Failure("submesh index is outside the source vertex buffer");
			}
		}
		if (total_index_count == 0)
			return Failure("mesh has no triangles");
		if (total_index_count > std::numeric_limits<std::uint32_t>::max())
			return Failure("merged cluster index buffer exceeds 32-bit ranges");

		auto asset = std::make_shared<ClusterLodAsset>();
		asset->options = options;
		std::vector<std::uint32_t> merged_indices;
		merged_indices.reserve(total_index_count);
		for (std::size_t i = 0; i < mesh.submesh_count; ++i)
		{
			const ClusterLodSubmeshView &submesh = mesh.submeshes[i];
			if (submesh.index_count == 0)
				continue;
			if (!AppendMeshlets(mesh, options, submesh.indices, submesh.index_count,
				submesh.material_index, 0, ClusterLodInvalidIndex, 0.0f,
				*asset, merged_indices, validation_error))
			{
				return Failure(validation_error);
			}
		}

		if (merged_indices.size() != total_index_count)
			return Failure("LOD-zero clusters do not cover every source triangle exactly once");
		asset->SetMergedIndices(std::move(merged_indices));
		asset->RebuildGpuRecords();
		ClusterLodBuildResult result;
		result.asset = std::move(asset);
		return result;
	}

	ClusterLodBuildResult BuildClusterLod(const ClusterLodMeshView &mesh,
		const ClusterLodBuildOptions &options)
	{
		ClusterLodBuildResult base = BuildClusterLodLevelZero(mesh, options);
		if (!base)
			return base;

		auto asset = std::move(base.asset);
		std::vector<std::uint32_t> merged_indices = *asset->merged_indices;
		std::vector<WorkingUnit> active;
		active.reserve(asset->clusters.size());
		for (std::uint32_t i = 0; i < asset->clusters.size(); ++i)
		{
			WorkingUnit unit;
			unit.clusters = { i };
			unit.material_index = asset->clusters[i].material_index;
			active.push_back(std::move(unit));
		}

		const float error_scale = meshopt_simplifyScale(mesh.positions, mesh.vertex_count, sizeof(float) * 3);
		std::uint32_t level = 1;
		while (!active.empty())
		{
			const auto partitions = PartitionUnits(active, *asset, merged_indices, options.group_cluster_limit);
			std::vector<WorkingUnit> next;
			bool produced_level = false;
			for (const auto &partition : partitions)
			{
				std::vector<unsigned int> input_indices;
				std::size_t input_index_count = 0;
				for (std::uint32_t unit_index : partition)
				{
					for (std::uint32_t cluster_index : active[unit_index].clusters)
						input_index_count += asset->clusters[cluster_index].index_count;
				}
				input_indices.reserve(input_index_count);
				float propagated_error = 0.0f;
				bool has_bounds = false;
				ClusterLodSphere group_bounds;
				for (std::uint32_t unit_index : partition)
				{
					const WorkingUnit &unit = active[unit_index];
					if (unit.group_index != ClusterLodInvalidIndex)
					{
						group_bounds = has_bounds
							? ClusterLodMerge(group_bounds, asset->groups[unit.group_index].bounds)
							: asset->groups[unit.group_index].bounds;
						has_bounds = true;
					}
					for (std::uint32_t cluster_index : active[unit_index].clusters)
					{
						const ClusterLodCluster &cluster = asset->clusters[cluster_index];
						const auto begin = merged_indices.begin() + cluster.first_index;
						input_indices.insert(input_indices.end(), begin, begin + cluster.index_count);
						propagated_error = std::max(propagated_error, cluster.error);
						group_bounds = has_bounds ? ClusterLodMerge(group_bounds, cluster.bounds) : cluster.bounds;
						has_bounds = true;
					}
				}

				std::size_t target_index_count = static_cast<std::size_t>(
					input_indices.size() * options.reduction_ratio);
				target_index_count = std::max<std::size_t>(3, target_index_count / 3 * 3);
				if (target_index_count >= input_indices.size())
					continue;

				std::vector<unsigned int> simplified_indices(input_indices.size());
				float relative_error = 0.0f;
				const std::size_t simplified_count = meshopt_simplify(simplified_indices.data(),
					input_indices.data(), input_indices.size(), mesh.positions, mesh.vertex_count,
					sizeof(float) * 3, target_index_count, options.target_error,
					meshopt_SimplifyLockBorder, &relative_error);
				if (simplified_count == 0 || simplified_count >= input_indices.size())
					continue;
				if (simplified_count % 3 != 0)
					return Failure("simplifier returned non-triangle topology");

				const std::uint32_t group_index = static_cast<std::uint32_t>(asset->groups.size());
				const std::uint32_t material_index = active[partition.front()].material_index;
				ClusterLodGroup group;
				group.level = level;
				group.input_cluster_offset = static_cast<std::uint32_t>(asset->group_input_clusters.size());
				group.output_cluster_offset = static_cast<std::uint32_t>(asset->clusters.size());
				group.child_group_offset = static_cast<std::uint32_t>(asset->group_children.size());
				group.local_error = relative_error * error_scale;
				group.error = std::max(group.local_error, propagated_error);
				group.bounds = group_bounds;
				if (!std::isfinite(group.error))
					return Failure("simplifier returned a non-finite error");

				for (std::uint32_t unit_index : partition)
				{
					const WorkingUnit &unit = active[unit_index];
					if (unit.group_index != ClusterLodInvalidIndex)
					{
						asset->group_children.push_back(unit.group_index);
						asset->groups[unit.group_index].parent_group_index = group_index;
						asset->groups[unit.group_index].parent_error = group.error;
					}
					for (std::uint32_t cluster_index : unit.clusters)
					{
						asset->group_input_clusters.push_back(cluster_index);
						asset->clusters[cluster_index].parent_group_index = group_index;
						asset->clusters[cluster_index].parent_error = group.error;
					}
				}
				group.input_cluster_count = static_cast<std::uint32_t>(
					asset->group_input_clusters.size() - group.input_cluster_offset);
				group.child_group_count = static_cast<std::uint32_t>(
					asset->group_children.size() - group.child_group_offset);
				asset->groups.push_back(group);

				std::string append_error;
				if (!AppendMeshlets(mesh, options, simplified_indices.data(), simplified_count,
					material_index, level, group_index, group.error,
					*asset, merged_indices, append_error))
				{
					return Failure(append_error);
				}

				WorkingUnit output;
				output.group_index = group_index;
				output.material_index = material_index;
				ClusterLodGroup &stored_group = asset->groups[group_index];
				stored_group.output_cluster_count = static_cast<std::uint32_t>(
					asset->clusters.size() - stored_group.output_cluster_offset);
				for (std::uint32_t i = 0; i < stored_group.output_cluster_count; ++i)
				{
					const std::uint32_t cluster_index = stored_group.output_cluster_offset + i;
					output.clusters.push_back(cluster_index);
					stored_group.bounds = ClusterLodMerge(stored_group.bounds,
						ClusterLodExpand(asset->clusters[cluster_index].bounds, stored_group.local_error));
				}
				next.push_back(std::move(output));
				produced_level = true;
			}
			if (!produced_level)
				break;
			active = std::move(next);
			++level;
			if (level > merged_indices.size() / 3 + 1)
				return Failure("cluster group construction did not converge");
		}

		asset->SetMergedIndices(std::move(merged_indices));
		asset->RebuildGpuRecords();
		std::string validation_error;
		if (!ValidateClusterLodAsset(*asset, mesh.vertex_count, validation_error))
			return Failure(validation_error);
		ClusterLodBuildResult result;
		result.asset = std::move(asset);
		return result;
	}
}
