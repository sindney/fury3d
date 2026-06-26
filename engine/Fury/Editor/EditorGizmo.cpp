#ifdef WITH_EDITOR

#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorPicking.hpp"

#include "Fury/Camera.h"
#include "Fury/MathUtil.h"
#include "Fury/Matrix4.h"
#include "Fury/Pipeline.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

#include "ImGuizmo.h"

#include <cstring>

namespace fury
{
	namespace Editor
	{
		extern SceneNode* g_SelectedSceneNode;

		// Gizmo state — single source of truth. EditorNodeProperties.cpp
		// reads/writes these to drive its Gizmo section UI; the runtime
		// settings handler in Editor.cpp persists them via imgui.ini.
		// Defined here as fileless globals so external linkage is explicit.
		ImGuizmo::OPERATION g_GizmoOp     = ImGuizmo::TRANSLATE;
		ImGuizmo::MODE      g_GizmoSpace  = ImGuizmo::WORLD;
		bool                g_SnapEnabled = false;
		float               g_SnapTranslate = 1.0f;
		float               g_SnapRotate    = 15.0f;   // degrees
		float               g_SnapScale     = 0.1f;

		// Sanity check that Matrix4::Raw is the layout ImGuizmo expects:
		// 16 contiguous floats in column-major order. Anything else means
		// the *.Raw[0] passes below would silently swap rows/columns.
		static_assert(sizeof(Matrix4) == sizeof(float) * 16,
			"Matrix4 layout must be 16 contiguous floats (column-major) for ImGuizmo");

		// Render the TRS gizmo over the selected node, sourced from the
		// active pipeline's camera. Called from Editor::Tick after the
		// dockspace + windows render but before user `on_update` (which
		// runs Pipeline::Execute / Gui::Render). ImGuizmo emits ImGui draw
		// commands, so it must run inside the same NewFrame/Render bracket.
		//
		// `central_rect_min` / `central_rect_size` come from the dockspace
		// central node so the gizmo only interacts in the 3D viewport area.
		void RenderGizmo(const ImVec2& central_rect_min, const ImVec2& central_rect_size)
		{
			// ImGuizmo's per-frame setup MUST run every frame so its hover
			// state stays correct, even when the gizmo isn't drawn. We
			// install the rect + drawlist unconditionally, then short-
			// circuit the Manipulate call when there's nothing to draw.
			ImGuizmo::BeginFrame();
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetRect(central_rect_min.x, central_rect_min.y,
				central_rect_size.x, central_rect_size.y);

			// Gate on prerequisites — without these, the world matrix has
			// no meaning and ImGuizmo would render at the origin.
			if (!Scene::Active) return;
			if (!Pipeline::Active) return;
			auto cameraNode = Pipeline::Active->GetCurrentCamera();
			if (!cameraNode) return;
			auto camera = cameraNode->GetComponent<Camera>();
			if (!camera) return;
			if (!g_SelectedSceneNode) return;

			// Don't manipulate while a pick is mid-flight: the click that
			// started the pick belongs to picking, not to a gizmo drag.
			if (Picking::IsPickInFlight()) return;

			// Build a fullscreen invisible window that owns the central
			// rect, so ImGuizmo can attach to its drawlist (which sits
			// above the dockspace passthru). NoInputs lets clicks fall
			// through to ImGuizmo's own hit-testing.
			ImGui::SetNextWindowPos(central_rect_min);
			ImGui::SetNextWindowSize(central_rect_size);
			ImGuiWindowFlags flags =
				ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoResize     | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoScrollWithMouse |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoNavFocus   | ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoInputs;
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			if (ImGui::Begin("##GizmoCanvas", nullptr, flags))
			{
				ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

				Matrix4 view  = cameraNode->GetInvertWorldMatrix();
				Matrix4 proj  = camera->GetProjectionMatrix();
				Matrix4 world = g_SelectedSceneNode->GetWorldMatrix();

				// Snap value: ImGuizmo expects either nullptr or a 3-float
				// array (x/y/z) regardless of operation; we replicate the
				// scalar across components. Rotation snap is interpreted
				// in degrees by ImGuizmo, matching our UI.
				float snapValues[3] = { 0, 0, 0 };
				const float* snapPtr = nullptr;
				if (g_SnapEnabled)
				{
					float v = g_SnapTranslate;
					if (g_GizmoOp == ImGuizmo::ROTATE) v = g_SnapRotate;
					else if (g_GizmoOp == ImGuizmo::SCALE) v = g_SnapScale;
					snapValues[0] = snapValues[1] = snapValues[2] = v;
					snapPtr = snapValues;
				}

				// Gizmo always operates in WORLD space. Local-axis
				// manipulation surfaced from the UI was confusing —
				// SCALE silently forced LOCAL anyway, ROTATE in
				// LOCAL didn't visibly differ for unrotated nodes,
				// and TRANSLATE in LOCAL means "drag along the
				// node's own axes" which most users don't expect by
				// default. Direct world-space drag matches Unity /
				// Godot's default behavior. The persisted
				// `g_GizmoSpace` is kept for forward-compat with
				// imgui.ini files that already wrote a space index.
				ImGuizmo::MODE mode = ImGuizmo::WORLD;

				ImGuizmo::Manipulate(&view.Raw[0], &proj.Raw[0],
					g_GizmoOp, mode, &world.Raw[0], nullptr, snapPtr);

				if (ImGuizmo::IsUsing())
				{
					// Convert the modified world matrix back to a local
					// matrix on the selected node, then decompose. We
					// only write the components affected by the active
					// op so a translate drag doesn't quantize the
					// existing rotation through round-trip.
					Matrix4 parent_world;
					if (auto parent = g_SelectedSceneNode->GetParent())
						parent_world = parent->GetWorldMatrix();
					Matrix4 local = parent_world.Inverse() * world;

					Vector4 t, s;
					Quaternion r;
					if (MathUtil::Decompose(local, t, r, s))
					{
						switch (g_GizmoOp)
						{
						case ImGuizmo::TRANSLATE:
							g_SelectedSceneNode->SetLocalPosition(t);
							break;
						case ImGuizmo::ROTATE:
							g_SelectedSceneNode->SetLocalRoattion(r);
							break;
						case ImGuizmo::SCALE:
							g_SelectedSceneNode->SetLocalScale(s);
							break;
						default:
							break;
						}
						g_SelectedSceneNode->Recompose(false);
					}
				}
			}
			ImGui::End();
			ImGui::PopStyleVar();
		}

		// Public API mirroring Editor::SetWindowVisible's tolerant-name
		// pattern: unknown names are silently ignored.
		void SetGizmoMode(const char* name)
		{
			if (!name) return;
			if      (std::strcmp(name, "translate") == 0) g_GizmoOp = ImGuizmo::TRANSLATE;
			else if (std::strcmp(name, "rotate")    == 0) g_GizmoOp = ImGuizmo::ROTATE;
			else if (std::strcmp(name, "scale")     == 0) g_GizmoOp = ImGuizmo::SCALE;
		}

		void SetGizmoSpace(const char* name)
		{
			if (!name) return;
			if      (std::strcmp(name, "local") == 0) g_GizmoSpace = ImGuizmo::LOCAL;
			else if (std::strcmp(name, "world") == 0) g_GizmoSpace = ImGuizmo::WORLD;
		}

		void SetSnapEnabled(bool enabled) { g_SnapEnabled = enabled; }

		const char* GetGizmoMode()
		{
			switch (g_GizmoOp)
			{
				case ImGuizmo::ROTATE:    return "rotate";
				case ImGuizmo::SCALE:     return "scale";
				case ImGuizmo::TRANSLATE: return "translate";
				default:                  return "translate";
			}
		}

		const char* GetGizmoSpace()
		{
			return g_GizmoSpace == ImGuizmo::LOCAL ? "local" : "world";
		}

		bool GetSnapEnabled() { return g_SnapEnabled; }
	}
}

#endif // WITH_EDITOR
