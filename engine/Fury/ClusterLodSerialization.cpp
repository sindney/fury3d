#include "Fury/ClusterLodSerialization.h"

#include <cstring>
#include <limits>

#include "Fury/ClusterLodValidation.h"

namespace fury
{
	namespace
	{
		constexpr std::size_t HeaderSize = ClusterLodPayloadHeaderSize;
		constexpr std::uint8_t Magic[4] = { 'C', 'L', 'O', 'D' };
		constexpr std::size_t ClusterRecordSize = 68;
		constexpr std::size_t GroupRecordSize = 60;

		void WriteU32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
		{
			for (int i = 0; i < 4; ++i)
				bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
		}

		void WriteU64(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint64_t value)
		{
			for (int i = 0; i < 8; ++i)
				bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
		}

		void WriteFloat(std::vector<std::uint8_t> &bytes, std::size_t offset, float value)
		{
			std::uint32_t bits;
			static_assert(sizeof(bits) == sizeof(value));
			std::memcpy(&bits, &value, sizeof(bits));
			WriteU32(bytes, offset, bits);
		}

		std::uint32_t ReadU32(const std::uint8_t *data, std::size_t offset)
		{
			return static_cast<std::uint32_t>(data[offset])
				| (static_cast<std::uint32_t>(data[offset + 1]) << 8)
				| (static_cast<std::uint32_t>(data[offset + 2]) << 16)
				| (static_cast<std::uint32_t>(data[offset + 3]) << 24);
		}

		std::uint64_t ReadU64(const std::uint8_t *data, std::size_t offset)
		{
			std::uint64_t value = 0;
			for (int i = 0; i < 8; ++i)
				value |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8);
			return value;
		}

		float ReadFloat(const std::uint8_t *data, std::size_t offset)
		{
			const std::uint32_t bits = ReadU32(data, offset);
			float value;
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		}

		bool ComputeLayout(const std::uint32_t counts[5], std::uint32_t offsets[7], std::uint32_t &total)
		{
			const std::uint32_t section_counts[7] = {
				counts[0], counts[1], counts[2], counts[3], counts[4], counts[1], counts[2]
			};
			const std::uint64_t record_sizes[7] = { 4, ClusterRecordSize, GroupRecordSize, 4, 4, 64, 64 };
			std::uint64_t running = HeaderSize;
			for (int i = 0; i < 7; ++i)
			{
				offsets[i] = static_cast<std::uint32_t>(running);
				const std::uint64_t bytes = static_cast<std::uint64_t>(section_counts[i]) * record_sizes[i];
				const std::uint64_t aligned = (bytes + 15) & ~std::uint64_t(15);
				running += aligned;
				if (running > std::numeric_limits<std::uint32_t>::max())
					return false;
			}
			total = static_cast<std::uint32_t>(running);
			return true;
		}

		ClusterLodPayloadResult ReadFailure(const std::string &message)
		{
			ClusterLodPayloadResult result;
			result.error = message;
			return result;
		}
	}

	bool WriteClusterLodPayload(const ClusterLodAsset &asset, std::uint64_t source_hash,
		std::uint32_t vertex_count, std::vector<std::uint8_t> &bytes, std::string &error)
	{
		bytes.clear();
		error.clear();
		if (!ValidateClusterLodAsset(asset, vertex_count, error))
			return false;
		ClusterLodAsset normalized = asset;
		normalized.RebuildGpuRecords();
		const std::uint32_t counts[5] = {
			static_cast<std::uint32_t>(asset.merged_indices->size()),
			static_cast<std::uint32_t>(asset.clusters.size()),
			static_cast<std::uint32_t>(asset.groups.size()),
			static_cast<std::uint32_t>(asset.group_input_clusters.size()),
			static_cast<std::uint32_t>(asset.group_children.size())
		};
		if (asset.merged_indices->size() > std::numeric_limits<std::uint32_t>::max()
			|| asset.clusters.size() > std::numeric_limits<std::uint32_t>::max()
			|| asset.groups.size() > std::numeric_limits<std::uint32_t>::max()
			|| asset.group_input_clusters.size() > std::numeric_limits<std::uint32_t>::max()
			|| asset.group_children.size() > std::numeric_limits<std::uint32_t>::max())
		{
			error = "payload exceeds 32-bit section counts";
			return false;
		}

		std::uint32_t offsets[7];
		std::uint32_t total = 0;
		if (!ComputeLayout(counts, offsets, total))
		{
			error = "payload exceeds 32-bit section offsets";
			return false;
		}
		bytes.assign(total, 0);
		for (int i = 0; i < 4; ++i)
			bytes[i] = Magic[i];
		WriteU32(bytes, 4, ClusterLodPayloadSchemaVersion);
		WriteU32(bytes, 8, ClusterLodBuilderVersion);
		WriteU32(bytes, 12, ClusterLodMeshoptimizerVersion);
		WriteU32(bytes, 16, ClusterLodPayloadHeaderSize);
		WriteU64(bytes, 24, source_hash);
		WriteU32(bytes, 32, asset.options.cluster_triangle_limit);
		WriteU32(bytes, 36, asset.options.cluster_vertex_limit);
		WriteU32(bytes, 40, asset.options.group_cluster_limit);
		WriteFloat(bytes, 44, asset.options.reduction_ratio);
		WriteFloat(bytes, 48, asset.options.target_error);
		WriteU32(bytes, 52, vertex_count);
		for (int i = 0; i < 5; ++i)
			WriteU32(bytes, 56 + i * 4, counts[i]);
		for (int i = 0; i < 7; ++i)
			WriteU32(bytes, 76 + i * 4, offsets[i]);

		for (std::size_t i = 0; i < asset.merged_indices->size(); ++i)
			WriteU32(bytes, offsets[0] + i * 4, (*asset.merged_indices)[i]);
		for (std::size_t i = 0; i < asset.clusters.size(); ++i)
		{
			const ClusterLodCluster &cluster = asset.clusters[i];
			const std::size_t offset = offsets[1] + i * ClusterRecordSize;
			WriteU32(bytes, offset, cluster.first_index);
			WriteU32(bytes, offset + 4, cluster.index_count);
			WriteU32(bytes, offset + 8, cluster.vertex_count);
			WriteU32(bytes, offset + 12, cluster.material_index);
			WriteU32(bytes, offset + 16, cluster.group_index);
			WriteU32(bytes, offset + 20, cluster.parent_group_index);
			WriteU32(bytes, offset + 24, cluster.level);
			for (int j = 0; j < 3; ++j)
				WriteFloat(bytes, offset + 28 + j * 4, cluster.bounds.center[j]);
			WriteFloat(bytes, offset + 40, cluster.bounds.radius);
			for (int j = 0; j < 3; ++j)
				WriteFloat(bytes, offset + 44 + j * 4, cluster.normal_cone.axis[j]);
			WriteFloat(bytes, offset + 56, cluster.normal_cone.cutoff);
			WriteFloat(bytes, offset + 60, cluster.error);
			WriteFloat(bytes, offset + 64, cluster.parent_error);
		}
		for (std::size_t i = 0; i < asset.groups.size(); ++i)
		{
			const ClusterLodGroup &group = asset.groups[i];
			const std::size_t offset = offsets[2] + i * GroupRecordSize;
			WriteU32(bytes, offset, group.input_cluster_offset);
			WriteU32(bytes, offset + 4, group.input_cluster_count);
			WriteU32(bytes, offset + 8, group.output_cluster_offset);
			WriteU32(bytes, offset + 12, group.output_cluster_count);
			WriteU32(bytes, offset + 16, group.child_group_offset);
			WriteU32(bytes, offset + 20, group.child_group_count);
			WriteU32(bytes, offset + 24, group.parent_group_index);
			WriteU32(bytes, offset + 28, group.level);
			for (int j = 0; j < 3; ++j)
				WriteFloat(bytes, offset + 32 + j * 4, group.bounds.center[j]);
			WriteFloat(bytes, offset + 44, group.bounds.radius);
			WriteFloat(bytes, offset + 48, group.local_error);
			WriteFloat(bytes, offset + 52, group.error);
			WriteFloat(bytes, offset + 56, group.parent_error);
		}
		for (std::size_t i = 0; i < asset.group_input_clusters.size(); ++i)
			WriteU32(bytes, offsets[3] + i * 4, asset.group_input_clusters[i]);
		for (std::size_t i = 0; i < asset.group_children.size(); ++i)
			WriteU32(bytes, offsets[4] + i * 4, asset.group_children[i]);
		for (std::size_t i = 0; i < normalized.gpu_clusters.size(); ++i)
		{
			const auto &gpu = normalized.gpu_clusters[i];
			const std::size_t offset = offsets[5] + i * 64;
			for (int j = 0; j < 3; ++j)
				WriteFloat(bytes, offset + j * 4, gpu.center[j]);
			WriteFloat(bytes, offset + 12, gpu.radius);
			for (int j = 0; j < 3; ++j)
				WriteFloat(bytes, offset + 16 + j * 4, gpu.cone_axis[j]);
			WriteFloat(bytes, offset + 28, gpu.cone_cutoff);
			WriteFloat(bytes, offset + 32, gpu.error);
			WriteFloat(bytes, offset + 36, gpu.parent_error);
			WriteU32(bytes, offset + 40, gpu.group_index);
			WriteU32(bytes, offset + 44, gpu.parent_group_index);
			WriteU32(bytes, offset + 48, gpu.first_index);
			WriteU32(bytes, offset + 52, gpu.index_count);
			WriteU32(bytes, offset + 56, gpu.material_index);
			WriteU32(bytes, offset + 60, gpu.level);
		}
		for (std::size_t i = 0; i < normalized.gpu_groups.size(); ++i)
		{
			const auto &gpu = normalized.gpu_groups[i];
			const std::size_t offset = offsets[6] + i * 64;
			for (int j = 0; j < 3; ++j)
				WriteFloat(bytes, offset + j * 4, gpu.center[j]);
			WriteFloat(bytes, offset + 12, gpu.radius);
			WriteFloat(bytes, offset + 16, gpu.local_error);
			WriteFloat(bytes, offset + 20, gpu.error);
			WriteFloat(bytes, offset + 24, gpu.parent_error);
			WriteU32(bytes, offset + 28, gpu.parent_group_index);
			WriteU32(bytes, offset + 32, gpu.input_cluster_offset);
			WriteU32(bytes, offset + 36, gpu.input_cluster_count);
			WriteU32(bytes, offset + 40, gpu.output_cluster_offset);
			WriteU32(bytes, offset + 44, gpu.output_cluster_count);
			WriteU32(bytes, offset + 48, gpu.child_group_offset);
			WriteU32(bytes, offset + 52, gpu.child_group_count);
			WriteU32(bytes, offset + 56, gpu.level);
			WriteU32(bytes, offset + 60, gpu.padding);
		}
		return true;
	}

	ClusterLodPayloadResult ReadClusterLodPayload(const std::uint8_t *data, std::size_t size)
	{
		if (!data || size < HeaderSize)
			return ReadFailure("payload is shorter than the header");
		for (int i = 0; i < 4; ++i)
		{
			if (data[i] != Magic[i])
				return ReadFailure("payload magic is invalid");
		}
		const std::uint32_t schema = ReadU32(data, 4);
		const std::uint32_t builder = ReadU32(data, 8);
		const std::uint32_t meshoptimizer = ReadU32(data, 12);
		if (schema != ClusterLodPayloadSchemaVersion || builder != ClusterLodBuilderVersion
			|| meshoptimizer != ClusterLodMeshoptimizerVersion)
			return ReadFailure("payload schema or builder version is incompatible");
		if (ReadU32(data, 16) != ClusterLodPayloadHeaderSize)
			return ReadFailure("payload header size is incompatible");
		for (int i = 104; i < 128; ++i)
		{
			if (data[i] != 0)
				return ReadFailure("payload reserved header bytes are nonzero");
		}

		std::uint32_t counts[5];
		for (int i = 0; i < 5; ++i)
			counts[i] = ReadU32(data, 56 + i * 4);
		if (counts[0] == 0 || counts[1] == 0)
			return ReadFailure("payload has empty required sections");
		std::uint32_t expected_offsets[7];
		std::uint32_t total = 0;
		if (!ComputeLayout(counts, expected_offsets, total) || total != size)
			return ReadFailure("payload section sizes are invalid or truncated");
		for (int i = 0; i < 7; ++i)
		{
			if (ReadU32(data, 76 + i * 4) != expected_offsets[i]
				|| expected_offsets[i] % 16 != 0)
				return ReadFailure("payload section alignment or offsets are invalid");
		}

		auto asset = std::make_shared<ClusterLodAsset>();
		asset->options.cluster_triangle_limit = ReadU32(data, 32);
		asset->options.cluster_vertex_limit = ReadU32(data, 36);
		asset->options.group_cluster_limit = ReadU32(data, 40);
		asset->options.reduction_ratio = ReadFloat(data, 44);
		asset->options.target_error = ReadFloat(data, 48);
		std::vector<std::uint32_t> indices(counts[0]);
		for (std::size_t i = 0; i < indices.size(); ++i)
			indices[i] = ReadU32(data, expected_offsets[0] + i * 4);
		asset->SetMergedIndices(std::move(indices));
		asset->clusters.resize(counts[1]);
		for (std::size_t i = 0; i < asset->clusters.size(); ++i)
		{
			ClusterLodCluster &cluster = asset->clusters[i];
			const std::size_t offset = expected_offsets[1] + i * ClusterRecordSize;
			cluster.first_index = ReadU32(data, offset);
			cluster.index_count = ReadU32(data, offset + 4);
			cluster.vertex_count = ReadU32(data, offset + 8);
			cluster.material_index = ReadU32(data, offset + 12);
			cluster.group_index = ReadU32(data, offset + 16);
			cluster.parent_group_index = ReadU32(data, offset + 20);
			cluster.level = ReadU32(data, offset + 24);
			for (int j = 0; j < 3; ++j)
				cluster.bounds.center[j] = ReadFloat(data, offset + 28 + j * 4);
			cluster.bounds.radius = ReadFloat(data, offset + 40);
			for (int j = 0; j < 3; ++j)
				cluster.normal_cone.axis[j] = ReadFloat(data, offset + 44 + j * 4);
			cluster.normal_cone.cutoff = ReadFloat(data, offset + 56);
			cluster.error = ReadFloat(data, offset + 60);
			cluster.parent_error = ReadFloat(data, offset + 64);
		}
		asset->groups.resize(counts[2]);
		for (std::size_t i = 0; i < asset->groups.size(); ++i)
		{
			ClusterLodGroup &group = asset->groups[i];
			const std::size_t offset = expected_offsets[2] + i * GroupRecordSize;
			group.input_cluster_offset = ReadU32(data, offset);
			group.input_cluster_count = ReadU32(data, offset + 4);
			group.output_cluster_offset = ReadU32(data, offset + 8);
			group.output_cluster_count = ReadU32(data, offset + 12);
			group.child_group_offset = ReadU32(data, offset + 16);
			group.child_group_count = ReadU32(data, offset + 20);
			group.parent_group_index = ReadU32(data, offset + 24);
			group.level = ReadU32(data, offset + 28);
			for (int j = 0; j < 3; ++j)
				group.bounds.center[j] = ReadFloat(data, offset + 32 + j * 4);
			group.bounds.radius = ReadFloat(data, offset + 44);
			group.local_error = ReadFloat(data, offset + 48);
			group.error = ReadFloat(data, offset + 52);
			group.parent_error = ReadFloat(data, offset + 56);
		}
		asset->group_input_clusters.resize(counts[3]);
		for (std::size_t i = 0; i < counts[3]; ++i)
			asset->group_input_clusters[i] = ReadU32(data, expected_offsets[3] + i * 4);
		asset->group_children.resize(counts[4]);
		for (std::size_t i = 0; i < counts[4]; ++i)
			asset->group_children[i] = ReadU32(data, expected_offsets[4] + i * 4);

		std::vector<ClusterLodGpuCluster> gpu_clusters(counts[1]);
		for (std::size_t i = 0; i < gpu_clusters.size(); ++i)
		{
			auto &gpu = gpu_clusters[i];
			const std::size_t offset = expected_offsets[5] + i * 64;
			for (int j = 0; j < 3; ++j)
				gpu.center[j] = ReadFloat(data, offset + j * 4);
			gpu.radius = ReadFloat(data, offset + 12);
			for (int j = 0; j < 3; ++j)
				gpu.cone_axis[j] = ReadFloat(data, offset + 16 + j * 4);
			gpu.cone_cutoff = ReadFloat(data, offset + 28);
			gpu.error = ReadFloat(data, offset + 32);
			gpu.parent_error = ReadFloat(data, offset + 36);
			gpu.group_index = ReadU32(data, offset + 40);
			gpu.parent_group_index = ReadU32(data, offset + 44);
			gpu.first_index = ReadU32(data, offset + 48);
			gpu.index_count = ReadU32(data, offset + 52);
			gpu.material_index = ReadU32(data, offset + 56);
			gpu.level = ReadU32(data, offset + 60);
		}
		std::vector<ClusterLodGpuGroup> gpu_groups(counts[2]);
		for (std::size_t i = 0; i < gpu_groups.size(); ++i)
		{
			auto &gpu = gpu_groups[i];
			const std::size_t offset = expected_offsets[6] + i * 64;
			for (int j = 0; j < 3; ++j)
				gpu.center[j] = ReadFloat(data, offset + j * 4);
			gpu.radius = ReadFloat(data, offset + 12);
			gpu.local_error = ReadFloat(data, offset + 16);
			gpu.error = ReadFloat(data, offset + 20);
			gpu.parent_error = ReadFloat(data, offset + 24);
			gpu.parent_group_index = ReadU32(data, offset + 28);
			gpu.input_cluster_offset = ReadU32(data, offset + 32);
			gpu.input_cluster_count = ReadU32(data, offset + 36);
			gpu.output_cluster_offset = ReadU32(data, offset + 40);
			gpu.output_cluster_count = ReadU32(data, offset + 44);
			gpu.child_group_offset = ReadU32(data, offset + 48);
			gpu.child_group_count = ReadU32(data, offset + 52);
			gpu.level = ReadU32(data, offset + 56);
			gpu.padding = ReadU32(data, offset + 60);
			if (gpu.padding != 0)
				return ReadFailure("GPU group padding is nonzero");
		}
		asset->RebuildGpuRecords();
		const bool cluster_mismatch = !gpu_clusters.empty()
			&& std::memcmp(asset->gpu_clusters.data(), gpu_clusters.data(),
				gpu_clusters.size() * sizeof(ClusterLodGpuCluster)) != 0;
		const bool group_mismatch = !gpu_groups.empty()
			&& std::memcmp(asset->gpu_groups.data(), gpu_groups.data(),
				gpu_groups.size() * sizeof(ClusterLodGpuGroup)) != 0;
		if (cluster_mismatch || group_mismatch)
			return ReadFailure("CPU and GPU payload records disagree");

		ClusterLodPayloadResult result;
		result.source_hash = ReadU64(data, 24);
		result.schema_version = schema;
		result.builder_version = builder;
		result.meshoptimizer_version = meshoptimizer;
		result.vertex_count = ReadU32(data, 52);
		if (!ValidateClusterLodAsset(*asset, result.vertex_count, result.error))
			return result;
		result.asset = std::move(asset);
		return result;
	}
}
