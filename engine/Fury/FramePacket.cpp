#include "Fury/FramePacket.h"

#include "Fury/Camera.h"
#include "Fury/Mesh.h"
#include "Fury/SceneNode.h"

namespace fury
{
	std::shared_ptr<Mesh> PickShadowLodMesh(const std::shared_ptr<Mesh> &base)
	{
		if (!base)
			return nullptr;
		unsigned int count = base->GetLodCount();
		if (count <= 1 || !base->IsLodBillboard(count - 1))
			return base;
		for (unsigned int i = count; i-- > 1;)
		{
			if (!base->IsLodBillboard(i))
				return base->GetLodMesh(i);
		}
		return base;
	}

	void FramePacket::Reset()
	{
		pipeline.reset();
		camera = PacketCamera();
		opaqueUnits.clear();
		transparentUnits.clear();
		lights.clear();
		instanced.clear();
		particles.clear();
		oceans.clear();
		sky = PacketSky();
		debug = PacketDebug();
		switches.reset();
		hdrMode = false;
		chain.clear();
		chainOverrides.clear();
		windParams = Vector4(1.0f, 0.0f, 1.0f, 1.0f);
		csmSplits = { 0.0f, 0.0f, 0.0f, 0.0f };
		csmMapSize = 1024;
		engineTime = 0.0f;
		windowW = windowH = 0;
		renderTarget = nullptr;
		overlayJobs.clear();
		guiFrame.reset();
		shadowResults.clear();
		frameShadowTemps.clear();
	}

	// Builds the packet camera from the live camera node (game thread only).
	PacketCamera BuildPacketCamera(const std::shared_ptr<SceneNode> &camNode)
	{
		PacketCamera cam;
		if (!camNode)
			return cam;
		auto camera = camNode->GetComponent<Camera>();
		if (!camera)
			return cam;

		cam.worldMatrix = camNode->GetWorldMatrix();
		cam.invertWorldMatrix = camNode->GetInvertWorldMatrix();
		cam.projectionMatrix = camera->GetProjectionMatrix();
		cam.worldPos = camNode->GetWorldPosition();
		cam.nearClip = camera->GetNear();
		cam.farClip = camera->GetFar();
		cam.fov = camera->GetFov();
		cam.shadowFar = camera->GetShadowFar();
		cam.frustum = camera->GetFrustum();
		cam.shadowBounds = camera->GetShadowBounds(false);
		cam.hasShadowBounds = cam.shadowBounds.GetExtents().SquareLength() > 0;
		cam.valid = true;
		return cam;
	}
}
