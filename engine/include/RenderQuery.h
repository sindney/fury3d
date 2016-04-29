#ifndef _FURY_RENDERQUERY_H_
#define _FURY_RENDERQUERY_H_

#include <memory>
#include <vector>

#include "Vector4.h"

namespace fury
{
	class SceneNode;

	class FURY_API RenderQuery
	{
	public:

		typedef std::shared_ptr<RenderQuery> Ptr;

		static Ptr Create();

		std::vector<std::shared_ptr<SceneNode>> OpaqueNodes;

		std::vector<std::shared_ptr<SceneNode>> TransparentNodes;

		std::vector<std::shared_ptr<SceneNode>> LightNodes;

		void Sort(Vector4 camPos);

		void Clear();
	};
}

#endif // _FURY_RENDERQUERY_H_