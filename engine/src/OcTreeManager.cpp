#include <deque>

#include "Frustum.h"
#include "Light.h"
#include "Material.h"
#include "MeshRender.h"
#include "OcTreeNode.h"
#include "OcTreeManager.h"
#include "RenderQuery.h"
//#include "RenderUtil.h"
#include "SceneNode.h"
#include "SphereBounds.h"
#include "Log.h"

namespace fury
{
	OcTreeManager::Ptr OcTreeManager::Create(Vector4 min, Vector4 max, unsigned int maxDepth)
	{
		return std::make_shared<OcTreeManager>(min, max, maxDepth);
	}

	OcTreeManager::OcTreeManager(Vector4 min, Vector4 max, unsigned int maxDepth) :
		m_TypeIndex(typeid(OcTreeManager)), m_MaxDepth(maxDepth)
	{
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
	}

	OcTreeManager::~OcTreeManager()
	{
		m_Root.reset();
		FURYD << "OcTreeManager::~OcTreeManager";
	}

	std::type_index OcTreeManager::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	void OcTreeManager::AddSceneNode(const SceneNode::Ptr &sceneNode)
	{
		AddSceneNode(sceneNode, m_Root, 0);
	}

	void OcTreeManager::AddSceneNodeRecursively(const std::shared_ptr<SceneNode> &sceneNode)
	{
		AddSceneNode(sceneNode);

		for (unsigned int i = 0; i < sceneNode->GetChildCount(); i++)
			AddSceneNode(sceneNode->GetChildAt(i));
	}

	void OcTreeManager::RemoveSceneNode(const SceneNode::Ptr &sceneNode)
	{
		sceneNode->RemoveFromOcTree(false);
	}

	void OcTreeManager::UpdateSceneNode(const SceneNode::Ptr &sceneNode)
	{
		sceneNode->RemoveFromOcTree(false);
		AddSceneNode(sceneNode);
	}

	void OcTreeManager::GetRenderQuery(const Collidable &collider, const std::shared_ptr<RenderQuery> &renderQuery, bool visualize) const
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

	void OcTreeManager::GetVisibleSceneNodes(const Collidable &collider, SceneNodes &sceneNodes, bool visualize) const
	{
		sceneNodes.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode) 
		{
			sceneNodes.push_back(sceneNode);

		}, visualize);
	}

	void OcTreeManager::GetVisibleRenderables(const Collidable &collider, SceneNodes &renderables, bool visualize) const
	{
		renderables.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			auto render = sceneNode->GetComponent<MeshRender>();
			if (render != nullptr && render->GetRenderable())
				renderables.push_back(sceneNode);

		}, visualize);
	}

	void OcTreeManager::GetVisibleLights(const Collidable &collider, SceneNodes &lights, bool visualize) const
	{
		lights.clear();

		WalkScene(collider, [&](const SceneNode::Ptr &sceneNode)
		{
			if (sceneNode->GetComponent<Light>() != nullptr)
				lights.push_back(sceneNode);

		}, visualize);
	}

	void OcTreeManager::GetVisibleRenderableAndLights(const Collidable &collider, SceneNodes &renderables, SceneNodes &lights, bool visualize) const
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

	void OcTreeManager::WalkScene(const Collidable &collider, const FilterFunc &filterFunc, bool visualize) const
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

	void OcTreeManager::Reset(Vector4 min, Vector4 max, unsigned int maxDepth)
	{
		m_Root.reset();
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
	}

	void OcTreeManager::Clear()
	{
		m_Root->Clear();
	}

	/*void OcTreeManager::DrawOcTree(Color color) const
	{
		DrawOcTreeNode(m_Root, color);
	}

	void OcTreeManager::DrawOcTreeNode(const OcTreeNode::Ptr &treeNode, Color color) const
	{
		RenderUtil::Instance()->DrawBoxBounds(treeNode->GetAABB(), color);

		for (int i = 0; i < 8; i++)
		{
			OcTreeNode::Ptr childNode = treeNode->GetChildAt(i);
			if (childNode != nullptr)
				DrawOcTreeNode(childNode, color);
		}
	}*/

	void OcTreeManager::AddSceneNode(const SceneNode::Ptr &sceneNode, const OcTreeNode::Ptr &treeNode, unsigned int depth)
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