#ifndef _FURY_OCTREE_H_
#define _FURY_OCTREE_H_

#include <vector>
#include <memory>
#include <typeindex>

#include "Fury/Color.h"
#include "SceneManager.h"
#include "Fury/Vector4.h"

namespace fury
{
	class BoxBounds;
	class OcTreeNode;
	class RenderUtil;

	// OcTree holds a shared_ptr to attached scenenodes.
	// When you need to destory a scenenode.
	// Call node.RemoveFromOcTree(true) + node.RemoveFromParent() + node.reset().
	// You'll destory this node and all it's childs.
	//
	// Auto-sizing: Create() (and Reset()) start the tree with a default
	// +/-1000 cube root. When a scene node's worldAABB exceeds the current
	// root, the tree wraps the existing root inside a new 2x root that
	// contains the offender. Callers that know their scene bounds up front
	// can skip the wraps by passing explicit min/max to Create.
	class FURY_API OcTree : public SceneManager, public std::enable_shared_from_this<OcTree>
	{
	public:

		typedef std::shared_ptr<OcTree> Ptr;

		static const unsigned int kDefaultMaxDepth = 8;

		static const float kDefaultHalfExtent;  // 1000.0f, defined in .cpp

		static Ptr Create();

		static Ptr Create(unsigned int maxDepth);

		static Ptr Create(Vector4 min, Vector4 max, unsigned int maxDepth = kDefaultMaxDepth);

	protected:

		std::type_index m_TypeIndex;

		std::shared_ptr<OcTreeNode> m_Root;

		unsigned int m_MaxDepth;

	public:

		OcTree();

		OcTree(unsigned int maxDepth);

		OcTree(Vector4 min, Vector4 max, unsigned int maxDepth);

		~OcTree();

		virtual std::type_index GetTypeIndex() const;

		virtual void AddSceneNode(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void AddSceneNodeRecursively(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void RemoveSceneNode(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void UpdateSceneNode(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void GetRenderQuery(const Collidable &collider, const std::shared_ptr<RenderQuery> &renderQuery, bool clear = true) const;

		virtual void GetVisibleSceneNodes(const Collidable &collider, SceneNodes &sceneNodes, bool clear = true) const;

		virtual void GetVisibleRenderables(const Collidable &collider, SceneNodes &renderables, bool clear = true) const;

		virtual void GetVisibleShadowCasters(const Collidable &collider, SceneNodes &renderables, bool clear = true) const;

		virtual void GetVisibleLights(const Collidable &collider, SceneNodes &lights, bool clear = true) const;

		virtual void GetVisibleRenderableAndLights(const Collidable &collider, SceneNodes &renderables, SceneNodes &lights, bool clear = true) const;

		virtual void WalkScene(const Collidable &collider, const FilterFunc &filterFunc) const;

		virtual void Reset();

		virtual void Reset(unsigned int maxDepth);

		virtual void Reset(Vector4 min, Vector4 max, unsigned int maxDepth);

		virtual void Clear();

		// Returns true once the root exists. With the default-cube Create()
		// this is always true; only callers that explicitly Reset() and then
		// query before any insert observe `false`.
		bool IsRooted() const;

		// Snapshot of the current root AABB. Call only when IsRooted().
		BoxBounds GetRootAABB() const;

		// Total tracked scene nodes across the whole tree (root's total).
		// Returns 0 when unrooted.
		unsigned int GetTotalSceneNodeCount() const;

		// Walks the tree and counts every allocated OcTreeNode. Returns 0 when
		// unrooted. Used by the editor's Profiler readout.
		unsigned int GetOccupiedNodeCount() const;

		// Maximum depth at which a tree-node holds a non-empty m_SceneNodes
		// list (root is depth 0). Returns 0 when unrooted.
		unsigned int GetMaxOccupiedDepth() const;

		// Emits one DrawBoxBounds per occupied tree-node. Caller MUST have
		// bracketed the call with renderUtil.BeginDrawLines / EndDrawLines.
		void DrawDebugBounds(RenderUtil &renderUtil) const;

	protected:

		void AddSceneNode(const std::shared_ptr<SceneNode> &sceneNode, const std::shared_ptr<OcTreeNode> &treeNode, unsigned int depth);

		void GrowRootToContain(const BoxBounds &nodeBounds);

	};
}

#endif // _FURY_OCTREE_H_