#ifdef WITH_EDITOR

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorThemes.h"
#include "Fury/Editor/EditorLog.h"
#include "Fury/BufferManager.h"
#include "Fury/BoxBounds.h"
#include "Fury/Camera.h"
#include "Fury/EntityManager.h"
#include "Fury/Log.h"
#include "Fury/Matrix4.h"
#include "Fury/OcTree.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderTarget.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Vector4.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

namespace fury
{
	namespace Editor
	{
		extern SceneIO g_SceneIO;
		extern SceneTreeProvider g_TreeProvider;
		extern CommandHandler g_CommandHandler;
		extern std::vector<CameraControl> g_CameraControls;
		extern std::unordered_map<std::string, bool> g_ImportFlags;
		extern SceneNode* g_SelectedSceneNode;
		extern bool g_ShowViewport;
		extern ImVec2 g_ViewportContentMin;
		extern ImVec2 g_ViewportContentSize;
		extern bool g_ViewportHovered;
		extern bool g_ViewportVisible;

		// Defined in EditorGizmo.cpp.
		void RenderGizmo(const ImVec2& central_rect_min, const ImVec2& central_rect_size);

		// ----------------------------------------------------------------
		// Settings window (Camera / Import / Themes)
		// ----------------------------------------------------------------
		void RenderSettingsWindow(bool* open)
		{
			ImGui::SetNextWindowSize(ImVec2(360, 360), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Settings", open))
			{
				ImGui::End();
				return;
			}

			// --- Camera ---------------------------------------------------
			if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (g_CameraControls.empty())
				{
					ImGui::TextDisabled("(no camera settings registered)");
				}
				else
				{
					for (auto& c : g_CameraControls)
					{
						if (c.kind == "checkbox")
						{
							bool v = c.get_b ? c.get_b() : false;
							if (ImGui::Checkbox(c.label.c_str(), &v))
							{
								if (c.set_b) try { c.set_b(v); } catch (...) {}
							}
						}
						else
						{
							float v = c.get_f ? c.get_f() : 0.0f;
							if (ImGui::SliderFloat(c.label.c_str(), &v, c.vmin, c.vmax))
							{
								if (c.set_f) try { c.set_f(v); } catch (...) {}
							}
						}
					}
				}
			}

			// --- Import ---------------------------------------------------
			if (ImGui::CollapsingHeader("Import", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool v = GetImportFlag("auto_default_sun", true);
				if (ImGui::Checkbox("Auto-Add Default Sun", &v))
				{
					SetImportFlag("auto_default_sun", v);
				}
			}

			// --- Themes ---------------------------------------------------
			if (ImGui::CollapsingHeader("Themes", ImGuiTreeNodeFlags_DefaultOpen))
			{
				int current = GetCurrentThemeIndex();
				if (ImGui::BeginCombo("Theme", kThemes[current].display_name))
				{
					for (std::size_t i = 0; i < kThemesCount; ++i)
					{
						bool is_selected = (current == (int)i);
						if (ImGui::Selectable(kThemes[i].display_name, is_selected))
						{
							ApplyTheme(static_cast<ETheme>(i));
							ImGui::MarkIniSettingsDirty();
						}
						if (is_selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}

			ImGui::End();
		}

		// ----------------------------------------------------------------
		// Profiler window (FPS / GBuffer / Shadows tabs)
		// ----------------------------------------------------------------
		namespace
		{
			void RenderProfilerFpsTab()
			{
				static float upper_bound = 100.0f;
				const float curFps = ImGui::GetIO().Framerate;
				while (curFps > upper_bound) upper_bound += 50;
				while (upper_bound - 50 > curFps) upper_bound -= 50;

				// Use ImGui's built-in PlotLines; no need to maintain a
				// per-label ring buffer for the editor's profiler tab.
				static float values[120] = {};
				static int   values_offset = 0;
				values[values_offset] = curFps;
				values_offset = (values_offset + 1) % IM_ARRAYSIZE(values);

				char overlay[64];
				std::snprintf(overlay, sizeof(overlay), "FPS %d", (int)curFps);
				ImGui::PlotLines("##fps", values, IM_ARRAYSIZE(values), values_offset,
					overlay, 1.0f, upper_bound, ImVec2(-FLT_MIN, 60));

				ImGui::Separator();
				ImGui::Text("CPU Mem: %u mb", BufferManager::Instance()->GetMemoryInMegaByte(false));
				ImGui::Text("GPU Mem: %u mb", BufferManager::Instance()->GetMemoryInMegaByte(true));

				ImGui::Separator();
				ImGui::Text("DrawCall: %u",    RenderUtil::Instance()->GetDrawCall());
				ImGui::Text("Triangles: %u",   RenderUtil::Instance()->GetTriangleCount());
				ImGui::Text("Mesh: %u",        RenderUtil::Instance()->GetMeshCount());
				ImGui::Text("SkinnedMesh: %u", RenderUtil::Instance()->GetSkinnedMeshCount());
				ImGui::Text("Light: %u",       RenderUtil::Instance()->GetLightCount());

				ImGui::Separator();

				static bool draw_light_bounds = false;
				static bool draw_mesh_bounds = false;
				static bool draw_custom_bounds = false;
				static bool use_csm = true;
				ImGui::Checkbox("Draw Light Bounds",       &draw_light_bounds);
				ImGui::Checkbox("Draw Mesh Bounds",        &draw_mesh_bounds);
				ImGui::Checkbox("Draw Custom Bounds",      &draw_custom_bounds);
				ImGui::Checkbox("Use Cascaded Shadow Map", &use_csm);
				if (Pipeline::Active)
				{
					Pipeline::Active->SetSwitch(PipelineSwitch::LIGHT_BOUNDS,        draw_light_bounds);
					Pipeline::Active->SetSwitch(PipelineSwitch::MESH_BOUNDS,         draw_mesh_bounds);
					Pipeline::Active->SetSwitch(PipelineSwitch::CUSTOM_BOUNDS,       draw_custom_bounds);
					Pipeline::Active->SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, use_csm);
				}

				ImGui::Separator();
				ImGui::Text("OcTree:");

				auto sm = Scene::Active ? Scene::Active->GetSceneManager() : nullptr;
				if (!sm)
				{
					ImGui::TextDisabled("(no active scene)");
				}
				else if (auto tree = std::dynamic_pointer_cast<OcTree>(sm))
				{
					if (!tree->IsRooted())
					{
						ImGui::Text("Root: (unrooted)");
						ImGui::Text("Scene Nodes: 0  Tree Nodes: 0  Max Depth: 0");
					}
					else
					{
						BoxBounds aabb = tree->GetRootAABB();
						Vector4 mn = aabb.GetMin();
						Vector4 mx = aabb.GetMax();
						ImGui::Text("Root Min: (%.1f, %.1f, %.1f)", mn.x, mn.y, mn.z);
						ImGui::Text("Root Max: (%.1f, %.1f, %.1f)", mx.x, mx.y, mx.z);
						ImGui::Text("Scene Nodes: %u  Tree Nodes: %u  Max Depth: %u",
							tree->GetTotalSceneNodeCount(),
							tree->GetOccupiedNodeCount(),
							tree->GetMaxOccupiedDepth());
					}

					static bool draw_octree_bounds = false;
					if (ImGui::Checkbox("Draw OcTree Bounds", &draw_octree_bounds))
					{
						if (Pipeline::Active)
							Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, draw_octree_bounds);
					}
					else if (Pipeline::Active)
					{
						// Keep pipeline state in sync if something else flipped it.
						Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, draw_octree_bounds);
					}
				}
				else
				{
					ImGui::TextDisabled("(non-octree scene manager)");
				}
			}

			void RenderProfilerGBufferTab()
			{
				if (!Pipeline::Active)
				{
					ImGui::TextDisabled("(no active pipeline)");
					return;
				}

				// GBuffer textures match the rendered framebuffer aspect.
				// Sizing must preserve that aspect; using the Profiler
				// window's own size produces tall-narrow distortions when
				// the window is docked on the side.
				const float content_w = ImGui::GetContentRegionAvail().x;
				float img_w = content_w > 0.0f ? content_w : 256.0f;
				if (img_w > 320.0f) img_w = 320.0f;

				auto show = [&](const char* label, const char* tex_name) {
					ImGui::Text("%s", label);
					if (auto t = Pipeline::Active->GetTextureByName(tex_name))
					{
						const float tex_w = (float)t->GetWidth();
						const float tex_h = (float)t->GetHeight();
						const float aspect = (tex_w > 0.0f && tex_h > 0.0f)
							? tex_h / tex_w : 0.5625f;  // 16:9 fallback
						ImGui::Image((ImTextureID)(intptr_t)t->GetID(),
							ImVec2(img_w, img_w * aspect),
							ImVec2(0, 1), ImVec2(1, 0));
					}
				};
				show("Depth Buffer:",    "gbuffer_depth");
				show("Normal Buffer:",   "gbuffer_normal");
				show("Diffuse Buffer:",  "gbuffer_diffuse");
				show("Light Buffer:",    "gbuffer_light");
			}

			void RenderProfilerShadowsTab()
			{
				if (!Pipeline::Active)
				{
					ImGui::TextDisabled("(no active pipeline)");
					return;
				}

				const float scale = 1.0f;

				if (auto ptr = Pipeline::Active->GetEntityManager()->Get<Texture>("1024*1024*0*depth24*2d"))
				{
					ImGui::Text("2D Shadow Map: ");
					ImGui::Image((ImTextureID)(intptr_t)ptr->GetID(),
						ImVec2(256 * scale, 256 * scale), ImVec2(0, 1), ImVec2(1, 0));
				}

				// Cube shadow map: blit each face into a temporary 2D texture
				// using a one-time-compiled blitter, then preview the six 2Ds.
				if (auto ptr = Pipeline::Active->GetEntityManager()->Get<Texture>("512*512*0*depth24*cube"))
				{
					static auto img0 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img1 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img2 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img3 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img4 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img5 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto blitShader = Shader::Create("EditorBlitCubeShader", ShaderType::OTHER);

					if (blitShader->GetDirty())
					{
						const char *blit_vs =
							"in vec3 vertex_position;"
							"out vec2 out_uv;"
							"void main()"
							"{"
							"	out_uv = vertex_position.xy * 0.5 + 0.5;"
							"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);"
							"}";
						const char *blit_fs =
							"uniform samplerCube src;"
							"uniform mat4 matrix;"
							"in vec2 out_uv;"
							"out vec4 fragment_output;"
							"void main()"
							"{"
							"   vec4 dir = matrix * vec4(out_uv.x, 1.0 - out_uv.y, 1.0, 1.0);"
							"	fragment_output = texture(src, dir.xyz);"
							"}";
						blitShader->Compile(blit_vs, blit_fs, "");
					}

					std::array<Matrix4, 6> dirMatrices;
					Vector4 lightPos(0, 0, 0, 1);
					dirMatrices[0].LookAt(lightPos, lightPos + Vector4(1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
					dirMatrices[1].LookAt(lightPos, lightPos + Vector4(-1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
					dirMatrices[2].LookAt(lightPos, lightPos + Vector4(0.0f, 1.0f, 0.0f), Vector4(0.0f, 0.0f, 1.0f));
					dirMatrices[3].LookAt(lightPos, lightPos + Vector4(0.0f, -1.0f, 0.0f), Vector4(0.0f, 0.0f, -1.0f));
					dirMatrices[4].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, 1.0f), Vector4(0.0f, -1.0f, 0.0f));
					dirMatrices[5].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, -1.0f), Vector4(0.0f, -1.0f, 0.0f));

					auto render = RenderUtil::Instance();
					std::shared_ptr<Texture> faces[6] = {img0, img1, img2, img3, img4, img5};
					for (int i = 0; i < 6; ++i)
					{
						blitShader->Bind();
						blitShader->BindMatrix("matrix", dirMatrices[i]);
						render->Blit(ptr, faces[i], blitShader);
					}

					ImGui::Text("Cube Shadow Map: ");
					ImGui::BeginGroup();
					for (int row = 0; row < 3; ++row)
					{
						ImGui::Image((ImTextureID)(intptr_t)faces[row * 2]->GetID(),
							ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
						ImGui::SameLine(140);
						ImGui::Image((ImTextureID)(intptr_t)faces[row * 2 + 1]->GetID(),
							ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
					}
					ImGui::EndGroup();
				}

				if (auto ptr = Pipeline::Active->GetEntityManager()->Get<Texture>("1024*1024*4*depth24*2d_array"))
				{
					static auto img0 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img1 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img2 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto img3 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
					static auto blitShader = Shader::Create("EditorBlitArrayShader", ShaderType::OTHER);

					if (blitShader->GetDirty())
					{
						const char *blit_vs =
							"in vec3 vertex_position;"
							"out vec2 out_uv;"
							"void main()"
							"{"
							"	out_uv = vertex_position.xy * 0.5 + 0.5;"
							"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);"
							"}";
						const char *blit_fs =
							"uniform sampler2DArray src;"
							"uniform float index;"
							"in vec2 out_uv;"
							"out vec4 fragment_output;"
							"void main()"
							"{"
							"	fragment_output = texture(src, vec3(out_uv, index));"
							"}";
						blitShader->Compile(blit_vs, blit_fs, "");
					}

					auto render = RenderUtil::Instance();
					std::shared_ptr<Texture> slices[4] = {img0, img1, img2, img3};
					for (int i = 0; i < 4; ++i)
					{
						blitShader->Bind();
						blitShader->BindFloat("index", (float)i);
						render->Blit(ptr, slices[i], blitShader);
					}

					ImGui::Text("Cascaded Shadow Map: ");
					ImGui::BeginGroup();
					for (int row = 0; row < 2; ++row)
					{
						ImGui::Image((ImTextureID)(intptr_t)slices[row * 2]->GetID(),
							ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
						ImGui::SameLine(140);
						ImGui::Image((ImTextureID)(intptr_t)slices[row * 2 + 1]->GetID(),
							ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
					}
					ImGui::EndGroup();
				}
			}
		}

		void RenderProfilerWindow(bool* open)
		{
			ImGui::SetNextWindowSize(ImVec2(420, 480), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Profiler", open))
			{
				ImGui::End();
				return;
			}

			if (ImGui::BeginTabBar("ProfilerTabs"))
			{
				if (ImGui::BeginTabItem("FPS"))     { RenderProfilerFpsTab();     ImGui::EndTabItem(); }
				if (ImGui::BeginTabItem("GBuffer")) { RenderProfilerGBufferTab(); ImGui::EndTabItem(); }
				if (ImGui::BeginTabItem("Shadows")) { RenderProfilerShadowsTab(); ImGui::EndTabItem(); }
				ImGui::EndTabBar();
			}
			ImGui::End();
		}

		// ----------------------------------------------------------------
		// Scene Inspector
		// ----------------------------------------------------------------
		namespace
		{
			// Per-row rename state.
			struct RenameState
			{
				bool active = false;
				bool needsFocus = false;
				char buffer[256] = {};
				SceneNode* target = nullptr;
			};
			static std::unordered_map<SceneNode*, RenameState> g_RenameStates;

			// Hover dwell times for collapsed rows during a drag.
			static std::unordered_map<SceneNode*, double> g_HoverExpandStart;
			static constexpr double kHoverExpandDelay = 0.5;

			// Deferred drag/drop reparenting. Mutations run at the end of the frame so ImGui's tree state stays balanced.
			struct PendingReparent { SceneNode* source; SceneNode* target; };
			static std::vector<PendingReparent> g_PendingReparents;

			// Deferred add/delete, same reasoning as the reparent queue.
			struct PendingAdd    { SceneNode::Ptr parent; SceneNode::Ptr child; };
			struct PendingDelete { SceneNode* target; };
			static std::vector<PendingAdd>    g_PendingAdds;
			static std::vector<PendingDelete> g_PendingDeletes;

			// ImGui drag-drop payload key. Pointer-sized payloads are safe
			// because both source and target live in the same process.
			static constexpr const char* kSceneNodeDragPayload = "FURY_SCENE_NODE";

			// True iff target is reachable from ancestor.
			bool IsDescendantOf(SceneNode* ancestor, SceneNode* target)
			{
				if (!ancestor || !target) return false;
				for (unsigned int i = 0; i < ancestor->GetChildCount(); ++i)
				{
					auto child = ancestor->GetChildAt(i);
					if (child.get() == target) return true;
					if (IsDescendantOf(child.get(), target)) return true;
				}
				return false;
			}

			// Adjust node's local TRS so the world transform stays the same after reparenting. Call BEFORE RemoveChild/AddChild.
			void PreserveWorldTransformOnReparent(SceneNode* node, SceneNode* newParent)
			{
				if (!node || !newParent) return;
				Vector4 worldPos = node->GetWorldPosition();
				Quaternion worldRot = node->GetWorldRoattion();
				Vector4 worldScl = node->GetWorldScale();

				// newLocalPos = newParentWorldMatrix^-1 * worldPos
				Matrix4 invParent = newParent->GetWorldMatrix().Inverse();
				Vector4 newLocalPos = invParent.Multiply(worldPos);

				// newLocalRot = newParentWorldRot^-1 * worldRot
				Quaternion parentRot = newParent->GetWorldRoattion();
				Quaternion newLocalRot = parentRot.Conjugate() * worldRot;

				// newLocalScl = worldScl / newParentScl (component-wise)
				Vector4 parentScl = newParent->GetWorldScale();
				Vector4 newLocalScl = worldScl;
				auto safeDiv = [](float a, float b) -> float {
					return std::abs(b) > 1e-6f ? a / b : a;
				};
				newLocalScl.x = safeDiv(worldScl.x, parentScl.x);
				newLocalScl.y = safeDiv(worldScl.y, parentScl.y);
				newLocalScl.z = safeDiv(worldScl.z, parentScl.z);

				node->SetLocalPosition(newLocalPos);
				node->SetLocalRoattion(newLocalRot);
				node->SetLocalScale(newLocalScl);
			}

			// Pick a sibling-unique name under `parent`. "Node", "Node (1)",
			// "Node (2)", … Mirrors the engine's lack of unique-name enforcement
			// (siblings may legitimately share a name) — we just want
			// inspector-generated names to never collide.
			std::string UniqueChildName(SceneNode* parent, const std::string& base)
			{
				if (!parent || !parent->FindChild(base)) return base;
				for (int i = 1; i < 100000; ++i)
				{
					std::string name = base + " (" + std::to_string(i) + ")";
					if (!parent->FindChild(name)) return name;
				}
				return base + " (?)";
			}

			// Action: create a fresh child under `target` and select it.
			void DoAddChild(SceneNode* target)
			{
				if (!target) return;
				auto name = UniqueChildName(target, "Node");
				auto child = SceneNode::Create(name);

				g_PendingAdds.push_back({ target->shared_from_this(), child });
				SetSelectedSceneNode(child.get());
				Editor::MarkSceneDirty();
			}

			// Action: deep-clone `target` (including descendants) under the
			// same parent, with a "(copy)" / "(copy N)" suffix. The clone
			// becomes the new selection. The root node is not duplicable.
			void DoDuplicate(SceneNode* target)
			{
				if (!target) return;
				auto parent = target->GetParent();
				if (!parent) return; // root — caller filters the menu
				std::string baseName = target->GetName();
				if (baseName.empty()) baseName = "Node";
				std::string copyName = baseName + " (copy)";
				if (parent->FindChild(copyName))
				{
					for (int i = 1; i < 100000; ++i)
					{
						copyName = baseName + " (copy " + std::to_string(i) + ")";
						if (!parent->FindChild(copyName)) break;
					}
				}
				auto clone = target->CloneTree(copyName);
				// Defer parent attachment + SceneManager re-registration.
				g_PendingAdds.push_back({ parent, clone });
				SetSelectedSceneNode(clone.get());
				Editor::MarkSceneDirty();
			}

			// Action: detach `target` from its parent. If the editor's
			// selection pointed at the deleted node, clear it.
			void DoDelete(SceneNode* target)
			{
				if (!target) return;
				if (!target->GetParent()) return; // root — caller filters the menu
				if (g_SelectedSceneNode == target)
					SetSelectedSceneNode(nullptr);
				g_RenameStates.erase(target);
				g_PendingDeletes.push_back({ target });
				Editor::MarkSceneDirty();
			}

			// Action: start in-place rename of `target`. Cancels any other
			// in-flight rename so only one field is active at a time.
			void DoRenameActivate(SceneNode* target)
			{
				if (!target) return;
				for (auto& pair : g_RenameStates) pair.second.active = false;
				auto& state = g_RenameStates[target];
				state.active = true;
				state.needsFocus = true;
				state.target = target;
				std::string name = target->GetName();
				std::strncpy(state.buffer, name.c_str(), sizeof(state.buffer) - 1);
				state.buffer[sizeof(state.buffer) - 1] = '\0';
				Editor::MarkSceneDirty();
			}

			// Per-row input handling that does NOT depend on the row's tree
			// structure: the right-click context menu and drag-drop. The
			// actual TreeNodeEx lives in the caller so it can also recurse
			// into children when the node is open.
			void HandleRowInteractions(SceneNode* node, int depth, bool isOpen)
			{
				if (!node) return;
				const bool isRoot = (depth == 0);
				const std::string popupId = "NodeMenu##" + std::to_string(reinterpret_cast<uintptr_t>(node));

				// --- Right-click context menu ------------------------------
				if (ImGui::BeginPopupContextItem(popupId.c_str()))
				{
					if (ImGui::MenuItem("Add Child")) DoAddChild(node);
					if (!isRoot)
					{
						ImGui::Separator();
						if (ImGui::MenuItem("Duplicate")) DoDuplicate(node);
						if (ImGui::MenuItem("Rename"))   DoRenameActivate(node);
						if (ImGui::MenuItem("Delete"))   DoDelete(node);
					}
					ImGui::EndPopup();
				}

				// --- Drag source --------------------------------------------
				// Disabled for the root: the spec only allows reparenting
				// across parents, never "out of" the root.
				if (!isRoot && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
				{
					SceneNode* payload = node;
					ImGui::SetDragDropPayload(kSceneNodeDragPayload, &payload, sizeof(payload));
					ImGui::Text("%s", node->GetName().empty() ? "(unnamed)" : node->GetName().c_str());
					ImGui::EndDragDropSource();
				}

				// --- Drop target --------------------------------------------
				// Queue the reparent; the actual mutation runs at the end of
				// the frame (see RenderSceneInspectorWindow's tail) so we don't
				// mutate the scene graph while ImGui is mid-tree.
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneNodeDragPayload))
					{
						IM_ASSERT(payload->DataSize == sizeof(SceneNode*));
						SceneNode* dragged = *(SceneNode**)payload->Data;
						if (dragged && dragged != node && !IsDescendantOf(dragged, node))
						{
							g_PendingReparents.push_back({ dragged, node });
						}
					}
					ImGui::EndDragDropTarget();
				}

				// --- Hover-to-expand during drag ---------------------------
				// Track first-hover time on a collapsed row; expand the row
				// once the dwell exceeds the threshold.
				if (ImGui::IsDragDropActive() && ImGui::IsItemHovered() && !isOpen && node->GetChildCount() > 0)
				{
					double now = ImGui::GetTime();
					auto it = g_HoverExpandStart.find(node);
					if (it == g_HoverExpandStart.end())
						g_HoverExpandStart[node] = now;
					else if (now - it->second > kHoverExpandDelay)
					{
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);
						g_HoverExpandStart.erase(it);
					}
				}
				else
				{
					// Cursor left the row — reset the dwell timer.
					g_HoverExpandStart.erase(node);
				}
			}

			// Shared row renderer used by both the Scene::Active path and the
			// Lua-provided TreeNode path. `node` may be nullptr for synthetic
			// rows supplied by Lua (read-only display only — interactions
			// are skipped when node is null).
			void RenderNodeRow(SceneNode* node, const std::string& displayName, int depth, bool* outOpen)
			{
				const bool isRoot = (depth == 0);
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
					| ImGuiTreeNodeFlags_OpenOnDoubleClick;
				if (isRoot) flags |= ImGuiTreeNodeFlags_DefaultOpen;
				if (node && g_SelectedSceneNode == node)
					flags |= ImGuiTreeNodeFlags_Selected;
				bool isLeaf = true;
				if (node) isLeaf = (node->GetChildCount() == 0);
				else      isLeaf = false; // unknown child count for synthetic rows
				if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

				bool open = false;
				auto rit = node ? g_RenameStates.find(node) : g_RenameStates.end();
				if (rit != g_RenameStates.end() && rit->second.active)
				{
					// Render an empty tree node + InputText on the same line.
					// The Leaf + NoTreePushOnOpen flags prevent the tree node
					// from opening/closing or pushing onto the ID stack — we
					// also force `open=false` so the caller's TreePop() is
					// skipped (ImGui asserts if TreePop has no matching
					// push). The user can keep editing children visually
					// collapsed while the rename field is active.
					flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
					ImGui::TreeNodeEx((void*)node, flags, "");
					open = false;
					ImGui::SameLine();
					ImGui::PushItemWidth(200);
					bool committed = ImGui::InputText("##rename", rit->second.buffer,
						sizeof(rit->second.buffer),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
					if (rit->second.needsFocus)
					{
						ImGui::SetKeyboardFocusHere(-1);
						rit->second.needsFocus = false;
					}
					bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
					bool deactivated = ImGui::IsItemDeactivated();
					if (committed || (deactivated && !cancelled))
					{
						if (node->GetName() != rit->second.buffer)
						{
							node->SetName(rit->second.buffer);
							Editor::MarkSceneDirty();
						}
						rit->second.active = false;
					}
					else if (cancelled)
					{
						rit->second.active = false;
					}
					ImGui::PopItemWidth();
				}
				else
				{
					open = ImGui::TreeNodeEx((void*)node, flags, "%s",
						displayName.empty() ? "(unnamed)" : displayName.c_str());
				}

				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				{
					if (node) SetSelectedSceneNode(node);
				}

				// Double-click on the label (not the arrow) starts rename.
				if (node && !isRoot && ImGui::IsItemHovered()
					&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					DoRenameActivate(node);
				}

				// Only the C++ scene-graph path exposes mutations: synthetic
				// Lua-owned rows are read-only.
				if (node) HandleRowInteractions(node, depth, open);

				if (outOpen) *outOpen = open;
			}

			void RenderSceneNodeRecursive(const std::shared_ptr<SceneNode>& node, int depth)
			{
				if (!node) return;
				bool open = false;
				RenderNodeRow(node.get(), node->GetName(), depth, &open);
				if (open && node->GetChildCount() > 0)
				{
					for (unsigned int i = 0; i < node->GetChildCount(); ++i)
						RenderSceneNodeRecursive(node->GetChildAt(i), depth + 1);
					ImGui::TreePop();
				}
			}

			void RenderTreeFromProvider(const TreeNode& tn, int depth)
			{
				bool open = false;
				RenderNodeRow(tn.node, tn.name, depth, &open);
				if (open && !tn.children.empty())
				{
					for (auto& c : tn.children)
						RenderTreeFromProvider(c, depth + 1);
					ImGui::TreePop();
				}
			}
		}

		void RenderSceneInspectorWindow(bool* open)
		{
			ImGui::SetNextWindowSize(ImVec2(280, 480), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Scene Inspector", open))
			{
				ImGui::End();
				return;
			}

			// F2 activates rename on the currently selected node. We only
			// trigger if the inspector window itself has focus — otherwise
			// F2 in the viewport or another panel could surprise the user.
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
				&& ImGui::IsKeyPressed(ImGuiKey_F2)
				&& g_SelectedSceneNode
				&& g_SelectedSceneNode->GetParent() != nullptr)
			{
				DoRenameActivate(g_SelectedSceneNode);
			}

			// Clear hover-expand bookkeeping whenever no drag is in flight
			// so a fresh drag doesn't inherit stale timers.
			if (!ImGui::IsDragDropActive())
				g_HoverExpandStart.clear();

			if (g_TreeProvider)
			{
				try
				{
					TreeNode tree = g_TreeProvider();
					RenderTreeFromProvider(tree, 0);
				}
				catch (...) {}
			}
			else if (Scene::Active)
			{
				auto root = Scene::Active->GetRootNode();
				RenderSceneNodeRecursive(root, 0);
			}
			else
			{
				ImGui::TextDisabled("(no active scene)");
			}

			// Empty-space drop target: dropping on the trailing blank area
			// reparents the dragged node to the root. A drop on the title
			// bar / window chrome is ignored because that area isn't part of
			// the inspector's draw list.
			SceneNode* root = Scene::Active ? Scene::Active->GetRootNode().get() : nullptr;
			ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneNodeDragPayload))
				{
					IM_ASSERT(payload->DataSize == sizeof(SceneNode*));
					SceneNode* dragged = *(SceneNode**)payload->Data;
					if (dragged && root && dragged != root && !IsDescendantOf(dragged, root))
					{
						g_PendingReparents.push_back({ dragged, root });
					}
				}
				ImGui::EndDragDropTarget();
			}

			// Apply deferred mutations. Re-register with SceneManager so the
		// renderer sees the new state.
			for (auto& pr : g_PendingReparents)
			{
				if (!pr.source || !pr.target) continue;
				PreserveWorldTransformOnReparent(pr.source, pr.target);
				auto sp = pr.source->shared_from_this();
				auto srcParent = pr.source->GetParent();
				if (srcParent) srcParent->RemoveChild(sp);
				pr.target->AddChild(sp);
				if (Scene::Active)
				{
					Scene::Active->GetSceneManager()->AddSceneNodeRecursively(sp);
				}
			}
			for (auto& pa : g_PendingAdds)
			{
				if (!pa.parent || !pa.child) continue;
				pa.parent->AddChild(pa.child);
				if (Scene::Active)
				{
					Scene::Active->GetSceneManager()->AddSceneNodeRecursively(pa.child);
				}
			}
			for (auto& pd : g_PendingDeletes)
			{
				if (!pd.target) continue;
				auto sp = pd.target->shared_from_this();
				if (Scene::Active)
				{
					Scene::Active->GetSceneManager()->RemoveSceneNode(sp);
				}
				pd.target->RemoveFromParent();
			}
			if (!g_PendingReparents.empty() ||
				!g_PendingAdds.empty() ||
				!g_PendingDeletes.empty())
			{
				Editor::MarkSceneDirty();
			}
			g_PendingReparents.clear();
			g_PendingAdds.clear();
			g_PendingDeletes.clear();

			ImGui::End();
		}
		// ----------------------------------------------------------------
		// Console
		// ----------------------------------------------------------------
		namespace
		{
			ImVec4 ColorForLevel(LogLevel l)
			{
				switch (l)
				{
					case LogLevel::Debug:    return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
					case LogLevel::Info:     return ImVec4(0.5f, 0.85f, 1.0f, 1.0f);
					case LogLevel::Warn:     return ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
					case LogLevel::Error:    return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
					case LogLevel::Critical: return ImVec4(0.8f, 0.1f, 0.1f, 1.0f);
				}
				return ImVec4(1, 1, 1, 1);
			}
		}

		void RenderConsoleWindow(bool* open)
		{
			ImGui::SetNextWindowSize(ImVec2(640, 320), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Console", open))
			{
				ImGui::End();
				return;
			}

			if (ImGui::Button("Clear")) GlobalLogBuffer().Clear();

			const float footer_h = ImGui::GetFrameHeightWithSpacing();
			ImGui::BeginChild("##log", ImVec2(0, -footer_h), true,
				ImGuiWindowFlags_HorizontalScrollbar);

			auto entries = GlobalLogBuffer().Snapshot();
			for (const auto& e : entries)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ColorForLevel(e.level));
				ImGui::TextUnformatted(e.text.c_str());
				ImGui::PopStyleColor();
			}

			// Auto-scroll only when the user is already at the bottom — this
			// preserves manual scrollback behavior.
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
				ImGui::SetScrollHereY(1.0f);

			ImGui::EndChild();

			static char buf[512] = {};
			ImGui::PushItemWidth(-FLT_MIN);
			bool reclaim_focus = false;
			if (ImGui::InputText("##cmd", buf, sizeof(buf),
				ImGuiInputTextFlags_EnterReturnsTrue))
			{
				if (buf[0] != '\0')
				{
					std::string line = buf;
					GlobalLogBuffer().Push(LogLevel::Info, std::string("> ") + line);
					if (g_CommandHandler)
					{
						try { g_CommandHandler(line); } catch (...) {}
					}
				}
				buf[0] = '\0';
				reclaim_focus = true;
			}
			ImGui::PopItemWidth();
			ImGui::SetItemDefaultFocus();
			if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);

			ImGui::End();
		}

		// ----------------------------------------------------------------
		// Content Browser
		// ----------------------------------------------------------------
		namespace
		{
			static std::string g_SelectedFile;

			char IconForExt(const std::string& ext_lower)
			{
				if (ext_lower == ".json") return 'J';
				if (ext_lower == ".bin")  return 'B';
				if (ext_lower == ".gltf" || ext_lower == ".glb") return 'G';
				if (ext_lower == ".fbx")  return 'F';
				return '?';
			}
		}

		void RenderContentBrowserWindow(bool* open)
		{
			ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Content Browser", open))
			{
				ImGui::End();
				return;
			}

			std::string dir = GetSceneDir();
			ImGui::TextDisabled("%s", dir.c_str());
			ImGui::Separator();

			std::error_code ec;
			if (!std::filesystem::exists(dir, ec) || ec)
			{
				ImGui::TextDisabled("(directory missing)");
				ImGui::End();
				return;
			}

			for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
			{
				if (!entry.is_regular_file(ec)) continue;
				std::string name = entry.path().filename().string();
				if (name.empty() || name[0] == '.') continue;

				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				char label[300];
				std::snprintf(label, sizeof(label), "[%c] %s",
					IconForExt(ext), name.c_str());

				bool selected = (g_SelectedFile == name);
				if (ImGui::Selectable(label, selected))
				{
					g_SelectedFile = name;
				}
			}

			ImGui::End();
		}

		// ----------------------------------------------------------------
		// Viewport window — docks the 3D scene into the editor dockspace.
		// The scene renders to an offscreen RenderTarget (sized to the
		// window's content rect) and is presented via ImGui::Image. The
		// captured content rect (g_ViewportContentMin/Size) is what the
		// gizmo and picking use for viewport-space coordinates.
		// ----------------------------------------------------------------
		void RenderViewportWindow(bool* open)
		{
			// Function-local static: the RT persists across frames and is
			// resized as the window resizes. Owned by this TU.
			static RenderTarget::Ptr rt;
			if (!rt) rt = RenderTarget::Create("EditorViewport");

		ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
		// NoNavFocus: clicking the viewport must NOT steal keyboard focus,
		// otherwise WantCaptureKeyboard goes true and the WASD camera-move
		// (gated on `not WantCaptureKeyboard` in Editor.lua) stops working
		// while the user drag-rotates over the viewport.
		if (!ImGui::Begin("Viewport", open,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus))
			{
				// Collapsed: no content rect. Mark not visible so gizmo +
				// picking no-op, and clear the pipeline RT so the scene
				// doesn't render into a stale target.
				g_ViewportVisible = false;
				g_ViewportContentSize = ImVec2(0, 0);
				if (Pipeline::Active)
					Pipeline::Active->SetRenderTarget(nullptr);
				ImGui::End();
				return;
			}

			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const ImVec2 pos = ImGui::GetCursorScreenPos();

			// Capture the content rect for the gizmo + picking regardless
			// of whether we have a usable size this frame.
			g_ViewportContentMin = pos;
			g_ViewportContentSize = avail;
			g_ViewportHovered = ImGui::IsWindowHovered();

			if (avail.x > 0.0f && avail.y > 0.0f && Pipeline::Active)
			{
				const int w = static_cast<int>(avail.x);
				const int h = static_cast<int>(avail.y);

				// Resize the RT to the content rect (no-op if unchanged).
				// If the RT fails to allocate (e.g. FBO incomplete), skip
				// the image this frame rather than crash on a null texture.
				if (!rt->Resize(w, h))
				{
					g_ViewportVisible = false;
					if (Pipeline::Active)
						Pipeline::Active->SetRenderTarget(nullptr);
					ImGui::End();
					return;
				}

				// Hand the RT to the pipeline so the next Execute renders
				// the 3D scene into it (see Pass::Bind's RT override).
				Pipeline::Active->SetRenderTarget(rt.get());

				// Re-derive the camera aspect from the content rect so the
				// scene isn't stretched when the window is resized. FOV /
				// near / far are preserved. SetAspect (unlike PerspectiveFov)
				// preserves the frustum's world transform so culling keeps
				// working — the camera node owns that transform.
				if (auto camNode = Pipeline::Active->GetCurrentCamera())
				{
					if (auto cam = camNode->GetComponent<Camera>())
					{
						const float aspect = static_cast<float>(w) / static_cast<float>(h);
						cam->SetAspect(aspect);
					}
				}

				g_ViewportVisible = true;

				// Present the RT's color texture. The texture is sampled
				// during Gui::Render (after Pipeline::Execute has written
				// this frame's scene into it), so there's no one-frame lag.
				ImTextureID tex = (ImTextureID)(intptr_t)rt->GetColorTexture()->GetID();
				ImGui::Image(tex, avail, ImVec2(0, 1), ImVec2(1, 0));

				// Render the TRS gizmo into the Viewport window's own draw
				// list, on top of the image. RenderGizmo uses
				// ImGui::GetWindowDrawList() (this window) so the gizmo
				// composites over the viewport rather than being hidden
				// behind the docked window.
				RenderGizmo(pos, avail);
			}
			else
			{
				g_ViewportVisible = false;
				if (Pipeline::Active)
					Pipeline::Active->SetRenderTarget(nullptr);
			}

			ImGui::End();
		}

		// Public wrappers for the Edit menu (called from Editor.cpp).

		void DeleteSelectedSceneNode()
		{
			if (g_SelectedSceneNode) DoDelete(g_SelectedSceneNode);
		}

		void DuplicateSelectedSceneNode()
		{
			if (g_SelectedSceneNode) DoDuplicate(g_SelectedSceneNode);
		}

		void AddChildToSelectedSceneNode()
		{
			if (g_SelectedSceneNode) DoAddChild(g_SelectedSceneNode);
		}
	}
}

#endif // WITH_EDITOR
