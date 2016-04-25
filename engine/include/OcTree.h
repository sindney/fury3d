#ifndef _FURY_OCTREE_H_
#define _FURY_OCTREE_H_

#include <vector>
#include <memory>
#include <typeindex>

#include "Color.h"
#include "SceneManager.h"
#include "Vector4.h"

namespace fury
{
	class OcTreeNode;

	// OcTree holds a shared_ptr to attached scenenodes.
	// When you need to destory a scenenode.
	// Call node.RemoveFromOcTree(true) + node.RemoveFromParent() + node.reset().
	// You'll destory this node and all it's childs.
	class FURY_API OcTree : public SceneManager, public std::enable_shared_from_this<OcTree>
	{
	public:

		typedef std::shared_ptr<OcTree> Ptr;

		static Ptr Create(Vector4 min, Vector4 max, unsigned int maxDepth = 6);

	protected:

		std::type_index m_TypeIndex;

		std::shared_ptr<OcTreeNode> m_Root;

		unsigned int m_MaxDepth;

	public:

		OcTree(Vector4 min, Vector4 max, unsigned int maxDepth);

		~OcTree();

		virtual std::type_index GetTypeIndex() const;

		virtual void AddSceneNode(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void AddSceneNodeRecursively(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void RemoveSceneNode(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void UpdateSceneNode(const std::shared_ptr<SceneNode> &sceneNode);

		virtual void GetRenderQuery(const Collidable &collider, const std::shared_ptr<RenderQuery> &renderQuery, bool visualize = false) const;

		virtual void GetVisibleSceneNodes(const Collidable &collider, SceneNodes &sceneNodes, bool visualize = false) const;

		virtual void GetVisibleRenderables(const Collidable &collider, SceneNodes &renderables, bool visualize = false) const;

		virtual void GetVisibleLights(const Collidable &collider, SceneNodes &lights, bool visualize = false) const;

		virtual void GetVisibleRenderableAndLights(const Collidable &collider, SceneNodes &renderables, SceneNodes &lights, bool visualize = false) const;

		virtual void WalkScene(const Collidable &collider, const FilterFunc &filterFunc, bool visualize = false) const;

		virtual void Reset(Vector4 min, Vector4 max, unsigned int maxDepth);

		virtual void Clear();

		//void DrawOcTree(Color color = Color::White) const;

		/*void SetVisualize(bool value);

		bool GetVisualize();*/

	protected:

		//void DrawOcTreeNode(const std::shared_ptr<OcTreeNode> &treeNode, Color color) const;

		void AddSceneNode(const std::shared_ptr<SceneNode> &sceneNode, const std::shared_ptr<OcTreeNode> &treeNode, unsigned int depth);

	};
}

#endif // _FURY_OCTREE_H_