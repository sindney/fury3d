#ifndef _FURY_CLUSTERLODSELECTION_H_
#define _FURY_CLUSTERLODSELECTION_H_

#include <cstdint>
#include <vector>

#include "Fury/ClusterLodAsset.h"

namespace fury
{
	constexpr float ClusterLodDefaultProjectionHysteresis = 0.1f;

	struct ClusterLodSelectionParams
	{
		float camera_position[3] = { 0.0f, 0.0f, 0.0f };
		float world_scale = 1.0f;
		float projection_scale = 1.0f;
		float error_threshold = 0.0f;
		float distance_epsilon = 1e-3f;
		const float *world_matrix = nullptr;
	};

	FURY_API std::vector<std::uint32_t> SelectClusterLodCut(const ClusterLodAsset &asset,
		const ClusterLodSelectionParams &params);

	FURY_API float ClusterLodUpdateProjectionFactor(float desired, float previous,
		float relative_band = ClusterLodDefaultProjectionHysteresis);

	// A spectral-norm bound remains conservative for non-uniform scale and shear.
	FURY_API float ClusterLodConservativeScale(const float world_matrix[16]);
}

#endif // _FURY_CLUSTERLODSELECTION_H_
