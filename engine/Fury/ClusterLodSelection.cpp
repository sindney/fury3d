#include "Fury/ClusterLodSelection.h"

#include <algorithm>
#include <cmath>

namespace fury
{
	std::vector<std::uint32_t> SelectClusterLodCut(const ClusterLodAsset &asset,
		const ClusterLodSelectionParams &params)
	{
		std::vector<std::uint32_t> selected;
		const float scale = params.world_scale * params.projection_scale;
		const float epsilon = std::max(params.distance_epsilon, 1e-6f);
		const auto conservative_distance = [&](const ClusterLodSphere &bounds)
		{
			float center[3] = { bounds.center[0], bounds.center[1], bounds.center[2] };
			float radius = bounds.radius;
			if (params.world_matrix)
			{
				const float *m = params.world_matrix;
				for (int row = 0; row < 3; ++row)
					center[row] = m[row] * bounds.center[0] + m[4 + row] * bounds.center[1]
						+ m[8 + row] * bounds.center[2] + m[12 + row];
				radius *= params.world_scale;
			}
			const float x = params.camera_position[0] - center[0];
			const float y = params.camera_position[1] - center[1];
			const float z = params.camera_position[2] - center[2];
			return std::max(std::sqrt(x * x + y * y + z * z) - radius, epsilon);
		};
		for (std::size_t i = 0; i < asset.clusters.size(); ++i)
		{
			const ClusterLodCluster &cluster = asset.clusters[i];
			const ClusterLodSphere *self_bounds = &cluster.bounds;
			if (cluster.group_index < asset.groups.size())
				self_bounds = &asset.groups[cluster.group_index].bounds;
			const float self_distance = conservative_distance(*self_bounds);
			const float self_error = cluster.error * scale / self_distance;
			float parent_error = ClusterLodInfiniteError;
			if (cluster.parent_group_index < asset.groups.size())
			{
				const ClusterLodGroup &parent = asset.groups[cluster.parent_group_index];
				parent_error = cluster.parent_error * scale / conservative_distance(parent.bounds);
			}
			if (self_error <= params.error_threshold && parent_error > params.error_threshold)
				selected.push_back(static_cast<std::uint32_t>(i));
		}
		return selected;
	}

	float ClusterLodUpdateProjectionFactor(float desired, float previous, float relative_band)
	{
		if (!std::isfinite(previous) || previous <= 0.0f)
			return desired;
		const float band = std::max(0.0f, relative_band);
		const float lower = previous * (1.0f - band);
		const float upper = previous * (1.0f + band);
		return desired < lower || desired > upper ? desired : previous;
	}

	float ClusterLodConservativeScale(const float world_matrix[16])
	{
		float squared_scale = 0.0f;
		float column_sum = 0.0f;
		float row_sum = 0.0f;
		for (int column = 0; column < 3; ++column)
		{
			float current_column = 0.0f;
			for (int row = 0; row < 3; ++row)
			{
				const float value = world_matrix[column * 4 + row];
				squared_scale += value * value;
				current_column += std::abs(value);
			}
			column_sum = std::max(column_sum, current_column);
		}
		for (int row = 0; row < 3; ++row)
		{
			float current_row = 0.0f;
			for (int column = 0; column < 3; ++column)
				current_row += std::abs(world_matrix[column * 4 + row]);
			row_sum = std::max(row_sum, current_row);
		}
		return std::min(std::sqrt(squared_scale), std::sqrt(column_sum * row_sum));
	}
}
