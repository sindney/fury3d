#include "Fury/ClusterLodImport.h"

#include "Fury/ClusterLodBuilder.h"
#include "Fury/ClusterLodPayloadStore.h"
#include "Fury/DdcStore.h"
#include "Fury/Mesh.h"

namespace fury
{
	bool IsClusterLodEligibleMesh(const Mesh &mesh)
	{
		if (mesh.IsSkinnedMesh() || mesh.GetSubMeshCount() == 0 || mesh.Positions.Data.empty())
			return false;
		for (unsigned int i = 0; i < mesh.GetSubMeshCount(); ++i)
		{
			const auto submesh = mesh.GetSubMeshAt(i);
			if (submesh && !submesh->Indices.Data.empty())
				return true;
		}
		return false;
	}

	bool PrepareClusterLodForImport(const std::shared_ptr<Mesh> &mesh,
		DdcStore &ddc, bool &cache_hit, std::string &error)
	{
		cache_hit = false;
		error.clear();
		if (!mesh)
		{
			error = "null mesh";
			return false;
		}
		if (!IsClusterLodEligibleMesh(*mesh))
			return true;

		ClusterLodDescriptor descriptor;
		descriptor.content_hash = MeshContentHash(mesh.get());
		descriptor.enabled = true;
		const std::uint32_t vertex_count = mesh->Positions.Data.size() / 3;
		ClusterLodPayloadResult cached = LoadClusterLodPayload(descriptor, vertex_count, ddc, error);
		if (cached)
		{
			mesh->SetClusterLodDescriptor(descriptor);
			mesh->SetClusterLodAsset(cached.asset);
			cache_hit = true;
			return true;
		}

		ClusterLodBuildResult built = BuildClusterLod(*mesh, descriptor.options);
		if (!built)
		{
			error = "mesh '" + mesh->GetName() + "': " + built.error;
			return false;
		}
		if (!StoreClusterLodPayload(ddc, *built.asset, descriptor, vertex_count, error))
		{
			error = "mesh '" + mesh->GetName() + "': " + error;
			return false;
		}
		mesh->SetClusterLodDescriptor(descriptor);
		mesh->SetClusterLodAsset(built.asset);
		return true;
	}
}
