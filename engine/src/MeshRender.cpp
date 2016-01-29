#include "Debug.h"
#include "Mesh.h"
#include "MeshRender.h"
#include "Material.h"
#include "SceneNode.h"

namespace fury
{
	MeshRender::Ptr MeshRender::Create(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh)
	{
		return std::make_shared<MeshRender>(material, mesh);
	}

	MeshRender::MeshRender(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh)
		: m_Material(material), m_Mesh(mesh) 
	{
		m_TypeIndex = typeid(MeshRender);
	};

	Component::Ptr MeshRender::Clone() const
	{
		return MeshRender::Create(m_Material.lock(), m_Mesh.lock());
	}

	void MeshRender::SetMaterial(const std::shared_ptr<Material> &material)
	{
		m_Material = material;
	}

	std::shared_ptr<Material> MeshRender::GetMaterial() const
	{
		return m_Material.lock();
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
		return !(m_Mesh.expired() || m_Material.expired());
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