#ifndef _FURY_SCENE_MANAGER_H_
#define _FURY_SCENE_MANAGER_H_

#include <vector>
#include <memory>
#include <functional>

#include "Macros.h"

namespace fury
{
	class Collidable;

	class RenderQuery;

	class SceneNode;

	class FURY_API SceneManager
	{
	public:

		typedef std::shared_ptr<SceneManager> Ptr;

		typedef std::vector<std::shared_ptr<SceneNode>> SceneNodes;

		typedef std::function<void(const std::shared_ptr<SceneNode>&)> FilterFunc;

	public:

		virtual void AddSceneNode(const std::shared_ptr<SceneNode> &sceneNode) = 0;

		virtual void AddSceneNodeRecursively(const std::shared_ptr<SceneNode> &sceneNode) = 0;

		virtual void RemoveSceneNode(const std::shared_ptr<SceneNode> &sceneNode) = 0;

		virtual void UpdateSceneNode(const std::shared_ptr<SceneNode> &sceneNode) = 0;

		virtual void GetRenderQuery(const Collidable &collider, const std::shared_ptr<RenderQuery> &renderQuery, bool clear = true) const = 0;

		virtual void GetVisibleSceneNodes(const Collidable &collider, SceneNodes &visibleNodes, bool clear = true) const = 0;

		virtual void GetVisibleRenderables(const Collidable &collider, SceneNodes &renderables, bool clear = true) const = 0;

		virtual void GetVisibleShadowCasters(const Collidable &collider, SceneNodes &renderables, bool clear = true) const = 0;

		virtual void GetVisibleLights(const Collidable &collider, SceneNodes &lights, bool clear = true) const = 0;

		virtual void GetVisibleRenderableAndLights(const Collidable &collider, SceneNodes &renderables, SceneNodes &lights, bool clear = true) const = 0;

		virtual void WalkScene(const Collidable &collider, const FilterFunc &filterFunc) const = 0;

		virtual void Clear() = 0;
	};
}

#endif // _FURY_SCENE_MANAGER_H_