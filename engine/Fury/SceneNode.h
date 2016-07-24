#ifndef _FURY_SCENENODE_H_
#define _FURY_SCENENODE_H_

#include <unordered_map>
#include <typeinfo>
#include <vector>

#include "Fury/BoxBounds.h"
#include "Fury/Entity.h"
#include "Fury/Quaternion.h"
#include "Fury/Matrix4.h"
#include "Fury/Vector4.h"
#include "Fury/Signal.h"

namespace fury
{
	class Component;

	class OcTreeNode;

	// To destory a scenenode.
	// Call node.RemoveFromParent + node.RemoveFromOcTree(true) + node.reset.
	// This node together with all it's childs will be destoried.
	class FURY_API SceneNode : public Entity, public std::enable_shared_from_this<SceneNode>
	{
		friend class OcTreeNode;

	public:

		typedef std::shared_ptr<SceneNode> Ptr;

		static Ptr Create(const std::string &name);

		// to enable serialization of custom component, registe to this map.
		static std::unordered_map<std::string, std::function<std::shared_ptr<Component>()>> ComponentRegistry;

	protected:

		std::weak_ptr<OcTreeNode> m_OcTreeNode;

		std::weak_ptr<SceneNode> m_Parent;

		std::vector<Ptr> m_Childs;

		std::unordered_map<std::type_index, std::shared_ptr<Component>> m_Components;

		BoxBounds m_ModelAABB;

		BoxBounds m_LocalAABB;

		BoxBounds m_WorldAABB;

		bool m_TransformDirty;

		Vector4 m_WorldPosition;

		Vector4 m_WorldScale;

		Quaternion m_WorldRotation;

		Vector4 m_LocalPosition;

		Vector4 m_LocalScale;

		Quaternion m_LocalRotation;

		Matrix4 m_LocalMatrix;

		Matrix4 m_InvertLocalMatrix;

		Matrix4 m_WorldMatrix;

		Matrix4 m_InvertWorldMatrix;

	public:

		Signal<const Ptr&>::Ptr OnTransformChange;

		SceneNode(const std::string &name);

		virtual ~SceneNode();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		// copies components and translations.
		Ptr Clone(const std::string &name) const;

		// remove this sceneNode from attached ocTree.
		// set recursively to true will call this on child nodes.
		void RemoveFromOcTree(bool recursively = false);

		void SetModelAABB(const BoxBounds &aabb);

		BoxBounds GetModelAABB() const;

		BoxBounds GetLocalAABB() const;

		BoxBounds GetWorldAABB() const;

		//////////////////////////////////
		// Transforms
		//////////////////////////////////

		void Recompose(bool force = false);

		Matrix4 GetLocalMatrix() const;

		Matrix4 GetInvertLocalMatrix() const;
		
		Matrix4 GetWorldMatrix() const;

		Matrix4 GetInvertWorldMatrix() const;

		Vector4 GetWorldPosition() const;

		Quaternion GetWorldRoattion() const;

		Vector4 GetWorldScale() const;

		Vector4 GetLocalPosition() const;

		Quaternion GetLocalRoattion() const;

		Vector4 GetLocalScale() const;

		void SetLocalPosition(Vector4 position);

		void SetLocalPosition(float x = 0.0f, float y = 0.0f, float z = 0.0f);

		void SetLocalRoattion(Quaternion rotation);

		// rotate in yxz order, in angles.
		void SetLocalRoattion(float x = 0.0f, float y = 0.0f, float z = 0.0f);

		void SetLocalRoattion(Vector4 axis, float angle);

		void SetLocalScale(Vector4 scale);
		
		// uniform scale
		void SetLocalScale(float factor = 1.0f);

		//////////////////////////////////
		// Hierarchy
		//////////////////////////////////

		Ptr GetParent() const;
		
		void AddChild(const Ptr &node);

		void RemoveChild(const Ptr &node);

		Ptr RemoveChildAt(unsigned int index);

		void RemoveFromParent();

		void RemoveAllChilds();

		/**
		 *	A.Replace(B): 
		 *	
		 *	Root	Root
		 *	|		|
		 *	B-C --> A-C
		 *	|		|
		 *	D		D
		 *	
		 *	Note: before replace, make sure this node has no parent and childs.
		 */
		void Replace(const Ptr &node);

		Ptr FindChild(const std::string &name) const;

		Ptr FindChildRecursively(const std::string &name) const;

		Ptr FindChild(size_t hashcode) const;

		Ptr FindChildRecursively(size_t hashcode) const;

		unsigned int GetChildCount() const;

		Ptr GetChildAt(unsigned int index) const;

		//////////////////////////////////
		// Components
		//////////////////////////////////

		bool AddComponent(const std::shared_ptr<Component> &ptr);

		bool RemoveComponent(std::type_index type);

		template<class ComponentType>
		std::shared_ptr<ComponentType> GetComponent() const;

		std::shared_ptr<Component> GetComponent(std::type_index type) const;

		void RemoveAllComponents(bool destructing = false);

	protected:

		void SetOcTreeNode(const std::shared_ptr<OcTreeNode> &ocTreeNode);

		void SetParent(const Ptr &parent);
	};

	template<class ComponentType>
	std::shared_ptr<ComponentType> SceneNode::GetComponent() const
	{
		return std::static_pointer_cast<ComponentType>(GetComponent(typeid(ComponentType)));
	}
}

#endif // _FURY_SCENENODE_H_