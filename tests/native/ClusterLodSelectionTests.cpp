#include "Fury/ClusterLodSelection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

	ClusterLodAsset MakeChain()
	{
		ClusterLodAsset asset;
		asset.clusters.resize(3);
		asset.groups.resize(1);
		asset.groups[0].bounds.radius = 3.0f;
		asset.groups[0].error = 2.0f;
		asset.clusters[0].bounds.radius = 1.0f;
		asset.clusters[0].error = 0.0f;
		asset.clusters[0].parent_error = 2.0f;
		asset.clusters[0].parent_group_index = 0;
		asset.clusters[1].group_index = 0;
		asset.clusters[1].bounds.radius = 1.0f;
		asset.clusters[1].error = 2.0f;
		asset.clusters[1].parent_error = ClusterLodInfiniteError;
		asset.clusters[2].group_index = 0;
		asset.clusters[2].bounds.radius = 100.0f;
		asset.clusters[2].error = 2.0f;
		asset.clusters[2].parent_error = ClusterLodInfiniteError;
		return asset;
	}

	void TestSelection()
	{
		const ClusterLodAsset asset = MakeChain();
		ClusterLodSelectionParams params;
		params.camera_position[2] = 10.0f;
		params.projection_scale = 100.0f;
		params.error_threshold = 10.0f;
		CHECK(SelectClusterLodCut(asset, params) == std::vector<std::uint32_t>({ 0 }));
		params.error_threshold = 30.0f;
		CHECK(SelectClusterLodCut(asset, params) == std::vector<std::uint32_t>({ 1, 2 }));
		params.error_threshold = 24.0f;
		CHECK(SelectClusterLodCut(asset, params) == std::vector<std::uint32_t>({ 0 }));

		ClusterLodAsset root;
		root.clusters.resize(1);
		root.clusters[0].bounds.radius = 1.0f;
		params.error_threshold = 0.0f;
		CHECK(SelectClusterLodCut(root, params) == std::vector<std::uint32_t>({ 0 }));

		params.camera_position[2] = 0.0f;
		CHECK(SelectClusterLodCut(root, params) == std::vector<std::uint32_t>({ 0 }));
	}

	void TestHysteresis()
	{
		CHECK(ClusterLodUpdateProjectionFactor(105.0f, 100.0f) == 100.0f);
		CHECK(ClusterLodUpdateProjectionFactor(111.0f, 100.0f) == 111.0f);
		CHECK(ClusterLodUpdateProjectionFactor(89.0f, 100.0f) == 89.0f);
		CHECK(ClusterLodUpdateProjectionFactor(110.0f, 100.0f) == 100.0f);
		CHECK(ClusterLodUpdateProjectionFactor(90.0f, 100.0f) == 100.0f);
		CHECK(ClusterLodUpdateProjectionFactor(75.0f, 0.0f) == 75.0f);
	}

	void TestConservativeScale()
	{
		float matrix[16] = {
			2.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 2.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		CHECK(std::abs(ClusterLodConservativeScale(matrix) - 2.0f) < 1e-5f);
		matrix[4] = 2.0f;
		matrix[5] = 1.0f;
		matrix[8] = 0.0f;
		matrix[10] = 1.0f;
		matrix[0] = 1.0f;
		const float scale = ClusterLodConservativeScale(matrix);
		for (int i = 0; i < 16; ++i)
		{
			const float x = static_cast<float>(i & 1);
			const float y = static_cast<float>((i >> 1) & 1);
			const float z = static_cast<float>((i >> 2) & 1);
			const float tx = x + 2.0f * y;
			const float length = std::sqrt(tx * tx + y * y + z * z);
			const float source_length = std::sqrt(x * x + y * y + z * z);
			if (source_length > 0.0f)
				CHECK(length / source_length <= scale + 1e-5f);
		}
	}
}

int main()
{
	TestSelection();
	TestHysteresis();
	TestConservativeScale();
	if (failures != 0)
	{
		std::cerr << failures << " cluster LOD selection checks failed" << std::endl;
		return 1;
	}
	std::cout << "Cluster LOD selection tests passed" << std::endl;
	return 0;
}
