#ifndef _FURY_CLUSTERLODSERIALIZATION_H_
#define _FURY_CLUSTERLODSERIALIZATION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Fury/ClusterLodAsset.h"

namespace fury
{
	constexpr std::uint32_t ClusterLodPayloadSchemaVersion = 1;
	constexpr std::uint32_t ClusterLodBuilderVersion = 1;
	constexpr std::uint32_t ClusterLodMeshoptimizerVersion = 190;
	constexpr std::uint32_t ClusterLodPayloadHeaderSize = 128;

	struct ClusterLodPayloadResult
	{
		std::shared_ptr<ClusterLodAsset> asset;
		std::uint64_t source_hash = 0;
		std::uint32_t schema_version = 0;
		std::uint32_t builder_version = 0;
		std::uint32_t meshoptimizer_version = 0;
		std::uint32_t vertex_count = 0;
		std::string error;

		explicit operator bool() const { return asset != nullptr; }
	};

	FURY_API bool WriteClusterLodPayload(const ClusterLodAsset &asset, std::uint64_t source_hash,
		std::uint32_t vertex_count, std::vector<std::uint8_t> &bytes, std::string &error);

	FURY_API ClusterLodPayloadResult ReadClusterLodPayload(const std::uint8_t *data, std::size_t size);
}

#endif // _FURY_CLUSTERLODSERIALIZATION_H_
