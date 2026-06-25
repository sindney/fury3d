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
#include "Fury/EntityManager.h"
#include "Fury/Matrix4.h"
#include "Fury/Pipeline.h"
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
			void RenderSceneNodeRecursive(const std::shared_ptr<SceneNode>& node, int depth)
			{
				if (!node) return;
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
					| ImGuiTreeNodeFlags_OpenOnDoubleClick;
				if (depth == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
				if (g_SelectedSceneNode == node.get())
					flags |= ImGuiTreeNodeFlags_Selected;
				if (node->GetChildCount() == 0)
					flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

				const std::string name = node->GetName();
				bool open = ImGui::TreeNodeEx((void*)node.get(), flags, "%s",
					name.empty() ? "(unnamed)" : name.c_str());

				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
					g_SelectedSceneNode = node.get();

				if (open && node->GetChildCount() > 0)
				{
					for (unsigned int i = 0; i < node->GetChildCount(); ++i)
					{
						RenderSceneNodeRecursive(node->GetChildAt(i), depth + 1);
					}
					ImGui::TreePop();
				}
			}

			void RenderTreeFromProvider(const TreeNode& tn, int depth)
			{
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
					| ImGuiTreeNodeFlags_OpenOnDoubleClick;
				if (depth == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
				if (g_SelectedSceneNode == tn.node && tn.node != nullptr)
					flags |= ImGuiTreeNodeFlags_Selected;
				if (tn.children.empty())
					flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

				bool open = ImGui::TreeNodeEx((const void*)&tn, flags, "%s",
					tn.name.empty() ? "(unnamed)" : tn.name.c_str());

				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
					g_SelectedSceneNode = tn.node;

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
	}
}

#endif // WITH_EDITOR
