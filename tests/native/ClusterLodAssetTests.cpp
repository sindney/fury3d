#include "Fury/ClusterLodAsset.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>
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

	static_assert(std::is_standard_layout_v<ClusterLodGpuCluster>);
	static_assert(std::is_trivially_copyable_v<ClusterLodGpuCluster>);
	static_assert(std::is_standard_layout_v<ClusterLodGpuGroup>);
	static_assert(std::is_trivially_copyable_v<ClusterLodGpuGroup>);
	static_assert(sizeof(ClusterLodGpuCluster) == 64);
	static_assert(alignof(ClusterLodGpuCluster) == 16);
	static_assert(offsetof(ClusterLodGpuCluster, radius) == 12);
	static_assert(offsetof(ClusterLodGpuCluster, cone_axis) == 16);
	static_assert(offsetof(ClusterLodGpuCluster, cone_cutoff) == 28);
	static_assert(offsetof(ClusterLodGpuCluster, error) == 32);
	static_assert(offsetof(ClusterLodGpuCluster, parent_error) == 36);
	static_assert(offsetof(ClusterLodGpuCluster, group_index) == 40);
	static_assert(offsetof(ClusterLodGpuCluster, parent_group_index) == 44);
	static_assert(offsetof(ClusterLodGpuCluster, first_index) == 48);
	static_assert(offsetof(ClusterLodGpuCluster, index_count) == 52);
	static_assert(offsetof(ClusterLodGpuCluster, material_index) == 56);
	static_assert(offsetof(ClusterLodGpuCluster, level) == 60);
	static_assert(sizeof(ClusterLodGpuGroup) == 64);
	static_assert(alignof(ClusterLodGpuGroup) == 16);
	static_assert(offsetof(ClusterLodGpuGroup, radius) == 12);
	static_assert(offsetof(ClusterLodGpuGroup, local_error) == 16);
	static_assert(offsetof(ClusterLodGpuGroup, error) == 20);
	static_assert(offsetof(ClusterLodGpuGroup, parent_error) == 24);
	static_assert(offsetof(ClusterLodGpuGroup, parent_group_index) == 28);
	static_assert(offsetof(ClusterLodGpuGroup, input_cluster_offset) == 32);
	static_assert(offsetof(ClusterLodGpuGroup, output_cluster_offset) == 40);
	static_assert(offsetof(ClusterLodGpuGroup, child_group_offset) == 48);
	static_assert(offsetof(ClusterLodGpuGroup, level) == 56);
	static_assert(offsetof(ClusterLodGpuGroup, padding) == 60);
	static_assert(std::is_same_v<decltype(ClusterLodAsset::merged_indices),
		std::shared_ptr<const std::vector<std::uint32_t>>>);

	ClusterLodSphere Sphere(float x, float y, float z, float radius)
	{
		ClusterLodSphere result;
		result.center[0] = x;
		result.center[1] = y;
		result.center[2] = z;
		result.radius = radius;
		return result;
	}

	void TestDefaults()
	{
		ClusterLodBuildOptions options;
		CHECK(options.cluster_triangle_limit == 128);
		CHECK(options.cluster_vertex_limit == 128);
		CHECK(options.group_cluster_limit == 8);
		CHECK(options.reduction_ratio == 0.5f);

		ClusterLodDescriptor descriptor;
		CHECK(descriptor.schema_version == ClusterLodDescriptorSchemaVersion);
		CHECK(descriptor.content_hash == 0);
		CHECK(!descriptor.enabled);

		ClusterLodCluster cluster;
		CHECK(cluster.group_index == ClusterLodInvalidIndex);
		CHECK(cluster.parent_group_index == ClusterLodInvalidIndex);
		CHECK(cluster.error == 0.0f);
		CHECK(std::isinf(cluster.parent_error));

		ClusterLodGroup group;
		CHECK(group.parent_group_index == ClusterLodInvalidIndex);
		CHECK(group.error == 0.0f);
		CHECK(std::isinf(group.parent_error));
	}

	void TestBounds()
	{
		const ClusterLodSphere outer = Sphere(0.0f, 0.0f, 0.0f, 2.0f);
		const ClusterLodSphere nested = Sphere(1.0f, 0.0f, 0.0f, 1.0f);
		const ClusterLodSphere disjoint = Sphere(5.0f, 0.0f, 0.0f, 1.0f);
		CHECK(ClusterLodContains(outer, outer));
		CHECK(ClusterLodContains(outer, nested));
		CHECK(!ClusterLodContains(nested, outer));
		CHECK(!ClusterLodContains(outer, disjoint));
		CHECK(ClusterLodContains(outer, Sphere(1.0f, 0.0f, 0.0f, 1.00005f)));
		CHECK(!ClusterLodContains(outer, Sphere(-2.0f, 0.0f, 0.0f, 1.0f)));

		const ClusterLodSphere merged = ClusterLodMerge(outer, disjoint);
		CHECK(merged.center[0] == 2.0f);
		CHECK(std::abs(merged.radius - 4.0f) < 0.001f);
		CHECK(ClusterLodContains(merged, outer));
		CHECK(ClusterLodContains(merged, disjoint));
		CHECK(ClusterLodContains(ClusterLodMerge(outer, nested), outer));
		CHECK(ClusterLodContains(ClusterLodMerge(outer, nested), nested));

		const ClusterLodSphere points = ClusterLodMerge(
			Sphere(0.0f, 0.0f, 0.0f, 0.0f), Sphere(2.0f, 0.0f, 0.0f, 0.0f));
		CHECK(points.center[0] == 1.0f);
		CHECK(points.radius >= 1.0f && points.radius < 1.001f);

		const ClusterLodSphere expanded = ClusterLodExpand(nested, 0.5f);
		CHECK(expanded.radius == 1.5f);
		CHECK(ClusterLodContains(expanded, nested));
		CHECK(ClusterLodExpand(nested, -1.0f).radius == nested.radius);
	}

	void TestSharedIndices()
	{
		ClusterLodAsset first;
		ClusterLodAsset second;
		first.SetMergedIndices({ 3, 1, 4, 1, 5, 9 });
		second.merged_indices = first.merged_indices;
		const std::shared_ptr<const std::vector<std::uint32_t>> allocation = first.merged_indices;
		CHECK(second.merged_indices.get() == allocation.get());
		first.merged_indices.reset();
		CHECK(second.merged_indices->at(2) == 4);
		second.merged_indices.reset();
		CHECK(allocation->size() == 6);
		CHECK(allocation->at(5) == 9);
	}

	void TestGpuConversion()
	{
		ClusterLodAsset asset;
		asset.clusters.resize(1);
		ClusterLodCluster &cluster = asset.clusters[0];
		cluster.bounds = Sphere(1.0f, 2.0f, 3.0f, 4.0f);
		cluster.normal_cone.axis[0] = 0.0f;
		cluster.normal_cone.axis[1] = 1.0f;
		cluster.normal_cone.axis[2] = 0.0f;
		cluster.normal_cone.cutoff = 0.25f;
		cluster.error = 0.5f;
		cluster.parent_error = 1.5f;
		cluster.group_index = 2;
		cluster.parent_group_index = 3;
		cluster.first_index = 30;
		cluster.index_count = 384;
		cluster.material_index = 1;
		cluster.level = 2;

		asset.groups.resize(1);
		ClusterLodGroup &group = asset.groups[0];
		group.bounds = Sphere(4.0f, 5.0f, 6.0f, 7.0f);
		group.local_error = 0.25f;
		group.error = 0.5f;
		group.parent_error = 2.0f;
		group.parent_group_index = 8;
		group.input_cluster_offset = 10;
		group.input_cluster_count = 4;
		group.output_cluster_offset = 20;
		group.output_cluster_count = 2;
		group.child_group_offset = 30;
		group.child_group_count = 3;
		group.level = 1;

		asset.RebuildGpuRecords();
		CHECK(asset.gpu_clusters.size() == 1);
		CHECK(asset.gpu_groups.size() == 1);
		const ClusterLodGpuCluster &gpu_cluster = asset.gpu_clusters[0];
		CHECK(gpu_cluster.center[2] == 3.0f);
		CHECK(gpu_cluster.radius == 4.0f);
		CHECK(gpu_cluster.cone_axis[1] == 1.0f);
		CHECK(gpu_cluster.cone_cutoff == 0.25f);
		CHECK(gpu_cluster.error == 0.5f);
		CHECK(gpu_cluster.parent_error == 1.5f);
		CHECK(gpu_cluster.group_index == 2);
		CHECK(gpu_cluster.parent_group_index == 3);
		CHECK(gpu_cluster.first_index == 30);
		CHECK(gpu_cluster.index_count == 384);
		CHECK(gpu_cluster.material_index == 1);
		CHECK(gpu_cluster.level == 2);

		const ClusterLodGpuGroup &gpu_group = asset.gpu_groups[0];
		CHECK(gpu_group.center[1] == 5.0f);
		CHECK(gpu_group.radius == 7.0f);
		CHECK(gpu_group.local_error == 0.25f);
		CHECK(gpu_group.error == 0.5f);
		CHECK(gpu_group.parent_error == 2.0f);
		CHECK(gpu_group.parent_group_index == 8);
		CHECK(gpu_group.input_cluster_offset == 10);
		CHECK(gpu_group.input_cluster_count == 4);
		CHECK(gpu_group.output_cluster_offset == 20);
		CHECK(gpu_group.output_cluster_count == 2);
		CHECK(gpu_group.child_group_offset == 30);
		CHECK(gpu_group.child_group_count == 3);
		CHECK(gpu_group.level == 1);
		CHECK(gpu_group.padding == 0);
	}
}

int main()
{
	TestDefaults();
	TestBounds();
	TestSharedIndices();
	TestGpuConversion();
	if (failures != 0)
	{
		std::cerr << failures << " cluster LOD asset checks failed" << std::endl;
		return 1;
	}
	std::cout << "Cluster LOD asset tests passed" << std::endl;
	return 0;
}
