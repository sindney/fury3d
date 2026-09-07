#include "Fury/EntityManager.h"
#include "Fury/Log.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Material.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Joint.h"
#include "Fury/Camera.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace fury
{
	MeshRender::Ptr MeshRender::Create(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh)
	{
		return std::make_shared<MeshRender>(material, mesh);
	}

	MeshRender::MeshRender(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh)
		: m_Mesh(mesh) 
	{
		m_TypeIndex = typeid(MeshRender);
		SetMaterial(material);
		SetMesh(mesh);
	};

	bool MeshRender::Load(const void* wrapper, bool object)
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

		// check type
		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "MeshRender")
		{
			FURYE << "Invalide type " << str << "!";
			return false;
		}

		// load mesh
		if (LoadMemberValue(wrapper, "mesh", str))
		{
			if (auto mesh = Scene::Manager()->Get<Mesh>(str))
			{
				SetMesh(mesh);
				// Seed the per-instance cast_shadows from the asset's
				// flag when the field is absent from the file.
				// Pre-existing .json / .bin scenes don't store
				// cast_shadows on MeshRender; reading the mesh's
				// flag preserves their behavior. New writes always
				// include the field.
				m_CastShadows = mesh->GetCastShadows();
			}
			else
			{
				FURYE << "Mesh " << str << " not found!";
				return false;
			}
		}
		else
		{
			FURYE << "mesh " << str << " not found!";
			return false;
		}

		LoadMemberValue(wrapper, "cast_shadows", m_CastShadows);

		// load materials
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
			else
			{
				FURYE << "Material " << str << " not found!";
				return false;
			}
		}))
		{
			return false;
		}

		// The LOD chain now lives on the bound Mesh itself (saved
		// inline in the Mesh's JSON). MeshRender only carries the
		// per-instance runtime selection (m_ActiveLod), which is
		// recomputed every frame and not persisted.

		return true;
	}

	void MeshRender::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		// save typeinfo
		SaveKey(wrapper, "type");
		SaveValue(wrapper, "MeshRender");
		
		// save mesh
		if (auto ptr = m_Mesh.lock())
		{
			SaveKey(wrapper, "mesh");
			SaveValue(wrapper, ptr->GetName());
		}

		// Per-instance shadow-casting flag. The mesh itself has its
		// own cast_shadows (asset-level default), but MeshRender
		// overrides per-instance so a user can toggle one tank's
		// shadow without touching every other instance of the mesh.
		SaveKey(wrapper, "cast_shadows");
		SaveValue(wrapper, m_CastShadows);

		// save materials
		SaveKey(wrapper, "materials");
		StartArray(wrapper);
		for (unsigned int i = 0; i < m_Materials.size(); i++)
		{
			if (auto ptr = m_Materials[i].lock())
			{
				SaveValue(wrapper, ptr->GetName());
			}
			else
			{
				FURYW << "Found empty material pointer at " << i << "!";
			}
		}
		EndArray(wrapper);

		// The LOD chain lives on the bound Mesh (saved inline in the
		// Mesh's JSON). No per-MeshRender LOD data is persisted.

		if (object)
			EndObject(wrapper);
	}

	Component::Ptr MeshRender::Clone() const
	{
		auto clone = MeshRender::Create(nullptr, m_Mesh.lock());
		unsigned int materialCount = static_cast<int>(m_Materials.size());

		for (unsigned int i = 0; i < materialCount; i++)
		{
			auto material = m_Materials[i];
			clone->SetMaterial(material.lock(), i);
		}

		return clone;
	}

	void MeshRender::SetMaterial(const std::shared_ptr<Material> &material, unsigned int index)
	{
		if (index < m_Materials.size())
			m_Materials[index] = material;
		else
			m_Materials.push_back(material);
	}

	std::shared_ptr<Material> MeshRender::GetMaterial(unsigned int index) const
	{
		if (index < m_Materials.size())
			return m_Materials[index].lock();
		else
			return nullptr;
	}

	unsigned int MeshRender::GetMaterialCount() const
	{
		return static_cast<int>(m_Materials.size());
	}

	void MeshRender::SetMesh(const std::shared_ptr<Mesh> &mesh)
	{
		m_Mesh = mesh;

		if (!m_Owner.expired())
			OnAttaching(m_Owner.lock());
	}

	std::shared_ptr<Mesh> MeshRender::GetMesh() const
	{
		return m_Mesh.lock();
	}

	bool MeshRender::GetRenderable() const
	{
		if (m_Mesh.expired())
			return false;

		for (auto material : m_Materials)
			if (material.expired())
				return false;

		if (m_Materials.size() < m_Mesh.lock()->GetSubMeshCount())
		{
			FURYW << "Material count and SubMesh count miss match!";
			return false;
		}

		return true;
	}

	bool MeshRender::GetCastShadows() const
	{
		return m_CastShadows;
	}

	void MeshRender::SetCastShadows(bool state)
	{
		m_CastShadows = state;
	}

	void MeshRender::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		if (m_Mesh.expired())
			return;

		node->SetModelAABB(m_Mesh.lock()->GetAABB());
	}

	void MeshRender::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnDetaching(node);
		node->SetModelAABB(BoxBounds());
	}

	unsigned int MeshRender::GetActiveLod() const
	{
		return m_ActiveLod;
	}

	std::shared_ptr<Mesh> MeshRender::GetActiveMesh() const
	{
		auto base = m_Mesh.lock();
		if (!base) return nullptr;
		const unsigned int count = base->GetLodCount();
		if (count <= 1) return base;
		if (m_ActiveLod >= count) return base->GetLodMesh(count - 1);
		return base->GetLodMesh(m_ActiveLod);
	}

	void MeshRender::UpdateActiveLod(const std::shared_ptr<SceneNode> &cameraNode)
	{
		auto base = m_Mesh.lock();
		if (!base)
		{
			m_ActiveLod = 0;
			return;
		}
		const unsigned int count = base->GetLodCount();
		if (count <= 1)
		{
			m_ActiveLod = 0;
			return;
		}

		// Coverage is computed from the world-space AABB so that
		// meshes translated/rotated/scaled away from the origin still
		// yield correct screen-coverage values. A degenerate (zero
		// radius) bounds maps to full coverage -> LOD 0.
		auto owner = m_Owner.lock();
		BoxBounds aabb = (owner && !owner->GetWorldAABB().GetInfinite()) ? owner->GetWorldAABB() : base->GetAABB();
		float coverage = ComputeCoverageForBounds(aabb, cameraNode);
		if (owner && coverage >= 0.0f)
			coverage *= ComputeLodJitter(owner->GetWorldPosition());
		m_ActiveLod = PickLodForCoverage(*base, coverage);
	}

	float MeshRender::ComputeCoverageForBounds(const BoxBounds &worldAabb,
		const std::shared_ptr<SceneNode> &cameraNode)
	{
		if (!cameraNode)
			return -1.0f;
		auto camComp = cameraNode->GetComponent<Camera>();
		if (!camComp)
			return -1.0f;

		auto mn = worldAabb.GetMin();
		auto mx = worldAabb.GetMax();
		auto center = (mn + mx) * 0.5f;
		auto size = mx - mn;
		float radius = 0.5f * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
		if (radius < 1e-6f)
			return 1.0f;

		// View-space distance from the AABB center to the camera
		// origin. Combined with the perspective half-FOV tangent,
		// gives the fraction of the viewport the model's bounding
		// sphere fills vertically.
		auto viewMatrix = cameraNode->GetInvertWorldMatrix();
		auto centerView = viewMatrix.Multiply(center);
		float distance = std::fabs(centerView.z);
		if (distance < 1e-3f) distance = 1e-3f;

		float fov = camComp->GetFov();
		float halfFovTan = std::tan(fov * 0.5f);
		if (halfFovTan < 1e-6f)
			return -1.0f;
		// Coverage as the model's apparent height / viewport height.
		float coverage = (radius / distance) / halfFovTan;
		if (coverage > 1.0f) coverage = 1.0f;
		return coverage;
	}

	unsigned int MeshRender::PickLodForCoverage(const Mesh &mesh, float coverage)
	{
		const unsigned int count = mesh.GetLodCount();
		if (count <= 1)
			return 0;
		if (coverage < 0.0f)
			return count - 1;

		// threshold(i) is the coverage below which tier i activates
		// (Mesh.h contract); tier 0 owns [threshold(1), 1.0]. Walk
		// deep->shallow: each tier whose threshold the coverage still
		// meets promotes the pick one slot shallower. (The historical
		// shallow->deep walk compared against threshold(i) directly,
		// which shifted every boundary one tier deeper and left tier 0
		// reachable only at the coverage=1.0 clamp.)
		unsigned int picked = count - 1;
		for (unsigned int i = count - 1; i >= 1; --i)
		{
			if (coverage >= mesh.GetLodThreshold(i))
				picked = i - 1;
		}
		return picked;
	}

	float MeshRender::ComputeLodJitter(const Vector4 &worldPos)
	{
		// Transition band, default +/-15%. FNV-1a over the cm-quantized
		// position keeps the jitter stable per object across frames while
		// decorrelating neighbors; moving an object re-seeds it.
		// FURY_LOD_JITTER overrides the band: 0 disables (A/B baseline),
		// any other float is the band fraction (e.g. 0.3 = +/-30%).
		static const float kBand = []()
		{
			const char *env = std::getenv("FURY_LOD_JITTER");
			if (env == nullptr)
				return 0.15f;
			float v = static_cast<float>(std::atof(env));
			return (v >= 0.0f && v < 0.95f) ? v : 0.15f;
		}();
		if (kBand <= 0.0f)
			return 1.0f;
		unsigned int h = 2166136261u;
		const int q[3] = {
			static_cast<int>(worldPos.x),
			static_cast<int>(worldPos.y),
			static_cast<int>(worldPos.z)
		};
		for (int c = 0; c < 3; ++c)
		{
			for (int b = 0; b < 4; ++b)
			{
				h ^= static_cast<unsigned int>((q[c] >> (b * 8)) & 0xff);
				h *= 16777619u;
			}
		}
		float h01 = static_cast<float>(h) / 4294967295.0f;
		return 1.0f + kBand * (2.0f * h01 - 1.0f);
	}
}