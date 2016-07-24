#include "Fury/EntityManager.h"
#include "Fury/Log.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Material.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Joint.h"

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
			if (auto mesh = Scene::Active->GetEntityManager()->Get<Mesh>(str))
			{
				SetMesh(mesh);
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

		// 次数有问题
		// load materials
		m_Materials.clear();
		if (!LoadArray(wrapper, "materials", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				FURYE << "materials is a string array!";
				return false;
			}
			if (auto material = Scene::Active->GetEntityManager()->Get<Material>(str))
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

		if (object)
			EndObject(wrapper);
	}

	Component::Ptr MeshRender::Clone() const
	{
		auto clone = MeshRender::Create(nullptr, m_Mesh.lock());
		unsigned int materialCount = m_Materials.size();

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
		return m_Materials.size();
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
}