#ifndef _FURY_CLUSTERLODVALIDATION_H_
#define _FURY_CLUSTERLODVALIDATION_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Fury/ClusterLodAsset.h"

namespace fury
{
	FURY_API bool ValidateClusterLodAsset(const ClusterLodAsset &asset,
		std::size_t vertex_count, std::string &error);

	FURY_API bool ValidateClusterLodCut(const ClusterLodAsset &asset,
		const std::vector<std::uint32_t> &selected, std::string &error);
}

#endif // _FURY_CLUSTERLODVALIDATION_H_
