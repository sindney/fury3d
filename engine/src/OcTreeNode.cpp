#include <math.h>

#include "OcTreeNode.h"
#include "OcTree.h"
#include "Plane.h"
#include "SceneNode.h"

namespace fury
{
	OcTreeNode::Ptr OcTreeNode::Create(OcTree &manager, const OcTreeNode::Ptr &parent, 
		Vector4 min, Vector4 max)
	{
		return std::make_shared<OcTreeNode>(manager, parent, min, max);
	}

	OcTreeNode::OcTreeNode(OcTree &manager, const OcTreeNode::Ptr &parent, Vector4 min, Vector4 max) :
		m_TypeIndex(typeid(OcTreeNode)), m_Manager(manager), m_Parent(parent), 
		m_AABB(min, max), m_IsLeaf(false), m_TotalSceneNodeCount(0)
	{

	}

	OcTreeNode::~OcTreeNode()
	{
		Clear();
	}

	std::type_index OcTreeNode::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	BoxBounds OcTreeNode::GetAABB() const
	{
		return m_AABB;
	}
	
	bool OcTreeNode::IsTwiceSize(BoxBounds other) const
	{
		Vector4 halfBoxSize = m_AABB.GetExtents();
		Vector4 boxSize = other.GetSize();
		return ((boxSize.x <= halfBoxSize.x) && (boxSize.y <= halfBoxSize.y) && (boxSize.z <= halfBoxSize.z));
	}

	bool OcTreeNode::IsLeaf() const
	{
		return m_IsLeaf;
	}

	OcTreeNode::Ptr OcTreeNode::GetFitNode(BoxBounds other)
	{
		Vector4 treeCenter = m_AABB.GetCenter();
		Vector4 treeMin = m_AABB.GetMin();
		Vector4 treeMax = m_AABB.GetMax();

		Vector4 otherMin = other.GetMin();
		Vector4 otherMax = other.GetMax();

		// test if boungbox is within this treeNode.

		if (otherMin.x <= treeMin.x || otherMin.y <= treeMin.y || otherMin.z <= treeMin.z ||
			otherMax.x >= treeMax.x || otherMax.y >= treeMax.y || otherMax.z >= treeMax.z)
			return shared_from_this();

		// test with split planes to find the correct child to fit in.

		Plane splitPlanes[] = {
			Plane(treeCenter, treeCenter + Vector4::YAxis, treeCenter + Vector4::XAxis),
			Plane(treeCenter, treeCenter + Vector4::XAxis, treeCenter + Vector4::ZAxis),
			Plane(treeCenter, treeCenter + Vector4::ZAxis, treeCenter + Vector4::YAxis)
		};

		bool collideResult[3];

		// index = first + second * 2 + third * 4
		int childIndex = 0;
		for (int i = 0; i < 3; i++)
		{
			Side side = splitPlanes[i].IsInside(other);

			if (side == Side::STRADDLE)
			{
				return shared_from_this();
			}
			else
			{
				collideResult[i] = side == Side::IN;
				childIndex += (collideResult[i] ? 1 : 0) * (int)pow(2, i);
			}
		}

		Vector4 treeExtents = m_AABB.GetExtents();

		OcTreeNode::Ptr child = m_Childs[childIndex];
		if (child == nullptr)
		{
			Vector4 aabbMax(
				(collideResult[2] ? 0 : 1) * treeExtents.x + treeCenter.x,
				(collideResult[1] ? 0 : 1) * treeExtents.y + treeCenter.y,
				(collideResult[0] ? 0 : 1) * treeExtents.z + treeCenter.z,
				1.0f
			);

			m_Childs[childIndex] = child = OcTreeNode::Create(
				m_Manager, shared_from_this(), aabbMax - treeExtents, aabbMax);
		}

		return child;
	}

	OcTree &OcTreeNode::GetManager() const
	{
		return m_Manager;
	}

	std::shared_ptr<OcTreeNode> OcTreeNode::GetChildAt(unsigned int index) const
	{
		if (index < 8)
			return m_Childs[index];
		else
			return nullptr;
	}

	unsigned int OcTreeNode::GetSceneNodeCount() const
	{
		return m_SceneNodes.size();
	}

	std::shared_ptr<SceneNode> OcTreeNode::GetSceneNodeAt(unsigned int index) const
	{
		if (index < m_SceneNodes.size())
			return m_SceneNodes[index];
		else
			return nullptr;
	}

	unsigned int OcTreeNode::GetTotalSceneNodeCount() const
	{
		return m_TotalSceneNodeCount;
	}

	void OcTreeNode::Clear()
	{
		for (auto &sceneNode : m_SceneNodes)
			sceneNode->SetOcTreeNode(nullptr);
		
		m_SceneNodes.clear();
		m_IsLeaf = true;

		for (int i = 0; i < 8; i++)
		{
			if (auto child = m_Childs[i])
				child->Clear();
			m_Childs[i] = nullptr;
		}	
	}

	void OcTreeNode::AddSceneNode(std::shared_ptr<SceneNode> node)
	{
		m_SceneNodes.push_back(node);
		node->SetOcTreeNode(shared_from_this());
		IncreaseSceneNodeCount();
	}

	void OcTreeNode::RemoveSceneNode(std::shared_ptr<SceneNode> node)
	{
		auto it = m_SceneNodes.begin();
		while (it != m_SceneNodes.end())
		{
			if (*it == node)
				break;
			++it;
		}

		if (it != m_SceneNodes.end())
		{
			m_SceneNodes.erase(it);
			node->SetOcTreeNode(nullptr);
			DecreaseSceneNodeCount();
		}
	}

	void OcTreeNode::IncreaseSceneNodeCount()
	{
		m_TotalSceneNodeCount++;
		if (m_Parent != nullptr)
			m_Parent->IncreaseSceneNodeCount();
	}

	void OcTreeNode::DecreaseSceneNodeCount()
	{
		m_TotalSceneNodeCount--;
		if (m_Parent != nullptr)
			m_Parent->DecreaseSceneNodeCount();
	}
}