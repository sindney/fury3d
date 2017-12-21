#include "Fury/MathUtil.h"
#include "Fury/Component.h"
#include "Fury/Log.h"
#include "Fury/Light.h"
#include "Fury/OcTreeNode.h"
#include "Fury/OcTree.h"
#include "Fury/SceneNode.h"
#include "Fury/EntityManager.h"
#include "Fury/Scene.h"
#include "Fury/MeshRender.h"
#include "Fury/Mesh.h"
#include "Fury/Material.h"

namespace fury
{
	std::unordered_map<std::string, std::function<Component::Ptr()>> SceneNode::ComponentRegistry = 
	{
		{ "MeshRender", []() -> Component::Ptr { return MeshRender::Create(nullptr, nullptr); } },
		{ "Light", []() -> Component::Ptr { return Light::Create(); } }
	};

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
		//FURYD << m_Name << " destoried.";
	}

	bool SceneNode::Load(const void* wrapper, bool object)
	{
		if (Scene::Active == nullptr)
		{
			FURYE << "Active Scene is null!";
			return false;
		}

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		// local translation
		if (!LoadMemberValue(wrapper, "pos", m_LocalPosition))
		{
			FURYE << "pos not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "scl", m_LocalScale))
		{
			FURYE << "scl not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "rot", m_LocalRotation))
		{
			FURYE << "rot not found!";
			return false;
		}

		// model aabb
		LoadMemberValue(wrapper, "aabb", m_ModelAABB);

		// apply transforms
		Recompose(true);

		// load components
		std::string type;
		if (!LoadArray(wrapper, "components", [&](const void* node) -> bool
		{
			if (!IsObject(node))
			{
				FURYE << "Component is an object!";
				return false;
			}

			if (!LoadMemberValue(node, "type", type))
			{
				FURYE << "type not found!";
				return false;
			}

			auto it = ComponentRegistry.find(type);
			if (it == ComponentRegistry.end())
			{
				FURYW << "Component " << type << " not found in SceneNode::ComponentRegistry!";
				return true;
			}

			auto component = it->second();
			if (component->Load(node))
			{
				AddComponent(component);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			return false;
		}

		// load childs
		if (!LoadArray(wrapper, "childs", [&](const void* node) -> bool
		{
			auto child = SceneNode::Create("temp");
			if (child->Load(node))
			{
				AddChild(child);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			return false;
		}
		
		return true;
	}

	void SceneNode::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "pos");
		SaveValue(wrapper, m_LocalPosition);

		SaveKey(wrapper, "rot");
		SaveValue(wrapper, m_LocalRotation);

		SaveKey(wrapper, "scl");
		SaveValue(wrapper, m_LocalScale);

		SaveKey(wrapper, "aabb");
		SaveValue(wrapper, m_ModelAABB);

		SaveKey(wrapper, "components");
		StartArray(wrapper);
		for (auto pair : m_Components)
			pair.second->Save(wrapper);
		EndArray(wrapper);

		SaveKey(wrapper, "childs");
		SaveArray(wrapper, m_Childs.size(), [&](unsigned int index)
		{
			m_Childs[index]->Save(wrapper);
		});

		if (object)
			EndObject(wrapper);
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
		SetLocalRoattion(MathUtil::EulerRadToQuat(
			Vector4(x * MathUtil::DegToRad, y * MathUtil::DegToRad, z * MathUtil::DegToRad)));
	}

	void SceneNode::SetLocalRoattion(Vector4 axis, float angle)
	{
		SetLocalRoattion(MathUtil::AxisRadToQuat(Vector4(axis, angle * MathUtil::DegToRad)));
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
			FURYW << "SceneNode " << node->GetName() << " not found!";
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
			FURYW << "Index out of range!";
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