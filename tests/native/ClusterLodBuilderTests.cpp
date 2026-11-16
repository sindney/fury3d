#include "Fury/ClusterLodBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace
{
	using namespace fury;

	int failures = 0;

	void Check(bool condition, const char *expression)
	{
		if (condition)
			return;
		std::cerr << "FAILED: " << expression << std::endl;
		++failures;
	}

#define CHECK(expression) Check((expression), #expression)

	struct GridFixture
	{
		std::vector<float> positions;
		std::vector<unsigned int> material_zero;
		std::vector<unsigned int> material_one;
	};

	GridFixture MakeGrid(std::size_t quadrant_count)
	{
		GridFixture fixture;
		const std::size_t stride = quadrant_count + 1;
		for (std::size_t y = 0; y < stride; ++y)
		{
			for (std::size_t x = 0; x < stride; ++x)
			{
				fixture.positions.push_back(static_cast<float>(x));
				fixture.positions.push_back(static_cast<float>(y));
				fixture.positions.push_back(0.0f);
			}
		}
		for (std::size_t y = 0; y < quadrant_count; ++y)
		{
			for (std::size_t x = 0; x < quadrant_count; ++x)
			{
				const unsigned int a = static_cast<unsigned int>(y * stride + x);
				const unsigned int b = a + 1;
				const unsigned int c = a + static_cast<unsigned int>(stride);
				const unsigned int d = c + 1;
				auto &indices = x < quadrant_count / 2 ? fixture.material_zero : fixture.material_one;
				indices.insert(indices.end(), { a, b, c, b, d, c });
			}
		}
		return fixture;
	}

	ClusterLodMeshView MakeView(const GridFixture &fixture,
		const std::vector<ClusterLodSubmeshView> &submeshes)
	{
		ClusterLodMeshView view;
		view.positions = fixture.positions.data();
		view.vertex_count = fixture.positions.size() / 3;
		view.submeshes = submeshes.data();
		view.submesh_count = submeshes.size();
		return view;
	}

	using Triangle = std::array<unsigned int, 3>;
	using TriangleCounts = std::map<Triangle, std::size_t>;

	void AddTriangle(TriangleCounts &counts, const unsigned int *indices)
	{
		Triangle triangle = { indices[0], indices[1], indices[2] };
		std::sort(triangle.begin(), triangle.end());
		++counts[triangle];
	}

	TriangleCounts SourceTriangles(const GridFixture &fixture)
	{
		TriangleCounts counts;
		for (std::size_t i = 0; i < fixture.material_zero.size(); i += 3)
			AddTriangle(counts, fixture.material_zero.data() + i);
		for (std::size_t i = 0; i < fixture.material_one.size(); i += 3)
			AddTriangle(counts, fixture.material_one.data() + i);
		return counts;
	}

	TriangleCounts OutputTriangles(const ClusterLodAsset &asset)
	{
		TriangleCounts counts;
		const auto &indices = *asset.merged_indices;
		for (std::size_t i = 0; i < indices.size(); i += 3)
			AddTriangle(counts, indices.data() + i);
		return counts;
	}

	void TestLevelZero()
	{
		const GridFixture fixture = MakeGrid(8);
		const std::vector<ClusterLodSubmeshView> submeshes = {
			{ fixture.material_zero.data(), fixture.material_zero.size(), 2 },
			{ fixture.material_one.data(), fixture.material_one.size(), 5 }
		};
		ClusterLodBuildOptions options;
		options.cluster_triangle_limit = 32;
		options.cluster_vertex_limit = 32;
		const ClusterLodMeshView view = MakeView(fixture, submeshes);
		const ClusterLodBuildResult result = BuildClusterLodLevelZero(view, options);
		CHECK(result.asset != nullptr);
		if (!result)
		{
			std::cerr << result.error << std::endl;
			return;
		}

		const ClusterLodAsset &asset = *result.asset;
		CHECK(asset.groups.empty());
		CHECK(asset.clusters.size() > 2);
		CHECK(asset.gpu_clusters.size() == asset.clusters.size());
		CHECK(asset.merged_indices->size()
			== fixture.material_zero.size() + fixture.material_one.size());
		CHECK(OutputTriangles(asset) == SourceTriangles(fixture));

		std::size_t expected_offset = 0;
		std::set<std::uint32_t> materials;
		for (const ClusterLodCluster &cluster : asset.clusters)
		{
			CHECK(cluster.first_index == expected_offset);
			CHECK(cluster.index_count % 3 == 0);
			CHECK(cluster.index_count <= options.cluster_triangle_limit * 3);
			CHECK(cluster.vertex_count > 0);
			CHECK(cluster.vertex_count <= options.cluster_vertex_limit);
			CHECK(cluster.level == 0);
			CHECK(cluster.error == 0.0f);
			CHECK(std::isinf(cluster.parent_error));
			CHECK(cluster.group_index == ClusterLodInvalidIndex);
			materials.insert(cluster.material_index);
			for (std::size_t i = 0; i < cluster.index_count; ++i)
			{
				const unsigned int index = (*asset.merged_indices)[cluster.first_index + i];
				const float *position = fixture.positions.data() + index * 3;
				const float x = position[0] - cluster.bounds.center[0];
				const float y = position[1] - cluster.bounds.center[1];
				const float z = position[2] - cluster.bounds.center[2];
				CHECK(std::sqrt(x * x + y * y + z * z) <= cluster.bounds.radius + 1e-5f);
			}
			const float axis_length = std::sqrt(cluster.normal_cone.axis[0] * cluster.normal_cone.axis[0]
				+ cluster.normal_cone.axis[1] * cluster.normal_cone.axis[1]
				+ cluster.normal_cone.axis[2] * cluster.normal_cone.axis[2]);
			CHECK(axis_length <= 1.0f + 1e-5f);
			CHECK(cluster.normal_cone.cutoff >= -1.0f);
			CHECK(cluster.normal_cone.cutoff <= 1.0f);
			expected_offset += cluster.index_count;
		}
		CHECK(expected_offset == asset.merged_indices->size());
		CHECK(materials == std::set<std::uint32_t>({ 2, 5 }));

		const ClusterLodBuildResult second = BuildClusterLodLevelZero(view, options);
		CHECK(second.asset != nullptr);
		if (second)
		{
			CHECK(*second.asset->merged_indices == *asset.merged_indices);
			CHECK(second.asset->clusters.size() == asset.clusters.size());
			for (std::size_t i = 0; i < asset.clusters.size(); ++i)
			{
				const ClusterLodCluster &a = asset.clusters[i];
				const ClusterLodCluster &b = second.asset->clusters[i];
				CHECK(a.first_index == b.first_index);
				CHECK(a.index_count == b.index_count);
				CHECK(a.vertex_count == b.vertex_count);
				CHECK(a.material_index == b.material_index);
				CHECK(a.bounds.radius == b.bounds.radius);
				CHECK(a.normal_cone.cutoff == b.normal_cone.cutoff);
			}
		}
	}

	void TestParentLevels()
	{
		const GridFixture fixture = MakeGrid(8);
		const std::vector<ClusterLodSubmeshView> submeshes = {
			{ fixture.material_zero.data(), fixture.material_zero.size(), 2 },
			{ fixture.material_one.data(), fixture.material_one.size(), 5 }
		};
		ClusterLodBuildOptions options;
		options.cluster_triangle_limit = 32;
		options.cluster_vertex_limit = 32;
		options.group_cluster_limit = 4;
		options.target_error = 1.0f;
		const ClusterLodBuildResult result = BuildClusterLod(MakeView(fixture, submeshes), options);
		CHECK(result.asset != nullptr);
		if (!result)
		{
			std::cerr << result.error << std::endl;
			return;
		}

		const ClusterLodAsset &asset = *result.asset;
		const std::size_t leaf_count = std::count_if(asset.clusters.begin(), asset.clusters.end(),
			[](const ClusterLodCluster &cluster) { return cluster.level == 0; });
		CHECK(!asset.groups.empty());
		CHECK(asset.clusters.size() > leaf_count);
		CHECK(asset.gpu_groups.size() == asset.groups.size());
		std::size_t root_triangle_count = 0;
		std::size_t leaf_triangle_count = 0;
		for (const ClusterLodCluster &cluster : asset.clusters)
		{
			if (cluster.level == 0)
				leaf_triangle_count += cluster.index_count / 3;
			if (cluster.parent_group_index == ClusterLodInvalidIndex)
			{
				root_triangle_count += cluster.index_count / 3;
				CHECK(std::isinf(cluster.parent_error));
			}
		}
		CHECK(root_triangle_count < leaf_triangle_count);

		bool found_root_group = false;
		for (std::size_t group_index = 0; group_index < asset.groups.size(); ++group_index)
		{
			const ClusterLodGroup &group = asset.groups[group_index];
			CHECK(group.input_cluster_count > 0);
			CHECK(group.input_cluster_count <= options.group_cluster_limit);
			CHECK(group.output_cluster_count > 0);
			CHECK(group.input_cluster_offset + group.input_cluster_count <= asset.group_input_clusters.size());
			CHECK(group.output_cluster_offset + group.output_cluster_count <= asset.clusters.size());
			CHECK(group.child_group_offset + group.child_group_count <= asset.group_children.size());
			CHECK(group.error >= group.local_error);
			const std::uint32_t material = asset.clusters[group.output_cluster_offset].material_index;
			for (std::uint32_t i = 0; i < group.input_cluster_count; ++i)
			{
				const std::uint32_t cluster_index = asset.group_input_clusters[group.input_cluster_offset + i];
				const ClusterLodCluster &input = asset.clusters[cluster_index];
				CHECK(input.parent_group_index == group_index);
				CHECK(input.parent_error == group.error);
				CHECK(input.material_index == material);
				CHECK(input.error <= group.error);
				CHECK(ClusterLodContains(group.bounds, input.bounds));
			}
			for (std::uint32_t i = 0; i < group.output_cluster_count; ++i)
			{
				const ClusterLodCluster &output = asset.clusters[group.output_cluster_offset + i];
				CHECK(output.group_index == group_index);
				CHECK(output.level == group.level);
				CHECK(output.error == group.error);
				CHECK(output.material_index == material);
			}
			for (std::uint32_t i = 0; i < group.child_group_count; ++i)
			{
				const std::uint32_t child_index = asset.group_children[group.child_group_offset + i];
				CHECK(child_index < asset.groups.size());
				CHECK(asset.groups[child_index].parent_group_index == group_index);
				CHECK(asset.groups[child_index].level < group.level);
			}
			if (group.parent_group_index == ClusterLodInvalidIndex)
			{
				found_root_group = true;
				CHECK(std::isinf(group.parent_error));
			}
			else
			{
				CHECK(group.parent_group_index < asset.groups.size());
				CHECK(group.parent_error == asset.groups[group.parent_group_index].error);
			}
		}
		CHECK(found_root_group);
	}

	void TestFailureRejection()
	{
		const GridFixture fixture = MakeGrid(1);
		const ClusterLodBuildOptions options;
		std::vector<ClusterLodSubmeshView> submeshes = {
			{ fixture.material_zero.data(), fixture.material_zero.size(), 0 }
		};
		ClusterLodMeshView view = MakeView(fixture, submeshes);

		submeshes[0].index_count = 4;
		CHECK(!BuildClusterLodLevelZero(view, options));
		submeshes[0].index_count = fixture.material_zero.size();

		std::vector<unsigned int> invalid_indices = { 0, 1, 100 };
		submeshes[0].indices = invalid_indices.data();
		submeshes[0].index_count = invalid_indices.size();
		CHECK(!BuildClusterLodLevelZero(view, options));
		submeshes[0].indices = fixture.material_zero.data();
		submeshes[0].index_count = fixture.material_zero.size();

		view.skinned = true;
		CHECK(!BuildClusterLodLevelZero(view, options));
		view.skinned = false;

		ClusterLodBuildOptions invalid_options;
		invalid_options.cluster_triangle_limit = 0;
		CHECK(!BuildClusterLodLevelZero(view, invalid_options));

		view.submeshes = nullptr;
		view.submesh_count = 0;
		CHECK(!BuildClusterLodLevelZero(view, options));
	}
}

int main()
{
	TestLevelZero();
	TestParentLevels();
	TestFailureRejection();
	if (failures != 0)
	{
		std::cerr << failures << " cluster LOD builder checks failed" << std::endl;
		return 1;
	}
	std::cout << "Cluster LOD builder tests passed" << std::endl;
	return 0;
}
