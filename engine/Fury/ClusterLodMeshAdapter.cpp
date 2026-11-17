#include "Fury/ClusterLodBuilder.h"

#include <vector>

#include "Fury/Mesh.h"

namespace fury
{
	ClusterLodBuildResult BuildClusterLod(const Mesh &mesh, const ClusterLodBuildOptions &options)
	{
		ClusterLodBuildResult result;
		const unsigned int submesh_count = mesh.GetSubMeshCount();
		if (submesh_count == 0)
		{
			result.error = "mesh has no canonical submeshes";
			return result;
		}
		std::vector<ClusterLodSubmeshView> submeshes(submesh_count);
		for (unsigned int i = 0; i < submesh_count; ++i)
		{
			const auto submesh = mesh.GetSubMeshAt(i);
			if (!submesh)
			{
				result.error = "mesh contains a null canonical submesh";
				return result;
			}
			submeshes[i].indices = submesh->Indices.Data.data();
			submeshes[i].index_count = submesh->Indices.Data.size();
			submeshes[i].material_index = i;
		}

		ClusterLodMeshView view;
		view.positions = mesh.Positions.Data.data();
		view.vertex_count = mesh.Positions.Data.size() / 3;
		view.submeshes = submeshes.data();
		view.submesh_count = submeshes.size();
		view.skinned = mesh.IsSkinnedMesh();
		return BuildClusterLod(view, options);
	}
}
