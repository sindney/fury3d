#ifdef WITH_EDITOR

#include <cctype>
#include <filesystem>
#include <memory>
#include <string>

#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorReflect.hpp"
#include "Fury/Light.h"
#include "Fury/MathUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Transform.h"

#include "ImGui/imgui.h"

#include "ImGuizmo.h"

namespace fury
{
	namespace Editor
	{
		extern SceneNode* g_SelectedSceneNode;

		// Gizmo state lives in EditorGizmo.cpp; we read/write it directly
		// from the Gizmo CollapsingHeader rendered at the top of this
		// window. Marking imgui.ini dirty on each change persists the
		// new value via the FuryEditor settings handler.
		extern ImGuizmo::OPERATION g_GizmoOp;
		extern ImGuizmo::MODE      g_GizmoSpace;
		extern bool                g_SnapEnabled;
		extern float               g_SnapTranslate;
		extern float               g_SnapRotate;
		extern float               g_SnapScale;

		namespace
		{
			// Walks Scene::Active->GetRootNode() and returns true iff `target`
			// is reachable. Cheap (the editor's selection invariant).
			bool IsReachable(const std::shared_ptr<SceneNode>& root, SceneNode* target)
			{
				if (!root || !target) return false;
				if (root.get() == target) return true;
				for (unsigned int i = 0; i < root->GetChildCount(); ++i)
				{
					if (IsReachable(root->GetChildAt(i), target)) return true;
				}
				return false;
			}

			// Top-of-window Gizmo controls. Always rendered (independent of
			// selection) so the user can pick a default mode before clicking
			// a node. Each control marks imgui.ini dirty on change so the
			// FuryEditor settings handler persists the new value.
			//
			// The gizmo always operates in world space — this is the
			// convention DCC tools (Maya / Blender / Unity / Godot in the
			// default mode) settle on for direct manipulation: dragging
			// the green axis moves the object along world Y regardless of
			// its parent transform. The Local/World toggle that USED to
			// live here was confusing because picking SCALE forced LOCAL
			// implicitly anyway. The toggle now lives in the Node section
			// and controls READOUT of position/rotation/scale numbers.
			void RenderGizmoSection()
			{
				if (!ImGui::CollapsingHeader("Gizmo", ImGuiTreeNodeFlags_DefaultOpen)) return;

				bool changed = false;

				// Mode radio buttons.
				if (ImGui::RadioButton("Translate", g_GizmoOp == ImGuizmo::TRANSLATE))
				{
					g_GizmoOp = ImGuizmo::TRANSLATE;
					changed = true;
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("Rotate", g_GizmoOp == ImGuizmo::ROTATE))
				{
					g_GizmoOp = ImGuizmo::ROTATE;
					changed = true;
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("Scale", g_GizmoOp == ImGuizmo::SCALE))
				{
					g_GizmoOp = ImGuizmo::SCALE;
					changed = true;
				}

				if (ImGui::Checkbox("Snap", &g_SnapEnabled))
				{
					changed = true;
				}

				// Snap-step inputs are visible only when snap is on. We
				// hide rather than disable so they don't add visual noise
				// in the common case (snap off).
				if (g_SnapEnabled)
				{
					if (ImGui::DragFloat("Translate Step", &g_SnapTranslate, 0.1f, 0.001f, 1000.0f, "%.3f"))
					{
						changed = true;
					}
					if (ImGui::DragFloat("Rotate Step",    &g_SnapRotate,    0.5f, 0.1f,    180.0f,  "%.1f deg"))
					{
						changed = true;
					}
					if (ImGui::DragFloat("Scale Step",     &g_SnapScale,     0.01f, 0.001f, 100.0f, "%.3f"))
					{
						changed = true;
					}
				}

				if (changed) ImGui::MarkIniSettingsDirty();
			}

			// Per-window state: which transform space the Node section
			// reads / edits. Local is the canonical model (matches what
			// SceneNode actually stores); World shows the composed
			// transform and is convenient for sanity checks. We keep
			// this as a window-local flag rather than persisted state —
			// the user picks display mode per session.
			bool g_NodeShowWorld = false;

			void RenderSceneNodeSection(SceneNode* node)
			{
				if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen)) return;

				ImGui::Text("Name: %s", node->GetName().empty() ? "(unnamed)" : node->GetName().c_str());

				// Local / World display toggle. Local is the editable
				// canonical state; World is read-only (decomposed from
				// the cached world matrix) — mirrors what most DCC
				// tools do, since editing a world-space transform
				// implies an inverse-parent-multiply that doesn't
				// round-trip cleanly when parents have non-uniform
				// scale.
				if (ImGui::RadioButton("Local##NodeSpace", !g_NodeShowWorld)) g_NodeShowWorld = false;
				ImGui::SameLine();
				if (ImGui::RadioButton("World##NodeSpace",  g_NodeShowWorld)) g_NodeShowWorld = true;

				bool changed = false;

				if (!g_NodeShowWorld)
				{
					Vector4 pos = node->GetLocalPosition();
					if (ImReflect::Input("Position", pos).get<Vector4>().is_changed())
					{
						node->SetLocalPosition(pos);
						changed = true;
					}

					Quaternion rot = node->GetLocalRoattion();
					if (ImReflect::Input("Rotation", rot).get<Quaternion>().is_changed())
					{
						node->SetLocalRoattion(rot);
						changed = true;
					}

					Vector4 scl = node->GetLocalScale();
					if (ImReflect::Input("Scale", scl).get<Vector4>().is_changed())
					{
						node->SetLocalScale(scl);
						changed = true;
					}
				}
				else
				{
					// World readout — read-only. Editing world transforms
					// when a parent has non-uniform scale produces shear
					// that the local TRS slot can't represent, so the
					// cleanest UX is to expose World as inspect-only.
					Vector4 pos = node->GetWorldPosition();
					Quaternion rot = node->GetWorldRoattion();
					Vector4 scl = node->GetWorldScale();
					ImGui::BeginDisabled();
					ImReflect::Input("Position", pos);
					ImReflect::Input("Rotation", rot);
					ImReflect::Input("Scale", scl);
					ImGui::EndDisabled();
				}

				if (changed) node->Recompose(false);
			}

			void RenderLightSection(Light* light)
			{
				if (!ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) return;

				bool aabb_dirty = false;

				LightType lt = light->GetType();
				if (ImReflect::Input("Type", lt).get<LightType>().is_changed())
				{
					light->SetType(lt);
					aabb_dirty = true;
				}

				Color col = light->GetColor();
				if (ImReflect::Input("Color", col).get<Color>().is_changed())
				{
					light->SetColor(col);
				}

				float intensity = light->GetIntensity();
				if (ImGui::DragFloat("Intensity", &intensity, 0.01f, 0.0f, 100.0f))
				{
					light->SetIntensity(intensity);
				}

				float inner_deg = light->GetInnerAngle() * MathUtil::RadToDeg;
				if (ImGui::DragFloat("Inner Angle", &inner_deg, 0.5f, 0.0f, 180.0f, "%.1f deg"))
				{
					light->SetInnerAngle(inner_deg * MathUtil::DegToRad);
					aabb_dirty = true;
				}

				float outer_deg = light->GetOutterAngle() * MathUtil::RadToDeg;
				if (ImGui::DragFloat("Outer Angle", &outer_deg, 0.5f, 0.0f, 180.0f, "%.1f deg"))
				{
					light->SetOutterAngle(outer_deg * MathUtil::DegToRad);
					aabb_dirty = true;
				}

				float falloff = light->GetFalloff();
				if (ImGui::DragFloat("Falloff", &falloff, 0.01f, 0.0f, 10.0f))
				{
					light->SetFalloff(falloff);
				}

				float radius = light->GetRadius();
				if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 1000.0f))
				{
					light->SetRadius(radius);
					aabb_dirty = true;
				}

				bool cast = light->GetCastShadows();
				if (ImGui::Checkbox("Cast Shadows", &cast))
				{
					light->SetCastShadows(cast);
				}

				if (aabb_dirty)
			{
				light->CalculateAABB();
				// Rebuild the convex volume mesh so the selection overlay
				// (and the Profiler's LIGHT_BOUNDS overlay) draw a sphere /
				// cone consistent with the current radius + outer angle.
				// CalculateAABB alone leaves the cached mesh stale.
				light->EvaluateVolume();
			}
			}
		}

		void RenderNodePropertiesWindow(bool* open)
		{
			ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Node Properties", open))
			{
				ImGui::End();
				return;
			}

			// Header: which scene file is being edited. Empty path → "(no
			// scene)" hint. Non-native source ("[imported] tank.fbx") tells
			// the user Save will route to Save As. Hover for full path.
			{
				const std::string path = Editor::GetCurrentScenePath();
				std::string head;
				if (path.empty())
				{
					head = "Scene: (none)";
				}
				else
				{
					std::filesystem::path p(path);
					const std::string base = p.filename().string();
					const std::string ext  = p.extension().string();
					std::string ext_lower; ext_lower.reserve(ext.size());
					for (char c : ext) ext_lower.push_back((char)std::tolower((unsigned char)c));
					const bool native = (ext_lower == ".json" || ext_lower == ".bin");
					head = native ? ("Scene: " + base) : ("Scene: [imported] " + base);
				}
				ImGui::TextDisabled("%s", head.c_str());
				if (!path.empty() && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", path.c_str());
				}
				ImGui::Separator();
			}

			// Gizmo section is rendered FIRST and unconditionally —
			// the user can configure mode/snap before any node is
			// selected, and switching nodes shouldn't reset state.
			RenderGizmoSection();

		// Dangling-pointer walk: if the selected node was removed from the
		// active scene since selection, drop the stale pointer.
		if (g_SelectedSceneNode && Scene::Active)
		{
			if (!IsReachable(Scene::Active->GetRootNode(), g_SelectedSceneNode))
			{
				SetSelectedSceneNode(nullptr);
			}
		}
		else if (g_SelectedSceneNode && !Scene::Active)
		{
			SetSelectedSceneNode(nullptr);
		}

			if (g_SelectedSceneNode == nullptr)
			{
				ImGui::TextDisabled("(no node selected)");
				ImGui::End();
				return;
			}

			SceneNode* node = g_SelectedSceneNode;
			RenderSceneNodeSection(node);

			if (auto light = node->GetComponent<Light>())
			{
				RenderLightSection(light.get());
			}

			ImGui::End();
		}
	}
}

#endif // WITH_EDITOR
