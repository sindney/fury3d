#ifndef _FURY_RENDERQUERY_H_
#define _FURY_RENDERQUERY_H_

#include <memory>
#include <vector>

#include "Vector4.h"

namespace fury
{
	class SceneNode;

	class Material;

	class Mesh;

	struct FURY_API RenderUnit
	{
		std::shared_ptr<SceneNode> node;

		std::shared_ptr<Mesh> mesh;

		std::shared_ptr<Material> material;

		int subMesh = 0;

		RenderUnit(const std::shared_ptr<SceneNode> &node, const std::shared_ptr<Mesh> &mesh,
			const std::shared_ptr<Material> &material, int subMesh)
		{
			this->node = node;
			this->mesh = mesh;
			this->material = material;
			this->subMesh = subMesh;
		}
	};

	class FURY_API RenderQuery
	{
	public:

		typedef std::shared_ptr<RenderQuery> Ptr;

		static Ptr Create();

		std::vector<RenderUnit> opaqueUnits;

		std::vector<RenderUnit> transparentUnits;

		std::vector<std::shared_ptr<SceneNode>> renderableNodes;

		std::vector<std::shared_ptr<SceneNode>> lightNodes;

		void AddRenderable(const std::shared_ptr<SceneNode> &node);

		void AddLight(const std::shared_ptr<SceneNode> &node);

		void Sort(Vector4 camPos);

		void Clear();
	};
}

#endif // _FURY_RENDERQUERY_H_
