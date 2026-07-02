#ifdef WITH_EDITOR

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorReflect.hpp"
#include "Fury/Camera.h"
#include "Fury/Component.h"
#include "Fury/EnumUtil.h"
#include "Fury/Light.h"
#include "Fury/Material.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Transform.h"
#include "Fury/Uniform.h"

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
			void RenderGizmoSection()
			{
				if (!ImGui::CollapsingHeader("Gizmo", ImGuiTreeNodeFlags_DefaultOpen)) return;

				bool changed = false;

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

			bool g_NodeShowWorld = false;

			void RenderSceneNodeSection(SceneNode* node)
			{
				if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen)) return;

				ImGui::Text("Name: %s", node->GetName().empty() ? "(unnamed)" : node->GetName().c_str());

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

			// ----- Component body renderers -----
			// Each renderer takes the SceneNode (for context) and the typed
			// component pointer. They are dispatched from the generic loop
			// in RenderNodePropertiesWindow below.
			void RenderTransformBody(SceneNode* node, Transform* t)
			{
				Vector4 pos = t->GetPosition();
				if (ImReflect::Input("Position", pos).get<Vector4>().is_changed())
				{
					t->SetPreTransforms(pos, t->GetRotation(), t->GetScale());
				}
				Quaternion rot = t->GetRotation();
				if (ImReflect::Input("Rotation", rot).get<Quaternion>().is_changed())
				{
					t->SetPreTransforms(t->GetPosition(), rot, t->GetScale());
				}
				Vector4 scl = t->GetScale();
				if (ImReflect::Input("Scale", scl).get<Vector4>().is_changed())
				{
					t->SetPreTransforms(t->GetPosition(), t->GetRotation(), scl);
				}
			}

			void RenderLightBody(SceneNode* node, Light* light)
			{
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
					light->EvaluateVolume();
				}
			}

			// Camera keeps the full body for v1 -- the only component whose
			// editable state isn't trivially "remove + re-add" to recover
			// from a mistake. Drop the static s_Aspect in favor of
			// GetAspect-ish behavior by tracking the user's last value
			// across edits.
			void RenderCameraBody(SceneNode* node, Camera* cam)
			{
				static float s_Aspect = 16.0f / 9.0f;
				float fov = cam->GetFov();
				if (ImGui::DragFloat("FOV (deg)", &fov, 0.5f, 1.0f, 179.0f, "%.1f"))
				{
					cam->PerspectiveFov(fov, s_Aspect, cam->GetNear(), cam->GetFar());
				}
				float near_p = cam->GetNear();
				if (ImGui::DragFloat("Near", &near_p, 0.01f, 0.001f, 100.0f))
				{
					cam->PerspectiveFov(fov, s_Aspect, near_p, cam->GetFar());
				}
				float far_p = cam->GetFar();
				if (ImGui::DragFloat("Far", &far_p, 1.0f, 1.0f, 10000.0f))
				{
					cam->PerspectiveFov(fov, s_Aspect, cam->GetNear(), far_p);
				}
				if (ImGui::DragFloat("Aspect", &s_Aspect, 0.01f, 0.1f, 10.0f, "%.2f"))
				{
					cam->SetAspect(s_Aspect);
				}
				float shadow_far = cam->GetShadowFar();
				if (ImGui::DragFloat("Shadow Far", &shadow_far, 1.0f, 1.0f, 1000.0f))
				{
					cam->SetShadowFar(shadow_far);
				}
				BoxBounds sb = cam->GetShadowBounds(false);
				Vector4 mn = sb.GetMin();
				Vector4 mx = sb.GetMax();
				bool bounds_changed = false;
				bounds_changed |= ImGui::DragFloat4("Shadow Min", &mn.x, 0.5f);
				bounds_changed |= ImGui::DragFloat4("Shadow Max", &mx.x, 0.5f);
				if (bounds_changed)
				{
					cam->SetShadowBounds(mn, mx);
				}
			}

			// Read-only texture row. 48×48 thumb + key + meta. Designed
			// to be cheap enough to drop into a scrollable child.
			void RenderMaterialTextureRow(Material* mat, const std::string& key)
			{
				if (!mat) return;
				auto tex = mat->GetTexture(key);
				if (tex)
				{
					ImGui::Image((ImTextureID)(intptr_t)tex->GetID(),
						ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
					ImGui::SameLine();
					ImGui::BeginGroup();
					ImGui::Text("%s", key.c_str());
					ImGui::Text("%s  %d × %d  %s  %s",
						tex->GetName().empty() ? "(unnamed)" : tex->GetName().c_str(),
						tex->GetWidth(), tex->GetHeight(),
						EnumUtil::TextureFormatToString(tex->GetFormat()).c_str(),
						tex->IsSRGB() ? "sRGB" : "linear");
					ImGui::EndGroup();
				}
				else
				{
					ImGui::Dummy(ImVec2(48, 48));
					ImGui::SameLine();
					ImGui::Text("%s (none)", key.c_str());
				}
			}

			// Material section body: just the texture slots, listed in a
			// fixed-height scrollable child. Standard slots (DIFFUSE /
			// SPECULAR / NORMAL) first, then any custom keys the material
			// exposes via GetTextures(). Opaque toggle intentionally
			// omitted -- the inspector is read-only; a future dedicated
			// material editor owns the editing surface.
			void RenderMaterialSectionBody(Material* mat)
			{
				if (!mat) return;
				std::vector<std::string> keys;
				auto add = [&](const std::string& k)
				{
					if (std::find(keys.begin(), keys.end(), k) == keys.end())
						keys.push_back(k);
				};
				add(Material::DIFFUSE_TEXTURE);
				add(Material::SPECULAR_TEXTURE);
				add(Material::NORMAL_TEXTURE);
				for (auto& pair : mat->GetTextures()) add(pair.first);

				// AlwaysVerticalScrollbar keeps the list height stable.
				ImGui::BeginChild("##textures", ImVec2(0, 140), true,
					ImGuiWindowFlags_AlwaysVerticalScrollbar);
				for (auto& key : keys)
				{
					ImGui::PushID(key.c_str());
					RenderMaterialTextureRow(mat, key);
					ImGui::PopID();
				}
				ImGui::EndChild();
			}

			// MeshRender body: cast-shadows + one-line mesh summary +
			// each bound material as its own sub-section (which contains
			// the texture list). Deliberately minimal -- the inspector is
			// for at-a-glance reading, not editing.
			void RenderMeshRenderBody(SceneNode* node, MeshRender* mr)
			{
				auto mesh = mr->GetMesh();
				if (mesh)
				{
					// Cast Shadows is a per-MeshRender flag now, not a
					// per-Mesh one. The mesh is a shared resource, so the
					// knob lives on the instance — toggling one tank's
					// shadow leaves the others that share the mesh alone.
					bool cast = mr->GetCastShadows();
					if (ImGui::Checkbox("Cast Shadows", &cast))
					{
						mr->SetCastShadows(cast);
					}
					unsigned int totalVerts = static_cast<unsigned int>(mesh->Positions.Data.size() / 3);
					unsigned int totalIndices = 0;
					for (unsigned int i = 0; i < mesh->GetSubMeshCount(); ++i)
					{
						auto sm = mesh->GetSubMeshAt(i);
						if (sm) totalIndices += static_cast<unsigned int>(sm->Indices.Data.size());
					}
					ImGui::Text("%s    %u verts  ·  %u tris",
						mesh->GetName().c_str(), totalVerts, totalIndices / 3);
				}
				else
				{
					ImGui::TextDisabled("(no mesh)");
				}

				// One sub-section per bound material slot. Empty slots
				// are skipped (the inspector is for what's there, not
				// what could be there).
				for (unsigned int i = 0; i < mr->GetMaterialCount(); ++i)
				{
					auto mat = mr->GetMaterial(i);
					if (!mat) continue;
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::CollapsingHeader(mat->GetName().empty() ? "(unnamed material)" : mat->GetName().c_str(),
						ImGuiTreeNodeFlags_DefaultOpen))
					{
						RenderMaterialSectionBody(mat.get());
					}
					ImGui::PopID();
				}
			}

			// ----- Component dispatch table -----
			struct ComponentEntry
			{
				std::string name;
				std::type_index type;
				bool removable;       // false for Transform
				void (*render)(SceneNode*, Component*);
			};
			static const std::vector<ComponentEntry>& ComponentRenderTable()
			{
				static const std::vector<ComponentEntry> table = {
					{ "Transform",  typeid(Transform),  false, [](SceneNode* n, Component* c) { RenderTransformBody(n, static_cast<Transform*>(c)); } },
					{ "Light",      typeid(Light),      true,  [](SceneNode* n, Component* c) { RenderLightBody(n, static_cast<Light*>(c)); } },
					{ "Camera",     typeid(Camera),     true,  [](SceneNode* n, Component* c) { RenderCameraBody(n, static_cast<Camera*>(c)); } },
					{ "MeshRender", typeid(MeshRender), true,  [](SceneNode* n, Component* c) { RenderMeshRenderBody(n, static_cast<MeshRender*>(c)); } },
				};
				return table;
			}

			// Render one component as a top-level CollapsingHeader with a
			// full-width delete button at the bottom (skipped for Transform).
			void RenderComponentSection(SceneNode* node, const ComponentEntry& entry)
			{
				auto comp = node->GetComponent(entry.type);
				if (!comp) return;

				ImGui::PushID(entry.name.c_str());
				// Body + delete button both inside `if (open)` so the delete
				// button hides when the section is collapsed.
				if (ImGui::CollapsingHeader(entry.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					entry.render(node, comp.get());
					if (entry.removable)
					{
						if (ImGui::Button(("Delete " + entry.name).c_str(),
							ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
						{
							node->RemoveComponent(entry.type);
							Editor::MarkSceneDirty();
						}
					}
				}
				ImGui::PopID();
			}

			// Full-width "+ Add Component" button. Lists every registry entry
			// except Transform; skips entries the node already has.
			void RenderAddComponentButton(SceneNode* node)
			{
				if (ImGui::Button("+ Add Component",
					ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
				{
					ImGui::OpenPopup("AddComponentPopup");
				}
				if (ImGui::BeginPopup("AddComponentPopup"))
				{
					for (const auto& entry : ComponentRenderTable())
				{
						if (!entry.removable) continue;
						if (node->GetComponent(entry.type)) continue;
						if (ImGui::MenuItem(entry.name.c_str()))
					{
						auto it = SceneNode::ComponentRegistry.find(entry.name);
						if (it != SceneNode::ComponentRegistry.end())
						{
							auto comp = it->second();
							node->AddComponent(comp);
							Editor::MarkSceneDirty();
						}
					}
				}
				ImGui::EndPopup();
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

				// One section per component, in registration order. No
				// enclosing "Components" header -- each is a peer in the panel.
				for (const auto& entry : ComponentRenderTable())
				{
					RenderComponentSection(node, entry);
				}

				RenderAddComponentButton(node);

				ImGui::End();
			}
		}
}

#endif // WITH_EDITOR
