#include <deque>

#include "Frustum.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRender.h"
#include "OcTreeNode.h"
#include "OcTree.h"
#include "RenderQuery.h"
#include "SceneNode.h"
#include "SphereBounds.h"
#include "Log.h"

namespace fury
{
	OcTree::Ptr OcTree::Create(Vector4 min, Vector4 max, unsigned int maxDepth)
	{
		return std::make_shared<OcTree>(min, max, maxDepth);
	}

	OcTree::OcTree(Vector4 min, Vector4 max, unsigned int maxDepth) :
		m_TypeIndex(typeid(OcTree)), m_MaxDepth(maxDepth)
	{
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
	}

	OcTree::~OcTree()
	{
		m_Root.reset();
		FURYD << "OcTree::~OcTree";
	}

	std::type_index OcTree::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	void OcTree::AddSceneNode(const SceneNode::Ptr &sceneNode)
	{
		AddSceneNode(sceneNode, m_Root, 0);
	}

	void OcTree::AddSceneNodeRecursively(const std::shared_ptr<SceneNode> &sceneNode)
	{
		AddSceneNode(sceneNode);

		for (unsigned int i = 0; i < sceneNode->GetChildCount(); i++)
			AddSceneNodeRecursively(sceneNode->GetChildAt(i));
	}

	void OcTree::RemoveSceneNode(const SceneNode::Ptr &sceneNode)
	{
		sceneNode->RemoveFromOcTree(false);
	}

	void OcTree::UpdateSceneNode(const SceneNode::Ptr &sceneNode)
	{
		sceneNode->RemoveFromOcTree(false);
		AddSceneNode(sceneNode);
	}

	void OcTree::GetRenderQuery(const Collidable &collider, const std::shared_ptr<RenderQuery> &renderQuery, bool clear) const
	{
		if (clear)
			renderQuery->Clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			if (sceneNode->GetComponent<Light>() != nullptr)
				renderQuery->AddLight(sceneNode);
			
			if (auto render = sceneNode->GetComponent<MeshRender>())
			{
				if (render->GetRenderable())
					renderQuery->AddRenderable(sceneNode);
			}
		});
	}

	void OcTree::GetVisibleSceneNodes(const Collidable &collider, SceneNodes &sceneNodes, bool clear) const
	{
		if (clear)
			sceneNodes.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode) 
		{
			sceneNodes.push_back(sceneNode);
		});
	}

	void OcTree::GetVisibleRenderables(const Collidable &collider, SceneNodes &renderables, bool clear) const
	{
		if (clear)
			renderables.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			auto render = sceneNode->GetComponent<MeshRender>();
			if (render != nullptr && render->GetRenderable())
				renderables.push_back(sceneNode);
		});
	}

	void OcTree::GetVisibleShadowCasters(const Collidable &collider, SceneNodes &renderables, bool clear) const
	{
		if (clear)
			renderables.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			auto render = sceneNode->GetComponent<MeshRender>();
			if (render != nullptr && render->GetRenderable() && render->GetMesh()->GetCastShadows())
				renderables.push_back(sceneNode);
		});
	}

	void OcTree::GetVisibleLights(const Collidable &collider, SceneNodes &lights, bool clear) const
	{
		if (clear)
			lights.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			if (sceneNode->GetComponent<Light>() != nullptr)
				lights.push_back(sceneNode);

		});
	}

	void OcTree::GetVisibleRenderableAndLights(const Collidable &collider, SceneNodes &renderables, SceneNodes &lights, bool clear) const
	{
		if (clear)
		{
			renderables.clear();
			lights.clear();
		}

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			auto render = sceneNode->GetComponent<MeshRender>();
			if (render != nullptr && render->GetRenderable())
				renderables.push_back(sceneNode);
			else if (sceneNode->GetComponent<Light>() != nullptr)
				lights.push_back(sceneNode);

		});
	}

	void OcTree::WalkScene(const Collidable &collider, const FilterFunc &filterFunc) const
	{
		using TreeNodePair = std::pair<bool, OcTreeNode::Ptr>;

		std::deque<TreeNodePair> possiblePairs;
		possiblePairs.push_back(std::make_pair(false, m_Root));

		while (!possiblePairs.empty())
		{
			// pop next possible node.
			TreeNodePair currentPair = possiblePairs.back();
			possiblePairs.pop_back();

			// procced
			bool tested = currentPair.first;
			OcTreeNode::Ptr treeNode = currentPair.second;

			if (treeNode->GetTotalSceneNodeCount() > 0)
			{
				Side result = tested ? Side::IN : collider.IsInside(treeNode->GetAABB());

				if (result != Side::OUT)
				{
					if (result == Side::IN)
						tested = true;

					// test currentTreeNode's belonging sceneNodes
					int sceneNodeCount = treeNode->GetSceneNodeCount();
					for (int i = 0; i < sceneNodeCount; i++)
					{
						SceneNode::Ptr sceneNode = treeNode->GetSceneNodeAt(i);
						if (tested || collider.IsInsideFast(sceneNode->GetWorldAABB()))
							filterFunc(sceneNode);
					}

					// add currentTreeNode's childs to possiblePairs vector.
					for (int i = 0; i < 8; i++)
					{
						OcTreeNode::Ptr childNode = treeNode->GetChildAt(i);
						if (childNode != nullptr && childNode->GetTotalSceneNodeCount() > 0)
							possiblePairs.push_back(std::make_pair(tested, childNode));
					}
				}
			}
		}
	}

	void OcTree::Reset(Vector4 min, Vector4 max, unsigned int maxDepth)
	{
		m_Root.reset();
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
	}

	void OcTree::Clear()
	{
		m_Root->Clear();
	}

	void OcTree::AddSceneNode(const SceneNode::Ptr &sceneNode, const OcTreeNode::Ptr &treeNode, unsigned int depth)
	{
		BoxBounds treeBounds = treeNode->GetAABB();
		BoxBounds nodeBounds = sceneNode->GetWorldAABB();

		if ((depth < m_MaxDepth) && treeNode->IsTwiceSize(nodeBounds))
		{
			OcTreeNode::Ptr fitNode = treeNode->GetFitNode(nodeBounds);
			AddSceneNode(sceneNode, fitNode, ++depth);
		}
		else
		{
			treeNode->AddSceneNode(sceneNode);
		}
	}

}