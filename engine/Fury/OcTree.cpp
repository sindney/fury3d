#include <deque>
#include <utility>

#include "Fury/Frustum.h"
#include "Fury/InstancedMeshRender.h"
#include "Fury/Light.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/OceanComponent.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/OcTreeNode.h"
#include "Fury/OcTree.h"
#include "Fury/RenderQuery.h"
#include "Fury/RenderUtil.h"
#include "Fury/SceneNode.h"
#include "Fury/SphereBounds.h"
#include "Fury/Log.h"

namespace fury
{
	const float OcTree::kDefaultHalfExtent = 1000.0f;

	OcTree::Ptr OcTree::Create()
	{
		return std::make_shared<OcTree>();
	}

	OcTree::Ptr OcTree::Create(unsigned int maxDepth)
	{
		return std::make_shared<OcTree>(maxDepth);
	}

	OcTree::Ptr OcTree::Create(Vector4 min, Vector4 max, unsigned int maxDepth)
	{
		return std::make_shared<OcTree>(min, max, maxDepth);
	}

	OcTree::OcTree() :
		m_TypeIndex(typeid(OcTree)), m_MaxDepth(kDefaultMaxDepth)
	{
		Vector4 min(-kDefaultHalfExtent, -kDefaultHalfExtent, -kDefaultHalfExtent, 1.0f);
		Vector4 max( kDefaultHalfExtent,  kDefaultHalfExtent,  kDefaultHalfExtent, 1.0f);
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
	}

	OcTree::OcTree(unsigned int maxDepth) :
		m_TypeIndex(typeid(OcTree)), m_MaxDepth(maxDepth)
	{
		Vector4 min(-kDefaultHalfExtent, -kDefaultHalfExtent, -kDefaultHalfExtent, 1.0f);
		Vector4 max( kDefaultHalfExtent,  kDefaultHalfExtent,  kDefaultHalfExtent, 1.0f);
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
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
		BoxBounds nodeBounds = sceneNode->GetWorldAABB();

		if (m_Root != nullptr && m_Root->GetAABB().IsInside(nodeBounds) != Side::IN)
			GrowRootToContain(nodeBounds);

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

			if (auto particle = sceneNode->GetComponent<ParticleRenderer>())
			{
				renderQuery->AddParticle(sceneNode);
			}

			if (sceneNode->GetComponent<OceanComponent>() != nullptr)
			{
				renderQuery->AddOcean(sceneNode);
			}

			if (auto instanced = sceneNode->GetComponent<InstancedMeshRender>())
			{
				if (instanced->GetRenderable())
					renderQuery->AddInstanced(sceneNode);
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

			// The spot shadow pass queries renderables (not casters);
			// instanced units must appear here too. Their cast_shadows
			// flag is honored in the pass itself.
			auto instanced = sceneNode->GetComponent<InstancedMeshRender>();
			if (instanced != nullptr && instanced->GetRenderable())
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
			// Per-instance shadow casting. MeshRender::GetCastShadows is
			// the source of truth now -- the mesh's own flag is the
			// asset-level default that's seeded into MeshRender on load
			// but doesn't override per-instance toggles.
			if (render != nullptr && render->GetRenderable() && render->GetCastShadows())
				renderables.push_back(sceneNode);

			// Instanced casters (ISM/HISM) honor their own flag.
			auto instanced = sceneNode->GetComponent<InstancedMeshRender>();
			if (instanced != nullptr && instanced->GetRenderable() && instanced->GetCastShadows())
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
		if (m_Root == nullptr)
			return;

		using TreeNodePair = std::pair<bool, OcTreeNode::Ptr>;

		// Root-resident nodes are always tested per-node: infinite-bounds
		// nodes (directional lights, oceans, particle systems) never
		// subdivide out of the root, and must still be collected when the
		// camera roams beyond the tree extent (the root cell itself tests
		// OUT while the infinite node is visible).
		int rootNodeCount = m_Root->GetSceneNodeCount();
		for (int i = 0; i < rootNodeCount; i++)
		{
			SceneNode::Ptr sceneNode = m_Root->GetSceneNodeAt(i);
			if (collider.IsInsideFast(sceneNode->GetWorldAABB()))
				filterFunc(sceneNode);
		}

		// Subtree: normal hierarchical pruning. Finite root-resident nodes
		// are contained by the root AABB, so an OUT root skips them safely.
		Side rootSide = collider.IsInside(m_Root->GetAABB());
		if (rootSide == Side::OUT)
			return;

		std::deque<TreeNodePair> possiblePairs;
		for (int i = 0; i < 8; i++)
		{
			OcTreeNode::Ptr childNode = m_Root->GetChildAt(i);
			if (childNode != nullptr && childNode->GetTotalSceneNodeCount() > 0)
				possiblePairs.push_back(std::make_pair(rootSide == Side::IN, childNode));
		}

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

	void OcTree::Reset()
	{
		Reset(kDefaultMaxDepth);
	}

	void OcTree::Reset(unsigned int maxDepth)
	{
		m_Root.reset();
		m_MaxDepth = maxDepth;
		Vector4 min(-kDefaultHalfExtent, -kDefaultHalfExtent, -kDefaultHalfExtent, 1.0f);
		Vector4 max( kDefaultHalfExtent,  kDefaultHalfExtent,  kDefaultHalfExtent, 1.0f);
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
	}

	void OcTree::Reset(Vector4 min, Vector4 max, unsigned int maxDepth)
	{
		m_Root.reset();
		m_MaxDepth = maxDepth;
		m_Root = OcTreeNode::Create(*this, nullptr, min, max);
	}

	void OcTree::Clear()
	{
		if (m_Root != nullptr)
			m_Root->Clear();
	}

	bool OcTree::IsRooted() const
	{
		return m_Root != nullptr;
	}

	BoxBounds OcTree::GetRootAABB() const
	{
		if (m_Root != nullptr)
			return m_Root->GetAABB();
		return BoxBounds();
	}

	unsigned int OcTree::GetTotalSceneNodeCount() const
	{
		return m_Root != nullptr ? m_Root->GetTotalSceneNodeCount() : 0u;
	}

	unsigned int OcTree::GetOccupiedNodeCount() const
	{
		if (m_Root == nullptr)
			return 0;

		unsigned int count = 0;
		std::deque<OcTreeNode*> stack;
		stack.push_back(m_Root.get());

		while (!stack.empty())
		{
			OcTreeNode *node = stack.back();
			stack.pop_back();
			++count;

			for (int i = 0; i < 8; ++i)
			{
				if (node->m_Childs[i] != nullptr)
					stack.push_back(node->m_Childs[i].get());
			}
		}

		return count;
	}

	unsigned int OcTree::GetMaxOccupiedDepth() const
	{
		if (m_Root == nullptr)
			return 0;

		unsigned int maxDepth = 0;
		std::deque<std::pair<OcTreeNode*, unsigned int>> stack;
		stack.push_back(std::make_pair(m_Root.get(), 0u));

		while (!stack.empty())
		{
			auto pair = stack.back();
			stack.pop_back();

			OcTreeNode *node = pair.first;
			unsigned int depth = pair.second;

			if (node->GetSceneNodeCount() > 0 && depth > maxDepth)
				maxDepth = depth;

			for (int i = 0; i < 8; ++i)
			{
				if (node->m_Childs[i] != nullptr)
					stack.push_back(std::make_pair(node->m_Childs[i].get(), depth + 1));
			}
		}

		return maxDepth;
	}

	void OcTree::DrawDebugBounds(RenderUtil &renderUtil) const
	{
		if (m_Root == nullptr)
			return;

		// Caller must have bracketed this call with BeginDrawLines / EndDrawLines
		// (see Pipeline::DrawDebug for the canonical wiring).
		static const Color kPalette[6] = {
			Color::White,
			Color::Yellow,
			Color::Green,
			Color::Cyan,
			Color::Blue,
			Color::Magenta,
		};

		std::deque<std::pair<OcTreeNode*, unsigned int>> stack;
		stack.push_back(std::make_pair(m_Root.get(), 0u));

		while (!stack.empty())
		{
			auto pair = stack.back();
			stack.pop_back();

			OcTreeNode *node = pair.first;
			unsigned int depth = pair.second;

			if (node->GetTotalSceneNodeCount() > 0)
				renderUtil.DrawBoxBounds(node->GetAABB(), kPalette[depth % 6]);

			for (int i = 0; i < 8; ++i)
			{
				if (node->m_Childs[i] != nullptr)
					stack.push_back(std::make_pair(node->m_Childs[i].get(), depth + 1));
			}
		}
	}

	void OcTree::AddSceneNode(const SceneNode::Ptr &sceneNode, const OcTreeNode::Ptr &treeNode, unsigned int depth)
	{
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

	void OcTree::GrowRootToContain(const BoxBounds &nodeBounds)
	{
		const int kMaxWraps = 32;

		for (int i = 0; i < kMaxWraps; ++i)
		{
			Side side = m_Root->GetAABB().IsInside(nodeBounds);
			if (side == Side::IN)
				return;

			Vector4 rootCenter = m_Root->GetAABB().GetCenter();
			Vector4 rootMin = m_Root->GetAABB().GetMin();
			Vector4 rootMax = m_Root->GetAABB().GetMax();
			Vector4 rootSize = m_Root->GetAABB().GetSize();
			Vector4 nodeCenter = nodeBounds.GetCenter();

			// Decide which direction to grow on each axis: if the node's center
			// is on the +side of the root center, grow toward +; otherwise -.
			bool growPlusX = nodeCenter.x >= rootCenter.x;
			bool growPlusY = nodeCenter.y >= rootCenter.y;
			bool growPlusZ = nodeCenter.z >= rootCenter.z;

			Vector4 newMin = rootMin;
			Vector4 newMax = rootMax;

			if (growPlusX) newMax.x = rootMax.x + rootSize.x; else newMin.x = rootMin.x - rootSize.x;
			if (growPlusY) newMax.y = rootMax.y + rootSize.y; else newMin.y = rootMin.y - rootSize.y;
			if (growPlusZ) newMax.z = rootMax.z + rootSize.z; else newMin.z = rootMin.z - rootSize.z;

			newMin.w = 1.0f;
			newMax.w = 1.0f;

			// The old root becomes one of the eight children of the new root,
			// in the corner *opposite* the growth direction (i.e., where the
			// old root's AABB still sits inside the new root).
			//
			// Child index layout per OcTreeNode.h:
			//   index = first + second * 2 + third * 4
			// where first  == X >= splitCenter (set when growPlusX is FALSE,
			//                  i.e. the old root is on the +X half of the new
			//                  root; same logic for Y and Z).
			int childIndex = 0;
			if (!growPlusX) childIndex += 1;
			if (!growPlusY) childIndex += 2;
			if (!growPlusZ) childIndex += 4;

			OcTreeNode::Ptr newRoot = OcTreeNode::Create(*this, nullptr, newMin, newMax);
			OcTreeNode::Ptr oldRoot = m_Root;

			newRoot->m_Childs[childIndex] = oldRoot;
			oldRoot->m_Parent = newRoot;
			newRoot->m_TotalSceneNodeCount = oldRoot->GetTotalSceneNodeCount();

			m_Root = newRoot;
		}

		FURYE << "OcTree::GrowRootToContain hit " << kMaxWraps
			<< "-wrap cap; nodeCenter=("
			<< nodeBounds.GetCenter().x << ", "
			<< nodeBounds.GetCenter().y << ", "
			<< nodeBounds.GetCenter().z << ")";
	}

}
