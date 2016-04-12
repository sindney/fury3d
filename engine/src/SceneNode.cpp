#include "Angle.h"
#include "Component.h"
#include "Debug.h"
#include "OcTreeNode.h"
#include "OcTree.h"
#include "SceneNode.h"

namespace fury
{
	const std::string SceneNode::EVENT_TRANSFORM_CHANGE = "event_transform_change";

	SceneNode::Ptr SceneNode::Create(const std::string &name)
	{
		return std::make_shared<SceneNode>(name);
	}

	SceneNode::SceneNode(const std::string &name)
		: Entity(name), m_LocalScale(1.0f, 1.0f, 1.0f, 1.0f), m_TransformDirty(true)
	{
		m_TypeIndex = typeid(SceneNode);
		OnTransformChange = Signal<const Ptr&>::Create();
	}

	SceneNode::~SceneNode()
	{
		RemoveAllComponents(true);
		RemoveAllChilds();
		LOGD << m_Name << " destoried.";
	}

	SceneNode::Ptr SceneNode::Clone(const std::string &name) const
	{
		auto ptr = SceneNode::Create(name);
		// clone components
		for (auto &comp : m_Components)
			ptr->AddComponent(comp.second->Clone());
		// clone translations
		ptr->SetLocalPosition(m_LocalPosition);
		ptr->SetLocalRoattion(m_LocalRotation);
		ptr->SetLocalScale(m_LocalScale);
		return ptr;
	}

	void SceneNode::SetOcTreeNode(const std::shared_ptr<OcTreeNode> &ocTreeNode)
	{
		m_OcTreeNode = ocTreeNode;
	}

	void SceneNode::RemoveFromOcTree(bool recursively)
	{
		if (!m_OcTreeNode.expired())
			m_OcTreeNode.lock()->RemoveSceneNode(shared_from_this());

		if (recursively)
		{
			for (unsigned int i = 0; i < m_Childs.size(); i++)
				m_Childs[i]->RemoveFromOcTree(true);
		}
	}

	void SceneNode::SetModelAABB(const BoxBounds &aabb)
	{
		if (aabb.GetInfinite())
		{
			m_ModelAABB = m_LocalAABB = m_WorldAABB = aabb;
		}
		else
		{
			m_ModelAABB = aabb;
			m_LocalAABB = m_LocalMatrix.Multiply(m_ModelAABB);
			m_WorldAABB = m_WorldMatrix.Multiply(m_ModelAABB);
		}
	}

	BoxBounds SceneNode::GetModelAABB() const
	{
		return m_ModelAABB;
	}

	BoxBounds SceneNode::GetLocalAABB() const
	{
		return m_LocalAABB;
	}

	BoxBounds SceneNode::GetWorldAABB() const
	{
		return m_WorldAABB;
	}

	//////////////////////////////////
	// Transforms
	//////////////////////////////////

	void SceneNode::Recompose(bool force)
	{
		if (!force && !m_TransformDirty)
			return;

		m_TransformDirty = false;

		// update local matrix
		m_LocalMatrix.Identity();
		m_LocalMatrix.AppendTranslation(m_LocalPosition);
		m_LocalMatrix.AppendRotation(m_LocalRotation);
		m_LocalMatrix.AppendScale(m_LocalScale);

		m_InvertLocalMatrix = m_LocalMatrix.Inverse();

		// update world matrix
		if (m_Parent.expired())
		{
			m_WorldMatrix = m_LocalMatrix;
			m_WorldPosition = m_LocalPosition;
			m_WorldRotation = m_LocalRotation;
			m_WorldScale = m_LocalScale;
		}
		else
		{
			Matrix4 matrix = m_Parent.lock()->GetWorldMatrix();
			m_WorldMatrix = matrix * m_LocalMatrix;
			m_WorldPosition = matrix.Multiply(m_LocalPosition);
			m_WorldRotation = matrix.Multiply(m_LocalRotation);
			m_WorldScale = matrix.Multiply(m_LocalScale);
		}
		m_InvertWorldMatrix = m_WorldMatrix.Inverse();

		// update bounding box
		SetModelAABB(m_ModelAABB);

		// update octree info
		if (!m_OcTreeNode.expired())
			m_OcTreeNode.lock()->GetManager().UpdateSceneNode(shared_from_this());

		// trigger event
		OnTransformChange->Emit(shared_from_this());

		// force update child nodes' matrix
		for (auto &child : m_Childs)
			child->Recompose(true);
	}

	Matrix4 SceneNode::GetLocalMatrix() const
	{
		return m_LocalMatrix;
	}

	Matrix4 SceneNode::GetInvertLocalMatrix() const
	{
		return m_InvertLocalMatrix;
	}

	Matrix4 SceneNode::GetWorldMatrix() const
	{
		return m_WorldMatrix;
	}

	Matrix4 SceneNode::GetInvertWorldMatrix() const
	{
		return m_InvertWorldMatrix;
	}

	Vector4 SceneNode::GetWorldPosition() const
	{
		return m_WorldPosition;
	}

	Quaternion SceneNode::GetWorldRoattion() const
	{
		return m_WorldRotation;
	}

	Vector4 SceneNode::GetWorldScale() const
	{
		return m_WorldScale;
	}

	Vector4 SceneNode::GetLocalPosition() const
	{
		return m_LocalPosition;
	}

	Quaternion SceneNode::GetLocalRoattion() const
	{
		return m_LocalRotation;
	}

	Vector4 SceneNode::GetLocalScale() const
	{
		return m_LocalScale;
	}

	void SceneNode::SetLocalPosition(Vector4 position)
	{
		if (m_TransformDirty || m_LocalPosition != position)
		{
			m_LocalPosition = position;
			m_TransformDirty = true;
		}
	}

	void SceneNode::SetLocalPosition(float x, float y, float z)
	{
		SetLocalPosition(Vector4(x, y, z));
	}

	void SceneNode::SetLocalRoattion(Quaternion rotation)
	{
		if (m_TransformDirty || m_LocalRotation != rotation)
		{
			m_LocalRotation = rotation;
			m_TransformDirty = true;
		}
	}

	void SceneNode::SetLocalRoattion(float x, float y, float z)
	{
		SetLocalRoattion(Angle::EulerRadToQuat(
			Vector4(x * Angle::DegToRad, y * Angle::DegToRad, z * Angle::DegToRad)));
	}

	void SceneNode::SetLocalRoattion(Vector4 axis, float angle)
	{
		SetLocalRoattion(Angle::AxisRadToQuat(Vector4(axis, angle * Angle::DegToRad)));
	}

	void SceneNode::SetLocalScale(Vector4 scale)
	{
		if (m_TransformDirty || m_LocalScale != scale)
		{
			m_LocalScale = scale;
			m_TransformDirty = true;
		}
	}

	void SceneNode::SetLocalScale(float factor)
	{
		SetLocalScale(Vector4(factor));
	}

	//////////////////////////////////
	// Hierarchy
	//////////////////////////////////

	void SceneNode::SetParent(const SceneNode::Ptr &parent)
	{
		m_Parent = parent;
		Recompose(true);
	}

	SceneNode::Ptr SceneNode::GetParent() const
	{
		return m_Parent.lock();
	}

	void SceneNode::AddChild(const SceneNode::Ptr &node)
	{
		if(node->GetParent())
			node->RemoveFromParent();

		m_Childs.push_back(node);
		node->SetParent(shared_from_this());
	}

	void SceneNode::RemoveChild(const SceneNode::Ptr &node)
	{
		unsigned int childCount = m_Childs.size();
		unsigned int i = 0;

		SceneNode::Ptr child = nullptr;

		for (i = 0; i < childCount; i++)
		{
			child = m_Childs[i];
			if (child == node)
				break;

			child = nullptr;
		}

		if (child != nullptr)
		{
			node->SetParent(nullptr);
			m_Childs.erase(m_Childs.begin() + i);
		}
		else
			LOGW << "SceneNode " << node->GetName() << " not found!";
	}

	SceneNode::Ptr SceneNode::RemoveChildAt(unsigned int index)
	{
		if(index < m_Childs.size())
		{
			auto child = m_Childs[index];
			child->SetParent(nullptr);
			m_Childs.erase(m_Childs.begin() + index);
			return child;
		}
		return nullptr;
	}

	void SceneNode::RemoveFromParent()
	{
		if (!m_Parent.expired())
			m_Parent.lock()->RemoveChild(shared_from_this());
	}

	void SceneNode::RemoveAllChilds()
	{
		Ptr node;
		while(m_Childs.size() > 0)
		{
			node = m_Childs.back();
			m_Childs.pop_back();

			node->SetParent(nullptr);
		}
	} 

	void SceneNode::Replace(const SceneNode::Ptr &node)
	{
		Ptr parent = node->GetParent();
		if(parent != nullptr)
		{
			parent->AddChild(shared_from_this());
			parent->RemoveChild(node);
		}

		Ptr child;
		while(node->GetChildCount() > 0)
		{
			AddChild(node->RemoveChildAt(0));
		}
	}

	SceneNode::Ptr SceneNode::FindChild(const std::string &name) const 
	{
		return FindChild(std::hash<std::string>()(name));
	}

	SceneNode::Ptr SceneNode::FindChildRecursively(const std::string &name) const
	{
		return FindChildRecursively(std::hash<std::string>()(name));
	}

	SceneNode::Ptr SceneNode::FindChild(size_t hashcode) const
	{
		unsigned int childCount = m_Childs.size();
		unsigned int i = 0;

		SceneNode::Ptr child = nullptr;

		for (i = 0; i < childCount; i++)
		{
			child = m_Childs[i];
			if (child->GetHashCode() == hashcode)
				return child;
		}

		return nullptr;
	}

	SceneNode::Ptr SceneNode::FindChildRecursively(size_t hashcode) const
	{
		std::vector<Ptr> nodeWithChilds;
		unsigned int childCount = m_Childs.size();
		unsigned int i, j;

		SceneNode::Ptr child = nullptr, currentNode = nullptr;

		for (i = 0; i < childCount; i++)
		{
			child = m_Childs[i];
			if (child->GetHashCode() == hashcode)
				return child;

			if (child->GetChildCount() > 0)
				nodeWithChilds.push_back(child);
		}

		while (!nodeWithChilds.empty())
		{
			currentNode = nodeWithChilds.back();
			nodeWithChilds.pop_back();

			j = currentNode->GetChildCount();
			for (i = 0; i < j; i++)
			{
				child = currentNode->GetChildAt(i);

				if (child->GetHashCode() == hashcode)
					return child;

				if (child->GetChildCount() > 0)
					nodeWithChilds.push_back(child);
			}
		}

		return nullptr;
	}

	unsigned int SceneNode::GetChildCount() const
	{
		return m_Childs.size();
	}

	SceneNode::Ptr SceneNode::GetChildAt(unsigned int index) const
	{
		if(index < m_Childs.size())
		{
			return m_Childs[index];
		}
		else
		{
			LOGW << "Index out of range!";
			return nullptr;
		}
	}

	//////////////////////////////////
	// Components
	//////////////////////////////////

	bool SceneNode::AddComponent(const Component::Ptr &ptr)
	{
		if (ptr->HasOwner())
			return false;

		auto it = m_Components.emplace(ptr->GetTypeIndex(), ptr);
		if (it.second)
		{
			ptr->OnAttaching(shared_from_this());
			return true;
		}
		return false;
	}

	bool SceneNode::RemoveComponent(std::type_index type)
	{
		auto it = m_Components.find(type);
		Component::Ptr ptr = nullptr;

		if (it != m_Components.end())
		{
			ptr = it->second;
			m_Components.erase(it);

			const std::type_index &typeIndex = ptr->GetTypeIndex();

			ptr->OnDetaching(shared_from_this());
			return true;
		}

		return false;
	}

	std::shared_ptr<Component> SceneNode::GetComponent(std::type_index type) const
	{
		auto it = m_Components.find(type);

		if (it != m_Components.end())
			return it->second;

		return nullptr;
	}

	void SceneNode::RemoveAllComponents(bool destructing)
	{
		for (auto it : m_Components)
		{
			if (destructing)
				it.second->OnOwnerDestructing(*this);
			else
				it.second->OnDetaching(shared_from_this());
		}
			
		m_Components.clear();
	}
}