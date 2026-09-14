#if WITH_EDITOR

#include "Fury/Editor/Editor.h"

#include "Fury/BoxBounds.h"
#include "Fury/Camera.h"
#include "Fury/Color.h"
#include "Fury/GLLoader.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/FramePacket.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderTarget.h"
#include "Fury/RenderThread.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Transform.h"
#include "Fury/Vector4.h"

namespace fury
{
	namespace Editor
	{
		extern SceneNode* g_SelectedSceneNode;

	// Everything the render-side selection overlay needs (no scene reads on
	// the GL thread).
	struct SelectionOverlayRequest
	{
		PacketCamera camera;

		RenderTarget *rt = nullptr;

		int variant = 0;   // 0 none, 1 light-point, 2 light-spot, 3 camera-frustum, 4 aabb

		std::shared_ptr<Mesh> mesh;   // light volume mesh

		Matrix4 world;

		Frustum frustum;

		BoxBounds aabb;
	};

	// Distinct from the Profiler's per-light-color debug overlay so
	// the two are visually distinguishable when both are on.
	const Color kSelectionColor(0.0f, 1.0f, 0.5f, 1.0f);

	// GL-thread overlay execution (runs as a frame-packet overlay job:
	// inline at the loop tail when single-threaded, on the render
	// thread when threaded).
	void ExecuteSelectionOverlay(const SelectionOverlayRequest &req)
	{
		if (req.variant == 0) return;

		GLint prev_fbo = 0;
		GLint prev_vp[4] = { 0, 0, 0, 0 };
		bool bound = false;
		if (req.rt != nullptr && req.rt->IsAllocated())
		{
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
			glGetIntegerv(GL_VIEWPORT, prev_vp);
			glBindFramebuffer(GL_FRAMEBUFFER, req.rt->GetFBO());
			glViewport(0, 0, req.rt->GetWidth(), req.rt->GetHeight());
			bound = true;
		}

		// The overlay must composite over the scene regardless of depth,
		// but the RT's depth buffer still holds the final composite
		// pass's near depth (~0), which would fail LESS testing for any
		// world-space wireframe. Disable depth for the overlay draw.
		GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
		glDisable(GL_DEPTH_TEST);

		auto renderUtil = RenderUtil::Instance();

		switch (req.variant)
		{
		case 1:
			renderUtil->BeginDrawMeshs(req.camera);
			renderUtil->DrawMesh(req.mesh, req.world, kSelectionColor);
			renderUtil->EndDrawMeshes();
			break;
		case 2:
			renderUtil->BeginDrawMeshs(req.camera);
			renderUtil->DrawMesh(req.mesh, req.world, kSelectionColor);
			renderUtil->EndDrawMeshes();
			break;
		case 3:
			renderUtil->BeginDrawLines(req.camera);
			renderUtil->DrawFrustum(req.frustum, kSelectionColor);
			renderUtil->EndDrawLines();
			break;
		case 4:
			renderUtil->BeginDrawLines(req.camera);
			renderUtil->DrawBoxBounds(req.aabb, kSelectionColor);
			renderUtil->EndDrawLines();
			break;
		default:
			break;
		}

		if (bound)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
			glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
		}
		if (prev_depth) glEnable(GL_DEPTH_TEST);
	}

	// Game thread: build the overlay request for the currently selected
	// SceneNode and attach it to the staged frame packet. Called from
	// Editor::TickPostRender after Pipeline::Execute so the job composites
	// over the scene rendered into the viewport RT, before the GUI pass
	// samples that RT for the Viewport window's Image.
	void DrawSelectionOverlay()
	{
		if (!g_SelectedSceneNode) return;
		if (!Pipeline::Active) return;
		auto cameraNode = Pipeline::Active->GetCurrentCamera();
		if (!cameraNode) return;
		if (!cameraNode->GetComponent<Camera>()) return;

		FramePacket *packet = RenderThread::Get().PeekStagedPacket();
		if (packet == nullptr) return;

		auto node = g_SelectedSceneNode;
		SelectionOverlayRequest req;
		req.camera = BuildPacketCamera(cameraNode);
		req.rt = Pipeline::Active->GetRenderTarget();

		if (auto light = node->GetComponent<Light>())
		{
			const LightType lt = light->GetType();
			if (lt == LightType::POINT)
			{
				// Unit ico-sphere scaled by radius -- mirrors
				// Pipeline::DrawDebug's POINT-light path.
				req.variant = 1;
				req.mesh = light->GetMesh();
				req.world = node->GetWorldMatrix();
				req.world.AppendScale(Vector4(light->GetRadius(), 0.0f));
			}
			else if (lt == LightType::SPOT)
			{
				// Cone is pre-shaped by EvaluateVolume (radius + outer
				// angle) -- no extra scaling needed.
				req.variant = 2;
				req.mesh = light->GetMesh();
				req.world = node->GetWorldMatrix();
			}
			// DIRECTIONAL: infinite bounds -- skip.
		}
		else if (auto cam = node->GetComponent<Camera>())
		{
			// Camera frustum (far capped so it stays readable).
			req.variant = 3;
			const float cappedFar = std::min(cam->GetFar(), 1000.0f);
			req.frustum = cam->GetFrustum(cam->GetNear(), cappedFar);
		}
		else
		{
			// Mesh / other: world AABB wireframe. Skip infinite or
			// invalid bounds (e.g. empty scene-root selection).
			const BoxBounds aabb = node->GetWorldAABB();
			if (!aabb.GetInfinite() && aabb.Valid())
			{
				req.variant = 4;
				req.aabb = aabb;
			}
		}

		if (req.variant == 0) return;
		packet->overlayJobs.emplace_back([req]() { ExecuteSelectionOverlay(req); });
	}
}
}

#endif // WITH_EDITOR
