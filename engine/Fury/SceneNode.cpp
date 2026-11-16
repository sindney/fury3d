#include "Fury/MathUtil.h"
#include "Fury/Archive.h"
#include "Fury/BodySetup.h"
#include "Fury/Camera.h"
#include "Fury/CharacterController.h"
#include "Fury/Component.h"
#include "Fury/FileUtil.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/OcTreeNode.h"
#include "Fury/OcTree.h"
#include "Fury/PlayerController.h"
#include "Fury/SceneNode.h"
#include "Fury/MeshRender.h"
#include "Fury/InstancedMeshRender.h"
#include "Fury/Transform.h"
#include "Fury/AnimationPlayer.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/Terrain.h"
#include "Fury/OceanComponent.h"
#include "Fury/BuoyancyComponent.h"

#include <map>

namespace fury
{
	std::unordered_map<std::string, std::function<Component::Ptr()>> SceneNode::ComponentRegistry =
	{
		{ "MeshRender",       []() -> Component::Ptr { return MeshRender::Create(nullptr, nullptr); } },
		{ "Light",            []() -> Component::Ptr { return Light::Create(); } },
		{ "Camera",           []() -> Component::Ptr { return Camera::Create(); } },
		{ "Transform",        []() -> Component::Ptr { return Transform::Create(); } },
		{ "Animator",         []() -> Component::Ptr { return Animator::Create(); } },
		{ "ParticleRenderer", []() -> Component::Ptr { return ParticleRenderer::Create(); } },
		{ "BodySetup",        []() -> Component::Ptr { return BodySetup::Create(); } },
		{ "FreeFlyController", []() -> Component::Ptr { return FreeFlyController::Create(); } },
		{ "CharacterController", []() -> Component::Ptr { return CharacterController::Create(); } },
		{ "SkyAtmosphere",    []() -> Component::Ptr { return SkyAtmosphere::Create(); } },
		{ "Terrain",          []() -> Component::Ptr { return Terrain::Create(); } },
		{ "OceanComponent",   []() -> Component::Ptr { return OceanComponent::Create(); } },
		{ "BuoyancyComponent", []() -> Component::Ptr { return BuoyancyComponent::Create(); } },
		{ "InstancedMeshRender", []() -> Component::Ptr { return InstancedMeshRender::Create(nullptr, nullptr); } }
	};

	std::string SceneNode::GetComponentTypeName(std::type_index type)
	{
		static const std::map<std::type_index, std::string> names = []()
		{
			std::map<std::type_index, std::string> out;
			for (const auto &kv : ComponentRegistry)
			{
				auto component = kv.second();
				if (component) out[component->GetTypeIndex()] = kv.first;
			}
			return out;
		}();
		auto it = names.find(type);
		return it == names.end() ? "" : it->second;
	}

	SceneNode::Ptr SceneNode::Create(const std::string &name)
	{
		return std::make_shared<SceneNode>(name);
	}

	SceneNode::SceneNode(const std::string &name)
		: Entity(name), m_TransformDirty(true), m_LocalScale(1.0f, 1.0f, 1.0f, 1.0f)
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

	void SceneNode::Serialize(Archive& ar)
	{
		if (m_EditorOnly && !ar.IsLoading()) return;
		Entity::SerializeFields(ar);
		if (ar.IsLoading())
		{
			RemoveAllComponents();
			RemoveAllChilds();
			m_EditorOnly = false;
		}
		ar << Required("pos", m_LocalPosition);
		ar << Required("rot", m_LocalRotation);
		ar << Required("scl", m_LocalScale);
		ar << Optional("aabb", m_ModelAABB);
		if (ar.IsLoading()) Recompose(true);

		if (ar.EnterArray("components"))
		{
			if (ar.IsLoading())
			{
				for (size_t i = 0; i < ar.GetArraySize(); ++i)
				{
					ar.SetArrayIndex(i);
					if (!ar.EnterObject()) continue;
					std::string type;
					ar << Required("type", type);
					auto it = ComponentRegistry.find(type);
					if (it == ComponentRegistry.end())
					{
						FURYW << "Component " << type << " not found in SceneNode::ComponentRegistry!";
						ar.LeaveObject();
						continue;
					}
					auto component = it->second();
					component->Serialize(ar);
					AddComponent(component);
					ar.LeaveObject();
				}
			}
			else
			{
				std::vector<std::pair<std::string, Component::Ptr>> components;
				for (const auto& kv : m_Components)
				{
					std::string type = GetComponentTypeName(kv.first);
					if (!type.empty()) components.push_back({ type, kv.second });
				}
				std::sort(components.begin(), components.end(),
					[](const auto& a, const auto& b) { return a.first < b.first; });
				for (const auto& kv : components)
				{
					if (!ar.EnterObject()) continue;
					std::string type = kv.first;
					ar << Required("type", type);
					kv.second->Serialize(ar);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}

		if (ar.EnterArray("childs"))
		{
			if (ar.IsLoading())
			{
				for (size_t i = 0; i < ar.GetArraySize(); ++i)
				{
					ar.SetArrayIndex(i);
					if (!ar.EnterObject()) continue;
					auto child = SceneNode::Create("temp");
					child->Serialize(ar);
					AddChild(child);
					ar.LeaveObject();
				}
			}
			else
			{
				for (const auto& child : m_Childs)
				{
					if (child->IsEditorOnly()) continue;
					if (!ar.EnterObject()) continue;
					child->Serialize(ar);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}
	}

	SceneNode::Ptr SceneNode::Clone(const std::string &name) const
	{
		// Round-trip through Save/Load so component references resolve
		// against the active scene's EntityManager. The output's
		// "name" field is overwritten with the requested name.
		auto self = std::const_pointer_cast<SceneNode>(shared_from_this());
		std::string json = FileUtil::SerializeToString(self);
		auto ptr = SceneNode::Create(name);
		if (!FileUtil::DeserializeFromString(ptr, json))
		{
			FURYE << "SceneNode::Clone: deserialize failed for '" << name << "'";
			return nullptr;
		}
		ptr->SetName(name);
		return ptr;
	}

	SceneNode::Ptr SceneNode::CloneTree(const std::string &name) const
	{
		// Round-trip through Save/Load so component references resolve.
		// Robust against new component types -- no per-component Clone
		// override to maintain.
		auto self = std::const_pointer_cast<SceneNode>(shared_from_this());
		std::string json = FileUtil::SerializeToString(self);
		auto ptr = SceneNode::Create(name);
		if (!FileUtil::DeserializeFromString(ptr, json))
		{
			FURYE << "SceneNode::CloneTree: deserialize failed for '" << name << "'";
			return nullptr;
		}
		ptr->SetName(name);
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

	void SceneNode::UpdateOcTreeBounds()
	{
		if (m_OcTreeNode.expired())
			return;
		m_OcTreeNode.lock()->GetManager().UpdateSceneNode(shared_from_this());
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
		UpdateOcTreeBounds();

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
		unsigned int childCount = static_cast<int>(m_Childs.size());
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
		// Compare names directly; GetHashCode() hashes the UUID, so the
		// hashcode overloads keep their own (EntityManager-key) semantics.
		for (const auto &child : m_Childs)
		{
			if (child->GetName() == name)
				return child;
		}

		return nullptr;
	}

	SceneNode::Ptr SceneNode::FindChildRecursively(const std::string &name) const
	{
		std::vector<Ptr> nodeWithChilds;

		for (const auto &child : m_Childs)
		{
			if (child->GetName() == name)
				return child;

			if (child->GetChildCount() > 0)
				nodeWithChilds.push_back(child);
		}

		while (!nodeWithChilds.empty())
		{
			auto currentNode = nodeWithChilds.back();
			nodeWithChilds.pop_back();

			const unsigned int j = currentNode->GetChildCount();
			for (unsigned int i = 0; i < j; i++)
			{
				auto child = currentNode->GetChildAt(i);
				if (child->GetName() == name)
					return child;

				if (child->GetChildCount() > 0)
					nodeWithChilds.push_back(child);
			}
		}

		return nullptr;
	}

	SceneNode::Ptr SceneNode::FindChild(size_t hashcode) const
	{
		unsigned int childCount = static_cast<int>(m_Childs.size());
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
		unsigned int childCount = static_cast<int>(m_Childs.size());
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
		return static_cast<int>(m_Childs.size());
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