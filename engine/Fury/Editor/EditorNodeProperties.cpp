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

namespace fury
{
	namespace Editor
	{
		extern SceneNode* g_SelectedSceneNode;

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

			void RenderSceneNodeSection(SceneNode* node)
			{
				if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen)) return;

				ImGui::Text("Name: %s", node->GetName().empty() ? "(unnamed)" : node->GetName().c_str());

				bool changed = false;

				Vector4 pos = node->GetLocalPosition();
				if (ImReflect::Input("Local Position", pos).get<Vector4>().is_changed())
				{
					node->SetLocalPosition(pos);
					changed = true;
				}

				Quaternion rot = node->GetLocalRoattion();
				if (ImReflect::Input("Local Rotation", rot).get<Quaternion>().is_changed())
				{
					node->SetLocalRoattion(rot);
					changed = true;
				}

				Vector4 scl = node->GetLocalScale();
				if (ImReflect::Input("Local Scale", scl).get<Vector4>().is_changed())
				{
					node->SetLocalScale(scl);
					changed = true;
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

				if (aabb_dirty) light->CalculateAABB();
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

			// Dangling-pointer walk: if the selected node was removed from the
			// active scene since selection, drop the stale pointer.
			if (g_SelectedSceneNode && Scene::Active)
			{
				if (!IsReachable(Scene::Active->GetRootNode(), g_SelectedSceneNode))
				{
					g_SelectedSceneNode = nullptr;
				}
			}
			else if (g_SelectedSceneNode && !Scene::Active)
			{
				g_SelectedSceneNode = nullptr;
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
