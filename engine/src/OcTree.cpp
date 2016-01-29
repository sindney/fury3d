#include <deque>

#include "Frustum.h"
#include "Light.h"
#include "Material.h"
#include "MeshRender.h"
#include "OcTreeNode.h"
#include "OcTree.h"
#include "RenderQuery.h"
//#include "RenderUtil.h"
#include "SceneNode.h"
#include "SphereBounds.h"

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
			AddSceneNode(sceneNode->GetChildAt(i));
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

	void OcTree::GetRenderQuery(const Collidable &collider, const std::shared_ptr<RenderQuery> &renderQuery, bool visualize) const
	{
		renderQuery->Clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			if (sceneNode->GetComponent<Light>() != nullptr)
			{
				renderQuery->LightNodes.push_back(sceneNode);
				return;
			}
			
			if (auto render = sceneNode->GetComponent<MeshRender>())
			{
				if (render->GetRenderable())
				{
					auto material = render->GetMaterial();

					if (material->GetOpaque())
						renderQuery->OpaqueNodes.push_back(sceneNode);
					else 
						renderQuery->TransparentNodes.push_back(sceneNode);
				}
			}
		}, visualize);
	}

	void OcTree::GetVisibleSceneNodes(const Collidable &collider, SceneNodes &sceneNodes, bool visualize) const
	{
		sceneNodes.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode) 
		{
			sceneNodes.push_back(sceneNode);

		}, visualize);
	}

	void OcTree::GetVisibleRenderables(const Collidable &collider, SceneNodes &renderables, bool visualize) const
	{
		renderables.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			auto render = sceneNode->GetComponent<MeshRender>();
			if (render != nullptr && render->GetRenderable())
				renderables.push_back(sceneNode);

		}, visualize);
	}

	void OcTree::GetVisibleLights(const Collidable &collider, SceneNodes &lights, bool visualize) const
	{
		lights.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			if (sceneNode->GetComponent<Light>() != nullptr)
				lights.push_back(sceneNode);

		}, visualize);
	}

	void OcTree::GetVisibleRenderableAndLights(const Collidable &collider, SceneNodes &renderables, SceneNodes &lights, bool visualize) const
	{
		renderables.clear();
		lights.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			auto render = sceneNode->GetComponent<MeshRender>();
			if (render != nullptr && render->GetRenderable())
				renderables.push_back(sceneNode);
			else if (sceneNode->GetComponent<Light>() != nullptr)
				lights.push_back(sceneNode);

		}, visualize);
	}

	void OcTree::WalkScene(const Collidable &collider, const FilterFunc &filterFunc, bool visualize) const
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

					/*if (visualize)
						RenderUtil::Instance()->DrawBoxBounds(treeNode->GetAABB(), tested ? Color::Red : Color::Blue);*/

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

	/*void OcTree::DrawOcTree(Color color) const
	{
		DrawOcTreeNode(m_Root, color);
	}

	void OcTree::DrawOcTreeNode(const OcTreeNode::Ptr &treeNode, Color color) const
	{
		RenderUtil::Instance()->DrawBoxBounds(treeNode->GetAABB(), color);

		for (int i = 0; i < 8; i++)
		{
			OcTreeNode::Ptr childNode = treeNode->GetChildAt(i);
			if (childNode != nullptr)
				DrawOcTreeNode(childNode, color);
		}
	}*/

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