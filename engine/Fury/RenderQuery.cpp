#include <algorithm>
#include <functional>

#include "Fury/RenderQuery.h"
#include "Fury/SceneNode.h"
#include "Fury/MeshRender.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"

#include "Fury/Log.h"
#include "Fury/Light.h"

namespace fury
{
	RenderQuery::Ptr RenderQuery::Create()
	{
		return std::make_shared<RenderQuery>();
	}

	void RenderQuery::AddRenderable(const std::shared_ptr<SceneNode> &node)
	{
		auto render = node->GetComponent<MeshRender>();
		auto mesh = render->GetMesh();
		auto subMeshCount = mesh->GetSubMeshCount();
		if (subMeshCount > 0)
		{
			for (unsigned int i = 0; i < subMeshCount; i++)
			{
				auto subMesh = mesh->GetSubMeshAt(i);
				auto material = render->GetMaterial(i);
				if (material->GetOpaque())
					opaqueUnits.push_back(RenderUnit(node, mesh, material, i));
				else
					transparentUnits.push_back(RenderUnit(node, mesh, material, i));
			}
		}
		else
		{
			auto material = render->GetMaterial();
			if (material->GetOpaque())
				opaqueUnits.push_back(RenderUnit(node, mesh, material, -1));
			else
				transparentUnits.push_back(RenderUnit(node, mesh, material, -1));
		}

		renderableNodes.push_back(node);
	}

	void RenderQuery::AddLight(const std::shared_ptr<SceneNode> &node)
	{
		lightNodes.push_back(node);
	}

	void RenderQuery::Sort(Vector4 camPos)
	{
		std::sort(opaqueUnits.begin(), opaqueUnits.end(), [&camPos](const RenderUnit &a, const RenderUnit &b) -> bool
		{
			return a.node->GetWorldPosition().Distance(camPos) < b.node->GetWorldPosition().Distance(camPos);
		});

		std::sort(transparentUnits.begin(), transparentUnits.end(), [&camPos](const RenderUnit &a, const RenderUnit &b) -> bool
		{
			return a.node->GetWorldPosition().Distance(camPos) > b.node->GetWorldPosition().Distance(camPos);
		});

		/*std::sort(lightNodes.begin(), lightNodes.end(), [](const SceneNode::Ptr &a, const SceneNode::Ptr &b) -> bool
		{
			return b->GetComponent<Light>()->GetCastShadows();
		});*/
	}

	void RenderQuery::Clear()
	{
		opaqueUnits.clear();
		transparentUnits.clear();
		renderableNodes.clear();
		lightNodes.clear();
	}
}
