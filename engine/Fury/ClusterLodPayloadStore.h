#ifndef _FURY_CLUSTERLODPAYLOADSTORE_H_
#define _FURY_CLUSTERLODPAYLOADSTORE_H_

#include <cstdint>
#include <string>

#include "Fury/ClusterLodSerialization.h"

namespace fury
{
	class DdcStore;

	FURY_API std::string MakeClusterLodPayloadKey(const ClusterLodDescriptor &descriptor);

	FURY_API std::string MakeClusterLodPackedKey(const std::string &payload_key);

	FURY_API bool StoreClusterLodPayload(DdcStore &ddc, const ClusterLodAsset &asset,
		const ClusterLodDescriptor &descriptor, std::uint32_t vertex_count, std::string &error);

	FURY_API ClusterLodPayloadResult LoadClusterLodPayload(const ClusterLodDescriptor &descriptor,
		std::uint32_t expected_vertex_count, DdcStore &ddc, std::string &error);
}

#endif // _FURY_CLUSTERLODPAYLOADSTORE_H_
