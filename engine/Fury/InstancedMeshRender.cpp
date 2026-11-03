#include "Fury/EntityManager.h"
#include "Fury/Archive.h"
#include "Fury/Log.h"
#include "Fury/Frustum.h"
#include "Fury/InstancedMeshRender.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace fury
{
	InstancedMeshRender::Ptr InstancedMeshRender::Create(const std::shared_ptr<Mesh> &mesh, const std::shared_ptr<Material> &material)
	{
		return std::make_shared<InstancedMeshRender>(mesh, material);
	}

	InstancedMeshRender::InstancedMeshRender(const std::shared_ptr<Mesh> &mesh, const std::shared_ptr<Material> &material)
		: m_Mesh(mesh)
	{
		m_TypeIndex = typeid(InstancedMeshRender);
		if (material)
			SetMaterial(material);
	}

	void InstancedMeshRender::Serialize(Archive& ar)
	{
		auto manager = ar.GetEntityManager();
		if (!manager && Scene::Active) manager = Scene::Manager();

		std::string meshName;
		if (ar.IsSaving())
		{
			if (auto mesh = m_Mesh.lock())
				meshName = mesh->GetName();
		}
		ar << Conditional("mesh", meshName, !meshName.empty());
		if (ar.IsLoading())
		{
			if (!manager)
			{
				ar.SetError("active scene is null");
				return;
			}
			if (meshName.empty())
			{
				ar.SetError("mesh is missing");
				return;
			}
			auto mesh = manager->Get<Mesh>(meshName);
			if (!mesh)
			{
				ar.SetError("mesh " + meshName + " not found");
				return;
			}
			SetMesh(mesh);
		}

		ar.TransferNamed("cast_shadows", m_CastShadows,
			ArchiveFieldPolicy::Optional, true);
		ar.TransferNamed("hierarchical", m_Hierarchical,
			ArchiveFieldPolicy::Optional, true);
		ar.TransferNamed("cull_distance", m_CullDistance,
			ArchiveFieldPolicy::Optional, true);

		std::vector<std::string> materialNames;
		if (ar.IsSaving())
		{
			for (unsigned int i = 0; i < m_Materials.size(); i++)
			{
				if (auto material = m_Materials[i].lock())
					materialNames.push_back(material->GetName());
				else
					FURYW << "Found empty material pointer at " << i << "!";
			}
		}
		ar.Transfer("materials", materialNames, ArchiveFieldPolicy::Required, true);
		if (!ar.IsValid()) return;
		if (ar.IsLoading())
		{
			m_Materials.clear();
			for (const auto &name : materialNames)
			{
				auto material = manager->Get<Material>(name);
				if (!material)
				{
					ar.SetError("material " + name + " not found");
					return;
				}
				m_Materials.push_back(material);
			}
		}

		std::vector<Instance> instances;
		if (ar.IsSaving()) instances = m_Instances;
		if (ar.EnterArray("instances", ArchiveFieldPolicy::Optional, true))
		{
			if (ar.IsLoading()) instances.resize(ar.GetArraySize());
			for (std::size_t i = 0; i < instances.size(); ++i)
			{
				if (ar.IsLoading()) ar.SetArrayIndex(i);
				if (!ar.EnterObject())
				{
					ar.LeaveArray();
					return;
				}
				Instance &instance = instances[i];
				ar << Optional("position", instance.Position);
				ar << Optional("rotation", instance.Rotation);
				ar << Optional("scale", instance.Scale, Vector4(1.0f, 1.0f, 1.0f));
				ar.LeaveObject();
			}
			ar.LeaveArray();
		}
		if (ar.IsLoading())
		{
			m_Instances = std::move(instances);
			m_ShadowMatricesDirty = true;
			m_MatrixCacheDirty = true;
		}
	}

	Component::Ptr InstancedMeshRender::Clone() const
	{
		auto clone = InstancedMeshRender::Create(m_Mesh.lock(), nullptr);
		for (unsigned int i = 0; i < m_Materials.size(); i++)
			clone->SetMaterial(m_Materials[i].lock(), i);
		clone->m_CastShadows = m_CastShadows;
		clone->m_Hierarchical = m_Hierarchical;
		clone->m_Instances = m_Instances;
		clone->m_ShadowMatricesDirty = true;
		clone->m_MatrixCacheDirty = true;
		return clone;
	}

	void InstancedMeshRender::SetMesh(const std::shared_ptr<Mesh> &mesh)
	{
		m_Mesh = mesh;
		m_ShadowMatricesDirty = true;
		m_MatrixCacheDirty = true;
		if (!m_Owner.expired())
			RefreshOwnerAABB();
	}

	std::shared_ptr<Mesh> InstancedMeshRender::GetMesh() const
	{
		return m_Mesh.lock();
	}

	void InstancedMeshRender::SetMaterial(const std::shared_ptr<Material> &material, unsigned int index)
	{
		if (index < m_Materials.size())
			m_Materials[index] = material;
		else
			m_Materials.push_back(material);
	}

	std::shared_ptr<Material> InstancedMeshRender::GetMaterial(unsigned int index) const
	{
		if (index < m_Materials.size())
			return m_Materials[index].lock();
		return nullptr;
	}

	unsigned int InstancedMeshRender::GetMaterialCount() const
	{
		return static_cast<unsigned int>(m_Materials.size());
	}

	bool InstancedMeshRender::GetRenderable() const
	{
		auto mesh = m_Mesh.lock();
		if (!mesh || m_Instances.empty())
			return false;

		for (auto material : m_Materials)
			if (material.expired())
				return false;

		if (m_Materials.size() < mesh->GetSubMeshCount())
		{
			FURYW << "Material count and SubMesh count miss match!";
			return false;
		}

		return true;
	}

	bool InstancedMeshRender::GetCastShadows() const
	{
		return m_CastShadows;
	}

	void InstancedMeshRender::SetCastShadows(bool state)
	{
		m_CastShadows = state;
	}

	void InstancedMeshRender::SetInstance(unsigned int index, const Instance &instance)
	{
		if (index >= m_Instances.size())
			return;
		m_Instances[index] = instance;
		m_ShadowMatricesDirty = true;
		m_MatrixCacheDirty = true;
		RefreshOwnerAABB();
	}

	void InstancedMeshRender::AddInstance(const Instance &instance)
	{
		m_Instances.push_back(instance);
		m_ShadowMatricesDirty = true;
		m_MatrixCacheDirty = true;
		RefreshOwnerAABB();
	}

	void InstancedMeshRender::RemoveInstance(unsigned int index)
	{
		if (index >= m_Instances.size())
			return;
		m_Instances.erase(m_Instances.begin() + index);
		m_ShadowMatricesDirty = true;
		m_MatrixCacheDirty = true;
		RefreshOwnerAABB();
	}

	void InstancedMeshRender::ClearInstances()
	{
		m_Instances.clear();
		m_ShadowMatricesDirty = true;
		m_MatrixCacheDirty = true;
		RefreshOwnerAABB();
	}

	Matrix4 InstancedMeshRender::ComposeLocalMatrix(const Instance &instance)
	{
		Matrix4 local;
		local.AppendTranslation(instance.Position);
		local.AppendRotation(instance.Rotation);
		local.AppendScale(instance.Scale);
		return local;
	}

	BoxBounds InstancedMeshRender::GetAggregateAABB() const
	{
		auto mesh = m_Mesh.lock();
		if (!mesh || m_Instances.empty())
			return BoxBounds();

		const BoxBounds &meshAabb = mesh->GetAABB();
		if (meshAabb.GetInfinite())
			return meshAabb;

		Vector4 mn(1e30f, 1e30f, 1e30f);
		Vector4 mx(-1e30f, -1e30f, -1e30f);
		for (const auto &instance : m_Instances)
		{
			BoxBounds worldBox = ComposeLocalMatrix(instance).Multiply(meshAabb);
			Vector4 bmn = worldBox.GetMin(), bmx = worldBox.GetMax();
			mn.x = std::min(mn.x, bmn.x); mn.y = std::min(mn.y, bmn.y); mn.z = std::min(mn.z, bmn.z);
			mx.x = std::max(mx.x, bmx.x); mx.y = std::max(mx.y, bmx.y); mx.z = std::max(mx.z, bmx.z);
		}
		return BoxBounds(mn, mx);
	}

	std::shared_ptr<const InstancedRenderCaches> InstancedMeshRender::SnapshotRenderCaches()
	{
		auto mesh = m_Mesh.lock();
		auto owner = m_Owner.lock();
		if (!mesh || !owner || m_Instances.empty())
			return nullptr;

		const Matrix4 &nodeWorld = owner->GetWorldMatrix();

		// Rebuild the published snapshot only when instances were edited
		// or the owner node moved; the packet then just shares it.
		bool stale = m_MatrixCacheDirty || m_ShadowMatricesDirty ||
			!m_PublishedCaches ||
			m_PublishedCaches->worlds.size() != m_Instances.size() ||
			std::memcmp(m_CacheNodeWorld.Raw, nodeWorld.Raw, sizeof(float) * 16) != 0;
		if (!stale)
			return m_PublishedCaches;

		m_MatrixCacheDirty = false;
		m_ShadowMatricesDirty = false;
		m_CacheNodeWorld = nodeWorld;
		m_ShadowMatrices.clear();   // legacy member: shadow draws read caches now

		auto caches = std::make_shared<InstancedRenderCaches>();
		caches->worlds.resize(m_Instances.size());
		caches->aabbs.resize(m_Instances.size());
		const BoxBounds &meshAabb = mesh->GetAABB();
		for (size_t i = 0; i < m_Instances.size(); ++i)
		{
			Matrix4 worldMat = nodeWorld * ComposeLocalMatrix(m_Instances[i]);
			caches->worlds[i] = worldMat;
			caches->aabbs[i] = worldMat.Multiply(meshAabb);
		}
		m_PublishedCaches = caches;
		return caches;
	}

	void InstancedMeshRender::BuildVisibleBatches(const Frustum &frustum, const std::shared_ptr<SceneNode> &cameraNode)
	{
		m_Batches.clear();
		auto mesh = m_Mesh.lock();
		auto owner = m_Owner.lock();
		if (!mesh || !owner || m_Instances.empty())
			return;

		PacketInstanced pk;
		pk.mesh = mesh;
		for (unsigned int i = 0; i < GetMaterialCount(); ++i)
			pk.materials.push_back(GetMaterial(i));
		pk.castShadows = m_CastShadows;
		pk.cullDistance = m_CullDistance;
		pk.hierarchical = m_Hierarchical;
		pk.shadowLodTier = GetShadowLodTier();
		pk.aggregateAABB = owner->GetWorldAABB();
		pk.caches = SnapshotRenderCaches();
		if (!pk.caches)
			return;

		BuildInstancedBatches(pk, frustum, BuildPacketCamera(cameraNode), m_Batches);
	}

	// Per-instance cull + LOD bucket over packet data (render thread).
	void BuildInstancedBatches(const PacketInstanced &pk, const Frustum &frustum,
		const PacketCamera &cam, std::vector<InstanceBatch> &outBatches)
	{
		outBatches.clear();
		auto mesh = pk.mesh;
		if (!mesh || !pk.caches || pk.caches->worlds.empty())
			return;

		const unsigned int lodCount = mesh->GetLodCount();

		// ISM mode picks one tier for the whole component from the
		// aggregate bounds; HISM buckets per instance below.
		unsigned int ismTier = 0;
		if (!pk.hierarchical)
		{
			float coverage = MeshRender::ComputeCoverageForBounds(pk.aggregateAABB,
				cam.invertWorldMatrix, cam.fov);
			ismTier = MeshRender::PickLodForCoverage(*mesh, coverage);
			if (ismTier >= lodCount) ismTier = lodCount - 1;
		}

		std::unordered_map<unsigned int, unsigned int> tierToBatch;
		const bool hasCullDist = pk.cullDistance > 0.0f;
		const float cullDistSq = pk.cullDistance * pk.cullDistance;
		const Vector4 camPos = cam.worldPos;
		const auto &worlds = pk.caches->worlds;
		const auto &aabbs = pk.caches->aabbs;
		for (size_t i = 0; i < worlds.size(); ++i)
		{
			const Matrix4 &worldMat = worlds[i];
			const Vector4 worldPos(worldMat.Raw[12], worldMat.Raw[13], worldMat.Raw[14]);
			// Draw-distance cap first (cheap). The cap is jittered per
			// instance (same position hash as the LOD dither) so the kill
			// edge is a staggered band, not a hard line across the field.
			if (hasCullDist)
			{
				Vector4 d = worldPos - camPos;
				float jitter = MeshRender::ComputeLodJitter(worldPos);
				if (d.SquareLength() > cullDistSq * jitter * jitter)
					continue;
			}

			const BoxBounds &worldBox = aabbs[i];
			if (!frustum.IsInsideFast(worldBox))
				continue;

			unsigned int tier = ismTier;
			if (pk.hierarchical)
			{
				float coverage = MeshRender::ComputeCoverageForBounds(worldBox,
					cam.invertWorldMatrix, cam.fov);
				// dithered transitions: each instance swaps tiers at a
				// slightly different distance (no double draws, no pop)
				coverage *= MeshRender::ComputeLodJitter(worldPos);
				tier = MeshRender::PickLodForCoverage(*mesh, coverage);
			}

			auto it = tierToBatch.find(tier);
			if (it == tierToBatch.end())
			{
				InstanceBatch batch;
				batch.LodTier = tier;
				batch.Billboard = mesh->IsLodBillboard(tier);
				outBatches.push_back(std::move(batch));
				it = tierToBatch.emplace(tier, static_cast<unsigned int>(outBatches.size()) - 1).first;
			}
			outBatches[it->second].WorldMatrices.push_back(worldMat);
		}

		std::sort(outBatches.begin(), outBatches.end(),
			[](const InstanceBatch &a, const InstanceBatch &b) { return a.LodTier < b.LodTier; });
	}

	const std::vector<Matrix4> &InstancedMeshRender::GetShadowMatrices()
	{
		if (m_ShadowMatricesDirty)
		{
			m_ShadowMatricesDirty = false;
			m_ShadowMatrices.clear();
			auto owner = m_Owner.lock();
			if (owner)
			{
				const Matrix4 &nodeWorld = owner->GetWorldMatrix();
				m_ShadowMatrices.reserve(m_Instances.size());
				for (const auto &instance : m_Instances)
					m_ShadowMatrices.push_back(nodeWorld * ComposeLocalMatrix(instance));
			}
		}
		return m_ShadowMatrices;
	}

	unsigned int InstancedMeshRender::GetShadowLodTier() const
	{
		auto mesh = m_Mesh.lock();
		if (!mesh)
			return 0;
		// Only billboard-terminated chains (kraut trees) demote past LOD 0
		// (mirrors Pipeline's shadow LOD policy; plain chains' coarse tiers
		// deviate from the rendered surface and self-shadow badly).
		unsigned int count = mesh->GetLodCount();
		if (count <= 1 || !mesh->IsLodBillboard(count - 1))
			return 0;
		for (unsigned int i = count; i-- > 1;)
		{
			if (!mesh->IsLodBillboard(i))
				return i;
		}
		return 0;
	}

	void InstancedMeshRender::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		RefreshOwnerAABB();
	}

	void InstancedMeshRender::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnDetaching(node);
		node->SetModelAABB(BoxBounds());
	}

	void InstancedMeshRender::RefreshOwnerAABB()
	{
		if (m_Owner.expired())
			return;
		m_Owner.lock()->SetModelAABB(GetAggregateAABB());
	}
}
