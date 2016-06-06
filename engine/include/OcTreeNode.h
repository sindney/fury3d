#ifndef _FURY_OCTREENODE_H_
#define _FURY_OCTREENODE_H_

#include "SceneNode.h"

namespace fury
{
	class OcTree;

	/**

	Children layout: 
	  ____________
	 /__2__/__6_ /
	/__3__/__7__/
	  ____________
	 /__0__/__4_ /
	/__1__/__5__/

	*/
	class FURY_API OcTreeNode : public TypeComparable, public std::enable_shared_from_this<OcTreeNode>
	{
		friend class OcTree;

	public:

		typedef std::shared_ptr<OcTreeNode> Ptr;

		static Ptr Create(OcTree &manager, const OcTreeNode::Ptr &parent, 
			Vector4 min, Vector4 max);

	protected:

		std::type_index m_TypeIndex;

		BoxBounds m_AABB;

		OcTree& m_Manager;

		OcTreeNode::Ptr m_Childs[8];

		std::vector<std::shared_ptr<SceneNode>> m_SceneNodes;

		OcTreeNode::Ptr m_Parent;

		bool m_IsLeaf;

		unsigned int m_TotalSceneNodeCount;

	public:

		OcTreeNode(OcTree &manager, const OcTreeNode::Ptr &parent, 
			Vector4 min, Vector4 max);

		virtual ~OcTreeNode();

		virtual std::type_index GetTypeIndex() const;

		BoxBounds GetAABB() const;

		OcTree &GetManager() const;

		bool IsTwiceSize(BoxBounds other) const;

		bool IsLeaf() const;

		OcTreeNode::Ptr GetFitNode(BoxBounds other);

		std::shared_ptr<OcTreeNode> GetChildAt(unsigned int index) const;

		unsigned int GetSceneNodeCount() const;

		std::shared_ptr<SceneNode> GetSceneNodeAt(unsigned int index) const;

		unsigned int GetTotalSceneNodeCount() const;

		void Clear();

		void AddSceneNode(const std::shared_ptr<SceneNode> &node);

		void RemoveSceneNode(const std::shared_ptr<SceneNode> &node);

	protected:

		void IncreaseSceneNodeCount();

		void DecreaseSceneNodeCount();

	};
}

#endif // _FURY_OCTREENODE_H_