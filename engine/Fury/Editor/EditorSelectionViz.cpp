#ifdef WITH_EDITOR

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
#include "Fury/Pipeline.h"
#include "Fury/RenderTarget.h"
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

		namespace
		{
			// Distinct from the Profiler's per-light-color debug overlay so
			// the two are visually distinguishable when both are on.
			const Color kSelectionColor(0.0f, 1.0f, 0.5f, 1.0f);

			// Bind the pipeline's render target (if any) so the overlay
			// composites over the scene inside the Viewport window's
			// render target. Returns true when the caller must restore
			// framebuffer 0 (i.e. an RT was bound). Saves the prior FBO +
			// viewport so the caller can restore them exactly.
			bool BindRenderTargetForOverlay(GLint& prev_fbo, GLint prev_vp[4])
			{
				if (!Pipeline::Active) return false;
				auto* rt = Pipeline::Active->GetRenderTarget();
				if (rt == nullptr || !rt->IsAllocated()) return false;

				glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
				glGetIntegerv(GL_VIEWPORT, prev_vp);

				glBindFramebuffer(GL_FRAMEBUFFER, rt->GetFBO());
				glViewport(0, 0, rt->GetWidth(), rt->GetHeight());
				return true;
			}

			void RestoreFramebuffer(bool bound, GLint prev_fbo, const GLint prev_vp[4])
			{
				if (!bound) return;
				glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
				glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
			}
		}

		// Draw a wireframe overlay for the currently selected SceneNode.
		// Mirrors the Profiler's debug-overlay primitives (RenderUtil::
		// DrawBoxBounds / DrawMesh in wireframe) but for the single
		// selected node only, independent of PipelineSwitch flags. Called
		// from Editor::TickPostRender after Pipeline::Execute so it
		// composites over the scene rendered into the viewport RT, before
		// Gui::Render samples that RT for the Viewport window's Image.
		void DrawSelectionOverlay()
		{
			if (!g_SelectedSceneNode) return;
			if (!Pipeline::Active) return;
			auto cameraNode = Pipeline::Active->GetCurrentCamera();
			if (!cameraNode) return;
			if (!cameraNode->GetComponent<Camera>()) return;

			GLint prev_fbo = 0;
			GLint prev_vp[4] = { 0, 0, 0, 0 };
			const bool bound = BindRenderTargetForOverlay(prev_fbo, prev_vp);

			// The overlay must composite over the scene regardless of depth,
			// but the RT's depth buffer still holds the final composite
			// pass's near depth (~0), which would fail LESS testing for any
			// world-space wireframe. Disable depth for the overlay draw.
			GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
			glDisable(GL_DEPTH_TEST);

			auto renderUtil = RenderUtil::Instance();
			auto node = g_SelectedSceneNode;

			if (auto light = node->GetComponent<Light>())
			{
				const LightType lt = light->GetType();
				if (lt == LightType::POINT)
				{
					// Unit ico-sphere scaled by radius — mirrors
					// Pipeline::DrawDebug's POINT-light path.
					Matrix4 world = node->GetWorldMatrix();
					world.AppendScale(Vector4(light->GetRadius(), 0.0f));
					renderUtil->BeginDrawMeshs(cameraNode);
					renderUtil->DrawMesh(light->GetMesh(), world, kSelectionColor);
					renderUtil->EndDrawMeshes();
				}
				else if (lt == LightType::SPOT)
				{
					// Cone is pre-shaped by EvaluateVolume (radius + outer
					// angle) — no extra scaling needed.
					renderUtil->BeginDrawMeshs(cameraNode);
					renderUtil->DrawMesh(light->GetMesh(), node->GetWorldMatrix(), kSelectionColor);
					renderUtil->EndDrawMeshes();
				}
				// DIRECTIONAL: infinite bounds — skip.
			}
			else
			{
				// Mesh / other: world AABB wireframe. Skip infinite or
				// invalid bounds (e.g. empty scene-root selection).
				const BoxBounds aabb = node->GetWorldAABB();
				if (!aabb.GetInfinite() && aabb.Valid())
				{
					renderUtil->BeginDrawLines(cameraNode);
					renderUtil->DrawBoxBounds(aabb, kSelectionColor);
					renderUtil->EndDrawLines();
				}
			}

			RestoreFramebuffer(bound, prev_fbo, prev_vp);
			if (prev_depth) glEnable(GL_DEPTH_TEST);
		}
	}
}

#endif // WITH_EDITOR
