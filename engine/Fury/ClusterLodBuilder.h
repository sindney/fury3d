#ifndef _FURY_CLUSTERLODBUILDER_H_
#define _FURY_CLUSTERLODBUILDER_H_

#include <cstddef>
#include <memory>
#include <string>

#include "Fury/ClusterLodAsset.h"

namespace fury
{
	class Mesh;

	struct ClusterLodSubmeshView
	{
		const unsigned int *indices = nullptr;
		std::size_t index_count = 0;
		std::uint32_t material_index = 0;
	};

	struct ClusterLodMeshView
	{
		const float *positions = nullptr;
		std::size_t vertex_count = 0;
		const ClusterLodSubmeshView *submeshes = nullptr;
		std::size_t submesh_count = 0;
		bool skinned = false;
	};

	struct ClusterLodBuildResult
	{
		std::shared_ptr<ClusterLodAsset> asset;
		std::string error;

		explicit operator bool() const { return asset != nullptr; }
	};

	FURY_API ClusterLodBuildResult BuildClusterLodLevelZero(const ClusterLodMeshView &mesh,
		const ClusterLodBuildOptions &options);

	FURY_API ClusterLodBuildResult BuildClusterLod(const ClusterLodMeshView &mesh,
		const ClusterLodBuildOptions &options);

	FURY_API ClusterLodBuildResult BuildClusterLod(const Mesh &mesh,
		const ClusterLodBuildOptions &options);
}

#endif // _FURY_CLUSTERLODBUILDER_H_
