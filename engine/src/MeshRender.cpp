#include "Log.h"
#include "Mesh.h"
#include "MeshRender.h"
#include "Material.h"
#include "SceneNode.h"
#include "Joint.h"

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
	};

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