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

	void RenderQuery::Sort(Vector4 camPos)
	{
		auto farToNear = [&camPos](const SceneNode::Ptr &a, const SceneNode::Ptr &b) -> bool
		{
			return a->GetWorldPosition().Distance(camPos) > b->GetWorldPosition().Distance(camPos);
		};
		auto nearToFar = [&camPos](const SceneNode::Ptr &a, const SceneNode::Ptr &b) -> bool
		{
			return a->GetWorldPosition().Distance(camPos) < b->GetWorldPosition().Distance(camPos);
		};

		std::sort(OpaqueNodes.begin(), OpaqueNodes.end(), nearToFar);
		std::sort(TransparentNodes.begin(), TransparentNodes.end(), farToNear);
		std::sort(LightNodes.begin(), LightNodes.end(), farToNear);
	}

	void RenderQuery::Clear()
	{
		OpaqueNodes.clear();
		TransparentNodes.clear();
		LightNodes.clear();
	}
}
