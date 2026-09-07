#ifndef _FURY_RENDERQUERY_H_
#define _FURY_RENDERQUERY_H_

#include <memory>
#include <vector>

#include "Fury/Vector4.h"

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

		// ParticleRenderer nodes, drawn after transparentUnits.
		std::vector<std::shared_ptr<SceneNode>> particleNodes;

		// OceanComponent nodes, drawn by pass_ocean (before transparents).
		std::vector<std::shared_ptr<SceneNode>> oceanNodes;

		std::vector<std::shared_ptr<SceneNode>> renderableNodes;

		std::vector<std::shared_ptr<SceneNode>> lightNodes;

		// InstancedMeshRender nodes, drawn as instanced units (separate
		// from opaqueUnits so the per-mesh path is untouched when no
		// instancing components exist).
		std::vector<std::shared_ptr<SceneNode>> instancedNodes;

		void AddRenderable(const std::shared_ptr<SceneNode> &node);

		void AddParticle(const std::shared_ptr<SceneNode> &node);

		void AddOcean(const std::shared_ptr<SceneNode> &node);

		void AddLight(const std::shared_ptr<SceneNode> &node);

		void AddInstanced(const std::shared_ptr<SceneNode> &node);

		void Sort(Vector4 camPos);

		void Clear();
	};
}

#endif // _FURY_RENDERQUERY_H_
