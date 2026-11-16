#include "Fury/ClusterLodBuilder.h"
#include "Fury/ClusterLodSerialization.h"

#include <cstdint>
#include <cstring>
#include <iostream>
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

	std::uint32_t ReadU32(const std::vector<std::uint8_t> &bytes, std::size_t offset)
	{
		return static_cast<std::uint32_t>(bytes[offset])
			| (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
			| (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
			| (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	}

	void WriteU32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
	{
		for (int i = 0; i < 4; ++i)
			bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
	}

	std::shared_ptr<ClusterLodAsset> MakeAsset()
	{
		const float positions[] = {
			0, 0, 0, 1, 0, 0, 2, 0, 0,
			0, 1, 0, 1, 1, 0, 2, 1, 0,
			0, 2, 0, 1, 2, 0, 2, 2, 0
		};
		const unsigned int indices[] = {
			0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4,
			3, 4, 7, 3, 7, 6, 4, 5, 8, 4, 8, 7
		};
		ClusterLodSubmeshView submesh = { indices, sizeof(indices) / sizeof(indices[0]), 3 };
		ClusterLodMeshView view;
		view.positions = positions;
		view.vertex_count = 9;
		view.submeshes = &submesh;
		view.submesh_count = 1;
		ClusterLodBuildOptions options;
		options.cluster_triangle_limit = 4;
		options.cluster_vertex_limit = 8;
		options.group_cluster_limit = 4;
		options.target_error = 1.0f;
		ClusterLodBuildResult result = BuildClusterLod(view, options);
		return result.asset;
	}

	void TestRoundTrip(const ClusterLodAsset &source, const std::vector<std::uint8_t> &bytes)
	{
		CHECK(bytes.size() % 16 == 0);
		CHECK(ReadU32(bytes, 4) == ClusterLodPayloadSchemaVersion);
		CHECK(ReadU32(bytes, 16) == ClusterLodPayloadHeaderSize);
		for (int i = 0; i < 7; ++i)
			CHECK(ReadU32(bytes, 76 + i * 4) % 16 == 0);

		ClusterLodPayloadResult loaded = ReadClusterLodPayload(bytes.data(), bytes.size());
		CHECK(loaded.asset != nullptr);
		if (!loaded)
		{
			std::cerr << loaded.error << std::endl;
			return;
		}
		CHECK(loaded.source_hash == 0x123456789abcdef0ULL);
		CHECK(loaded.schema_version == ClusterLodPayloadSchemaVersion);
		CHECK(loaded.builder_version == ClusterLodBuilderVersion);
		CHECK(loaded.meshoptimizer_version == ClusterLodMeshoptimizerVersion);
		CHECK(loaded.vertex_count == 9);
		CHECK(loaded.asset->options.cluster_triangle_limit == source.options.cluster_triangle_limit);
		CHECK(loaded.asset->options.cluster_vertex_limit == source.options.cluster_vertex_limit);
		CHECK(loaded.asset->options.group_cluster_limit == source.options.group_cluster_limit);
		CHECK(loaded.asset->options.reduction_ratio == source.options.reduction_ratio);
		CHECK(loaded.asset->options.target_error == source.options.target_error);
		CHECK(*loaded.asset->merged_indices == *source.merged_indices);
		CHECK(loaded.asset->group_input_clusters == source.group_input_clusters);
		CHECK(loaded.asset->group_children == source.group_children);
		CHECK(loaded.asset->clusters.size() == source.clusters.size());
		CHECK(loaded.asset->groups.size() == source.groups.size());
		for (std::size_t i = 0; i < source.clusters.size(); ++i)
		{
			const ClusterLodCluster &a = source.clusters[i];
			const ClusterLodCluster &b = loaded.asset->clusters[i];
			CHECK(a.first_index == b.first_index);
			CHECK(a.index_count == b.index_count);
			CHECK(a.vertex_count == b.vertex_count);
			CHECK(a.material_index == b.material_index);
			CHECK(a.group_index == b.group_index);
			CHECK(a.parent_group_index == b.parent_group_index);
			CHECK(a.level == b.level);
			CHECK(a.bounds.radius == b.bounds.radius);
			CHECK(a.normal_cone.cutoff == b.normal_cone.cutoff);
			CHECK(a.error == b.error);
			CHECK(a.parent_error == b.parent_error);
		}
		for (std::size_t i = 0; i < source.groups.size(); ++i)
		{
			const ClusterLodGroup &a = source.groups[i];
			const ClusterLodGroup &b = loaded.asset->groups[i];
			CHECK(a.input_cluster_offset == b.input_cluster_offset);
			CHECK(a.input_cluster_count == b.input_cluster_count);
			CHECK(a.output_cluster_offset == b.output_cluster_offset);
			CHECK(a.output_cluster_count == b.output_cluster_count);
			CHECK(a.child_group_offset == b.child_group_offset);
			CHECK(a.child_group_count == b.child_group_count);
			CHECK(a.parent_group_index == b.parent_group_index);
			CHECK(a.level == b.level);
			CHECK(a.bounds.radius == b.bounds.radius);
			CHECK(a.local_error == b.local_error);
			CHECK(a.error == b.error);
			CHECK(a.parent_error == b.parent_error);
		}
		CHECK(std::memcmp(loaded.asset->gpu_clusters.data(), source.gpu_clusters.data(),
			source.gpu_clusters.size() * sizeof(ClusterLodGpuCluster)) == 0);
		CHECK(std::memcmp(loaded.asset->gpu_groups.data(), source.gpu_groups.data(),
			source.gpu_groups.size() * sizeof(ClusterLodGpuGroup)) == 0);

		std::vector<std::uint8_t> second;
		std::string error;
		CHECK(WriteClusterLodPayload(source, 0x123456789abcdef0ULL, 9, second, error));
		CHECK(second == bytes);
	}

	void TestRejection(const ClusterLodAsset &source, const std::vector<std::uint8_t> &bytes)
	{
		for (std::size_t size : { std::size_t(0), std::size_t(1), std::size_t(127),
			std::size_t(128), bytes.size() - 1 })
		{
			CHECK(!ReadClusterLodPayload(bytes.data(), size).asset);
		}
		std::vector<std::uint8_t> corrupt = bytes;
		corrupt[0] = 'X';
		CHECK(!ReadClusterLodPayload(corrupt.data(), corrupt.size()).asset);
		corrupt = bytes;
		WriteU32(corrupt, 4, 99);
		CHECK(!ReadClusterLodPayload(corrupt.data(), corrupt.size()).asset);
		corrupt = bytes;
		WriteU32(corrupt, 8, 99);
		CHECK(!ReadClusterLodPayload(corrupt.data(), corrupt.size()).asset);
		corrupt = bytes;
		WriteU32(corrupt, 76, ReadU32(corrupt, 76) + 1);
		CHECK(!ReadClusterLodPayload(corrupt.data(), corrupt.size()).asset);
		corrupt = bytes;
		corrupt[104] = 1;
		CHECK(!ReadClusterLodPayload(corrupt.data(), corrupt.size()).asset);
		corrupt = bytes;
		WriteU32(corrupt, ReadU32(corrupt, 76), 0xffffffffU);
		CHECK(!ReadClusterLodPayload(corrupt.data(), corrupt.size()).asset);
		corrupt = bytes;
		corrupt[ReadU32(corrupt, 96) + 12] ^= 1;
		CHECK(!ReadClusterLodPayload(corrupt.data(), corrupt.size()).asset);

		ClusterLodAsset invalid = source;
		invalid.clusters[0].first_index = 0xffffffffU;
		std::vector<std::uint8_t> output;
		std::string error;
		CHECK(!WriteClusterLodPayload(invalid, 1, 9, output, error));
		CHECK(output.empty());
	}
}

int main()
{
	const std::shared_ptr<ClusterLodAsset> asset = MakeAsset();
	CHECK(asset != nullptr);
	if (!asset)
		return 1;
	std::vector<std::uint8_t> bytes;
	std::string error;
	CHECK(WriteClusterLodPayload(*asset, 0x123456789abcdef0ULL, 9, bytes, error));
	if (bytes.empty())
	{
		std::cerr << error << std::endl;
		return 1;
	}
	TestRoundTrip(*asset, bytes);
	TestRejection(*asset, bytes);
	if (failures != 0)
	{
		std::cerr << failures << " cluster LOD serialization checks failed" << std::endl;
		return 1;
	}
	std::cout << "Cluster LOD serialization tests passed" << std::endl;
	return 0;
}
