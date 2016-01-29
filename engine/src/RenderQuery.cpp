#include <algorithm>
#include <functional>

#include "RenderQuery.h"
#include "SceneNode.h"

namespace fury
{
	RenderQuery::Ptr RenderQuery::Create()
	{
		return std::make_shared<RenderQuery>();
	}

	void RenderQuery::Sort(Vector4 camPos, bool farToNear)
	{
		std::function<bool(const SceneNode::Ptr &, const SceneNode::Ptr &)> compare = nullptr;

		if (farToNear)
		{
			compare = [&camPos](const SceneNode::Ptr &a, const SceneNode::Ptr &b) -> bool
			{
				return a->GetWorldPosition().Distance(camPos) > b->GetWorldPosition().Distance(camPos);
			};
		}
		else
		{
			compare = [&camPos](const SceneNode::Ptr &a, const SceneNode::Ptr &b) -> bool
			{
				return a->GetWorldPosition().Distance(camPos) < b->GetWorldPosition().Distance(camPos);
			};
		}

		std::sort(OpaqueNodes.begin(), OpaqueNodes.end(), compare);
		std::sort(TransparentNodes.begin(), TransparentNodes.end(), compare);
		std::sort(LightNodes.begin(), LightNodes.end(), compare);
	}

	void RenderQuery::Clear()
	{
		OpaqueNodes.clear();
		TransparentNodes.clear();
		LightNodes.clear();
	}
}