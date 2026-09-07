#include "Fury/EntityManager.h"
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

	bool InstancedMeshRender::Load(const void* wrapper, bool object)
	{
		if (Scene::Active == nullptr)
		{
			FURYE << "Active Pipeline is null!";
			return false;
		}

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "InstancedMeshRender")
		{
			FURYE << "Invalide type " << str << "!";
			return false;
		}

		if (LoadMemberValue(wrapper, "mesh", str))
		{
			if (auto mesh = Scene::Manager()->Get<Mesh>(str))
				SetMesh(mesh);
			else
			{
				FURYE << "Mesh " << str << " not found!";
				return false;
			}
		}
		else
		{
			FURYE << "InstancedMeshRender: mesh not found!";
			return false;
		}

		LoadMemberValue(wrapper, "cast_shadows", m_CastShadows);
		LoadMemberValue(wrapper, "hierarchical", m_Hierarchical);
		LoadMemberValue(wrapper, "cull_distance", m_CullDistance);

		m_Materials.clear();
		if (!LoadArray(wrapper, "materials", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				FURYE << "materials is a string array!";
				return false;
			}
			if (auto material = Scene::Manager()->Get<Material>(str))
			{
				m_Materials.push_back(material);
				return true;
			}
			FURYE << "Material " << str << " not found!";
			return false;
		}))
		{
			return false;
		}

		m_Instances.clear();
		LoadArray(wrapper, "instances", [&](const void* node) -> bool
		{
			Instance instance;
			LoadMemberValue(node, "position", instance.Position);
			LoadMemberValue(node, "rotation", instance.Rotation);
			LoadMemberValue(node, "scale", instance.Scale);
			m_Instances.push_back(instance);
			return true;
		});

		m_ShadowMatricesDirty = true;
		m_MatrixCacheDirty = true;
		return true;
	}

	void InstancedMeshRender::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		SaveKey(wrapper, "type");
		SaveValue(wrapper, "InstancedMeshRender");

		if (auto ptr = m_Mesh.lock())
		{
			SaveKey(wrapper, "mesh");
			SaveValue(wrapper, ptr->GetName());
		}

		SaveKey(wrapper, "cast_shadows");
		SaveValue(wrapper, m_CastShadows);
		SaveKey(wrapper, "hierarchical");
		SaveValue(wrapper, m_Hierarchical);
		SaveKey(wrapper, "cull_distance");
		SaveValue(wrapper, m_CullDistance);

		SaveKey(wrapper, "materials");
		StartArray(wrapper);
		for (unsigned int i = 0; i < m_Materials.size(); i++)
		{
			if (auto ptr = m_Materials[i].lock())
				SaveValue(wrapper, ptr->GetName());
			else
				FURYW << "Found empty material pointer at " << i << "!";
		}
		EndArray(wrapper);

		SaveKey(wrapper, "instances");
		SaveArray(wrapper, static_cast<unsigned int>(m_Instances.size()), [&](unsigned int index)
		{
			const auto &instance = m_Instances[index];
			StartObject(wrapper);
			SaveKey(wrapper, "position");
			SaveValue(wrapper, instance.Position);
			SaveKey(wrapper, "rotation");
			SaveValue(wrapper, instance.Rotation);
			SaveKey(wrapper, "scale");
			SaveValue(wrapper, instance.Scale);
			EndObject(wrapper);
		});

		if (object)
			EndObject(wrapper);
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

	void InstancedMeshRender::BuildVisibleBatches(const Frustum &frustum, const std::shared_ptr<SceneNode> &cameraNode)
	{
		m_Batches.clear();
		auto mesh = m_Mesh.lock();
		auto owner = m_Owner.lock();
		if (!mesh || !owner || m_Instances.empty())
			return;

		const Matrix4 &nodeWorld = owner->GetWorldMatrix();
		const BoxBounds &meshAabb = mesh->GetAABB();
		const unsigned int lodCount = mesh->GetLodCount();

		// Rebuild the world matrix/AABB cache only when instances were
		// edited or the owner node moved -- the per-frame path below is
		// then just a frustum test + coverage eval per instance.
		bool cacheStale = m_MatrixCacheDirty ||
			m_WorldMatrixCache.size() != m_Instances.size() ||
			std::memcmp(m_CacheNodeWorld.Raw, nodeWorld.Raw, sizeof(float) * 16) != 0;
		if (cacheStale)
		{
			m_MatrixCacheDirty = false;
			m_CacheNodeWorld = nodeWorld;
			m_WorldMatrixCache.resize(m_Instances.size());
			m_WorldAABBCache.resize(m_Instances.size());
			for (size_t i = 0; i < m_Instances.size(); ++i)
			{
				Matrix4 worldMat = nodeWorld * ComposeLocalMatrix(m_Instances[i]);
				m_WorldMatrixCache[i] = worldMat;
				m_WorldAABBCache[i] = worldMat.Multiply(meshAabb);
			}
		}

		// ISM mode picks one tier for the whole component from the
		// aggregate bounds; HISM buckets per instance below.
		unsigned int ismTier = 0;
		if (!m_Hierarchical)
		{
			float coverage = MeshRender::ComputeCoverageForBounds(owner->GetWorldAABB(), cameraNode);
			ismTier = MeshRender::PickLodForCoverage(*mesh, coverage);
			if (ismTier >= lodCount) ismTier = lodCount - 1;
		}

		std::unordered_map<unsigned int, unsigned int> tierToBatch;
		const bool hasCullDist = m_CullDistance > 0.0f;
		const float cullDistSq = m_CullDistance * m_CullDistance;
		const Vector4 camPos = cameraNode ? cameraNode->GetWorldPosition() : Vector4(0.0f, 0.0f, 0.0f);
		for (size_t i = 0; i < m_Instances.size(); ++i)
		{
			const Matrix4 &worldMat = m_WorldMatrixCache[i];
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

			const BoxBounds &worldBox = m_WorldAABBCache[i];
			if (!frustum.IsInsideFast(worldBox))
				continue;

			unsigned int tier = ismTier;
			if (m_Hierarchical)
			{
				float coverage = MeshRender::ComputeCoverageForBounds(worldBox, cameraNode);
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
				m_Batches.push_back(std::move(batch));
				it = tierToBatch.emplace(tier, static_cast<unsigned int>(m_Batches.size()) - 1).first;
			}
			m_Batches[it->second].WorldMatrices.push_back(m_WorldMatrixCache[i]);
		}

		std::sort(m_Batches.begin(), m_Batches.end(),
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
