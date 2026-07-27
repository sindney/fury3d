#ifdef WITH_EDITOR

#include "Fury/BufferManager.h"
#include "Fury/AnimationClip.h"
#include "Fury/Camera.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorAssetPicker.h"
#include "Fury/Editor/EditorAssetWindows.h"
#include "Fury/Editor/EditorAnimationWindow.h"
#include "Fury/Editor/EditorConfirmDialog.h"
#include "Fury/Editor/EditorDebug.h"
#include "Fury/Editor/EditorLog.h"
#include "Fury/Editor/EditorThemes.h"
#include "Fury/EntityManager.h"
#include "Fury/Light.h"
#include "Fury/EntityUtil.h"
#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Pipeline.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/RenderSettings.h"
#include "Fury/SceneManager.h"
#include "Fury/RenderTarget.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"
#include "Fury/Vector4.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGuizmo.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fury {
namespace Editor {
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

// Gizmo state lives in EditorGizmo.cpp; the viewport toolbar
// edits it directly (moved out of the Node Properties window).
extern ImGuizmo::OPERATION g_GizmoOp;
extern ImGuizmo::MODE g_GizmoSpace;
extern bool g_SnapEnabled;
extern float g_SnapTranslate;
extern float g_SnapRotate;
extern float g_SnapScale;

// Reference-grid toggle state. Editor-owned (persisted to imgui.ini
// via the FuryEditor settings handler in Editor.cpp), default ON;
// applied to the pipeline switch per frame from the viewport
// toolbar so pipeline recreation (scene reload) keeps the choice.
bool g_ShowGrid = true;

// ----------------------------------------------------------------
// Settings window (Camera / Import / Themes)
// ----------------------------------------------------------------
void RenderSettingsWindow(bool* open) {
	ImGui::SetNextWindowSize(ImVec2(360, 360), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Settings", open)) {
		ImGui::End();
		return;
	}

	// --- Editor (grid + camera + theme) ---------------------------
	if (ImGui::CollapsingHeader("Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();

		// Reference grid — persisted editor state (see g_ShowGrid);
		// the viewport toolbar's Grid checkbox binds the same global.
		if (ImGui::Checkbox("Show Grid", &g_ShowGrid)) {
			ImGui::MarkIniSettingsDirty();
		}

		// Snap step sizes (the viewport toolbar carries the Snap
		// toggle itself).
		ImGui::TextDisabled("Snap Steps");
		bool snap_changed = false;
		if (ImGui::DragFloat("Translate Step", &g_SnapTranslate, 0.1f, 0.001f, 1000.0f, "%.3f")) snap_changed = true;
		if (ImGui::DragFloat("Rotate Step", &g_SnapRotate, 0.5f, 0.1f, 180.0f, "%.1f deg")) snap_changed = true;
		if (ImGui::DragFloat("Scale Step", &g_SnapScale, 0.01f, 0.001f, 100.0f, "%.3f")) snap_changed = true;
		if (snap_changed) ImGui::MarkIniSettingsDirty();

		ImGui::Spacing();

		ImGui::TextDisabled("Camera");
		if (g_CameraControls.empty()) {
			ImGui::TextDisabled("(no camera settings registered)");
		} else {
			for (auto& c : g_CameraControls) {
				if (c.kind == "checkbox") {
					bool v = c.get_b ? c.get_b() : false;
					if (ImGui::Checkbox(c.label.c_str(), &v)) {
						if (c.set_b) try {
								c.set_b(v);
							} catch (...) {}
					}
				} else {
					float v = c.get_f ? c.get_f() : 0.0f;
					if (ImGui::SliderFloat(c.label.c_str(), &v, c.vmin, c.vmax)) {
						if (c.set_f) try {
								c.set_f(v);
							} catch (...) {}
					}
				}
			}
		}
		ImGui::Spacing();

		ImGui::TextDisabled("Theme");
		int current = GetCurrentThemeIndex();
		if (ImGui::BeginCombo("Theme", kThemes[current].display_name)) {
			for (std::size_t i = 0; i < kThemesCount; ++i) {
				bool is_selected = (current == (int)i);
				if (ImGui::Selectable(kThemes[i].display_name, is_selected)) {
					ApplyTheme(static_cast<ETheme>(i));
					ImGui::MarkIniSettingsDirty();
				}
				if (is_selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	// --- Import ---------------------------------------------------
	if (ImGui::CollapsingHeader("Import")) {
		bool v = GetImportFlag("auto_default_sun", true);
		if (ImGui::Checkbox("Auto-Add Default Sun", &v)) {
			SetImportFlag("auto_default_sun", v);
		}
		bool a = GetImportFlag("auto_scale_detect", true);
		if (ImGui::Checkbox("Auto-Scale Detection", &a)) {
			SetImportFlag("auto_scale_detect", a);
		}
		// Normal generation for NORMAL-less primitives (GltfImporter).
		int ng = GetImportFlag("normals_smooth", true) ? 0 : 1;
		ImGui::SetNextItemWidth(170.0f);
		if (ImGui::Combo("Normal Gen", &ng, "Smooth (default)\0Flat\0")) {
			SetImportFlag("normals_smooth", ng == 0);
		}
		// Weld/dedup coincident vertices on import (MeshUtil::OptimizeMesh).
		bool om = GetImportFlag("optimize_mesh", true);
		if (ImGui::Checkbox("Optimize Mesh (Weld)", &om)) {
			SetImportFlag("optimize_mesh", om);
		}
	}

	// --- Engine (read-only unit/coord info + CSM toggle) ---------
	if (ImGui::CollapsingHeader("Engine")) {
		// Read-only reference info — see Camera.h / docs/ARCHITECTURE.md §5.1.
		ImGui::TextDisabled("Unit:   1 unit = 1 cm");
		ImGui::TextDisabled("Coords: right-handed, +Y up, -Z front");
		ImGui::Spacing();

		// HDR / LDR + CSM + postprocess chain: these are all fields
		// of the active scene's `renderSettings` block (see Scene.h
		// / RenderSettings.h). When no scene is active the panel
		// stays read-only and shows "(no active pipeline)".
		auto scene = Scene::Active;
		auto settings = scene ? scene->GetRenderSettings() : nullptr;

		if (Pipeline::Active && settings) {
			// HDR toggle. The renderSettings block now carries
			// HDR / CSM / the postprocess chain, so toggling HDR
			// is a single click — no Camera-component ceremony
			// required. (Earlier revisions opened an "Add Camera?"
			// confirm dialog here, but the camera typically lives
			// in a child node, not the root, so the existence
			// check was always wrong; removing it makes the
			// toggle a single click.)
			bool hdr = settings->IsHDR();
			if (ImGui::Checkbox("HDR (rgba16f + ACES tonemap)", &hdr)) {
				settings->SetHDR(hdr);
				Pipeline::Active->SetHDRMode(hdr);
				Editor::MarkSceneDirty();
			}

			// CSM toggle. Reads from the scene's renderSettings
			// (the single source of truth — the pipeline switch is
			// seeded from it on each frame by
			// PrelightPipeline::Execute).
			bool csm = settings->IsCascadedShadowMap();
			if (ImGui::Checkbox("Cascaded Shadow Map (CSM)", &csm)) {
				settings->SetCascadedShadowMap(csm);
				Pipeline::Active->SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, csm);
				Editor::MarkSceneDirty();
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextDisabled("Postprocess chain");

			// One row per effect entry: name + enabled checkbox +
			// reorder buttons + remove. Add row at the bottom.
			auto &chain = settings->GetChainMutable();
			for (size_t i = 0; i < chain.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				auto &entry = chain[i];

				// Resolve status: dim the row when the name is not
				// in the registry so the user can see at a glance
				// which entries are broken.
				auto resolved = fury::PostProcessRegistry::Get(entry.effectName);
				if (!resolved) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
				}
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%s", entry.effectName.c_str());
				if (!resolved) {
					ImGui::PopStyleColor();
					ImGui::SameLine();
					ImGui::TextDisabled("(unresolved)");
				}

				ImGui::SameLine();
				if (ImGui::Checkbox("Enabled", &entry.enabled)) {
					settings->SetEffectEnabled(static_cast<unsigned int>(i), entry.enabled);
					Editor::MarkSceneDirty();
				}

				ImGui::SameLine();
				if (ImGui::Button("Up") && i > 0) {
					settings->MoveEffect(static_cast<unsigned int>(i), static_cast<unsigned int>(i - 1));
					Editor::MarkSceneDirty();
				}
				ImGui::SameLine();
				if (ImGui::Button("Down") && i + 1 < chain.size()) {
					settings->MoveEffect(static_cast<unsigned int>(i), static_cast<unsigned int>(i + 1));
					Editor::MarkSceneDirty();
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove")) {
					settings->RemoveEffect(entry.effectName);
					Editor::MarkSceneDirty();
					ImGui::PopID();
					break; // chain mutated; restart the loop.
				}
				ImGui::PopID();
			}

			if (ImGui::Button("Add Effect...")) {
				ImGui::OpenPopup("PostprocessPicker");
			}
			// Effect picker modal (task 4.3): reuses the shared
			// RenderAssetPickerModal; the PostProcessEffect collect
			// branch in EditorAssetPicker iterates the process-global
			// PostProcessRegistry (effects don't live in the scene's
			// EntityManager). Picking appends to the chain enabled.
			RenderAssetPickerModal("PostprocessPicker", "Add Postprocess Effect",
				typeid(PostProcessEffect),
				[settings](std::shared_ptr<void> p) {
					auto effect = std::static_pointer_cast<PostProcessEffect>(p);
					if (effect) {
						settings->AddEffect(effect->GetName(), true);
						Editor::MarkSceneDirty();
					}
				});
		} else {
			ImGui::TextDisabled("(no active pipeline or scene)");
		}
	}

	ImGui::End();
}

// ----------------------------------------------------------------
// Profiler window (Perf / GBuffer / Shadows tabs)
// ----------------------------------------------------------------
namespace {
// Shadows tab light-selector state. Index into the sorted
// shadow-casting light list (always >= 0; clamped to range at the
// top of each Shadows tab render). Session-local; matches the
// existing pattern (e.g. draw_light_bounds) of keeping editor
// toggles in static state rather than imgui.ini.
int g_SelectedShadowLightIndex = 0;

// Recursive helper: collect every SceneNode whose Light casts shadows.
void CollectShadowCasters(const std::shared_ptr<SceneNode> &node,
	std::vector<std::shared_ptr<SceneNode>> &out)
{
	if (!node) return;
	if (auto light = node->GetComponent<Light>()) {
		if (light->GetCastShadows())
			out.push_back(node);
	}
	for (unsigned int i = 0; i < node->GetChildCount(); ++i)
		CollectShadowCasters(node->GetChildAt(i), out);
}

// Stable sort: directional first, then point, then spot.
static int LightTypeRank(LightType type) {
	switch (type) {
	case LightType::DIRECTIONAL: return 0;
	case LightType::POINT: return 1;
	case LightType::SPOT: return 2;
	}
	return 3;
}
void RenderProfilerPerfTab() {
	static float upper_bound = 100.0f;
	const float curFps = ImGui::GetIO().Framerate;
	while (curFps > upper_bound) upper_bound += 50;
	while (upper_bound - 50 > curFps) upper_bound -= 50;

	// Use ImGui's built-in PlotLines; no need to maintain a
	// per-label ring buffer for the editor's profiler tab.
	static float values[120] = {};
	static int values_offset = 0;
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
	ImGui::Text("DrawCall: %u", RenderUtil::Instance()->GetDrawCall());
	ImGui::Text("Triangles: %u", RenderUtil::Instance()->GetTriangleCount());
	ImGui::Text("Mesh: %u", RenderUtil::Instance()->GetMeshCount());
	ImGui::Text("SkinnedMesh: %u", RenderUtil::Instance()->GetSkinnedMeshCount());
	ImGui::Text("Light: %u", RenderUtil::Instance()->GetLightCount());
}

void RenderProfilerGBufferTab() {
	if (!Pipeline::Active) {
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
		if (auto t = Pipeline::Active->GetTextureByName(tex_name)) {
			const float tex_w = (float)t->GetWidth();
			const float tex_h = (float)t->GetHeight();
			const float aspect = (tex_w > 0.0f && tex_h > 0.0f)
									 ? tex_h / tex_w
									 : 0.5625f; // 16:9 fallback
			ImGui::Image((ImTextureID)(intptr_t)t->GetID(),
						 ImVec2(img_w, img_w * aspect),
						 ImVec2(0, 1), ImVec2(1, 0));
		} else {
			ImGui::TextDisabled("(not in active pipeline)");
		}
	};
	// LDR/HDR auto-switch: the HDR pipeline (DefferedLightingPBR)
	// names its lighting targets hdr_light / hdr_composite (rgba16f)
	// and has no gbuffer_light at all — key the list off what the
	// active pipeline declares rather than the LDR layout, so the
	// light buffer keeps updating after a pipeline switch. HDR
	// float textures sample fine through ImGui::Image (values >1
	// clamp to white, which reads as "hot" — acceptable for debug).
	show("Depth Buffer:", "gbuffer_depth");
	show("Normal Buffer:", "gbuffer_normal");
	show("Diffuse Buffer:", "gbuffer_diffuse");
	if (Pipeline::Active->GetTextureByName("hdr_light")) {
		show("Light Buffer (HDR rgba16f):", "hdr_light");
		show("Composite (pre-tonemap HDR):", "hdr_composite");
	} else {
		show("Light Buffer:", "gbuffer_light");
	}
}

void RenderProfilerShadowsTab() {
	if (!Pipeline::Active) {
		ImGui::TextDisabled("(no active pipeline)");
		return;
	}
	if (!Scene::Active) {
		ImGui::TextDisabled("(no active scene)");
		return;
	}

	ImGui::Separator();

	// 1. Walk the active scene to collect every Shadow-casting Light.
	//    We walk SceneNode directly (instead of RenderQuery::lightNodes)
	//    because RenderQuery is frustum-culled; off-screen lights that
	//    are still candidates should appear in the dropdown.
	std::vector<std::shared_ptr<SceneNode>> shadowLightNodes;
	CollectShadowCasters(Scene::Active->GetRootNode(), shadowLightNodes);

	std::sort(shadowLightNodes.begin(), shadowLightNodes.end(),
		[](const std::shared_ptr<SceneNode> &a, const std::shared_ptr<SceneNode> &b) {
			auto la = a->GetComponent<Light>();
			auto lb = b->GetComponent<Light>();
			int ra = la ? LightTypeRank(la->GetType()) : 99;
			int rb = lb ? LightTypeRank(lb->GetType()) : 99;
			if (ra != rb) return ra < rb;
			return a->GetName() < b->GetName();
		});

	if (shadowLightNodes.empty()) {
		ImGui::TextDisabled("(no shadow-casting lights)");
		return;
	}

	// Clamp selection into the current list (scene reload may shrink it).
	if (g_SelectedShadowLightIndex < 0 ||
		g_SelectedShadowLightIndex >= (int)shadowLightNodes.size()) {
		g_SelectedShadowLightIndex = 0;
	}

	// 2. Light-selector combo. Hidden when only one light (no point
	//    showing a picker). The dropdown lists individual lights only;
	//    there is no "All lights" entry — each shadow map is large
	//    enough that stacking them is not useful.
	if (shadowLightNodes.size() > 1) {
		auto selNode = shadowLightNodes[g_SelectedShadowLightIndex];
		auto selLight = selNode->GetComponent<Light>();
		static thread_local std::string previewBuf;
		previewBuf = EnumUtil::LightTypeToString(selLight->GetType());
		previewBuf += " - ";
		previewBuf += selNode->GetName();

		ImGui::Text("Light:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##shadow_light_picker", previewBuf.c_str())) {
			for (int i = 0; i < (int)shadowLightNodes.size(); ++i) {
				auto n = shadowLightNodes[i];
				auto l = n->GetComponent<Light>();
				std::string entry = EnumUtil::LightTypeToString(l->GetType());
				entry += " - ";
				entry += n->GetName();
				bool sel = (g_SelectedShadowLightIndex == i);
				if (ImGui::Selectable(entry.c_str(), sel))
					g_SelectedShadowLightIndex = i;
				if (sel) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	// 3. Render the selected light's shadow section.
	const float scale = 1.0f;

	auto renderOneSection = [&](const std::shared_ptr<SceneNode> &lightNode) {
		auto light = lightNode->GetComponent<Light>();
		if (!light) return;

		const std::string typeName = EnumUtil::LightTypeToString(light->GetType());
		std::string header = "Shadow - ";
		header += typeName;
		header += " ";
		header += lightNode->GetName();
		ImGui::Text("%s", header.c_str());

		auto shadowTex = Pipeline::Active->GetLastShadowTexture(*lightNode);
		if (!shadowTex) {
			ImGui::TextDisabled("(no shadow map this frame)");
			ImGui::Separator();
			return;
		}

		switch (light->GetType()) {
		case LightType::DIRECTIONAL:
			if (Pipeline::Active->IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP)) {
				static auto img0 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
				static auto img1 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
				static auto img2 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
				static auto img3 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
				static auto blitShader = Shader::Create("EditorBlitArrayShader", ShaderType::OTHER);

				if (blitShader->GetDirty()) {
					const char* blit_vs =
						"in vec3 vertex_position;"
						"out vec2 out_uv;"
						"void main()"
						"{"
						"	out_uv = vertex_position.xy * 0.5 + 0.5;"
						"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);"
						"}";
					const char* blit_fs =
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
				for (int i = 0; i < 4; ++i) {
					blitShader->Bind();
					blitShader->BindFloat("index", (float)i);
					render->Blit(shadowTex, slices[i], blitShader);
				}

				ImGui::BeginGroup();
				for (int row = 0; row < 2; ++row) {
					ImGui::Image((ImTextureID)(intptr_t)slices[row * 2]->GetID(),
								 ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
					ImGui::SameLine(140);
					ImGui::Image((ImTextureID)(intptr_t)slices[row * 2 + 1]->GetID(),
								 ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
				}
				ImGui::EndGroup();
			} else {
				ImGui::Image((ImTextureID)(intptr_t)shadowTex->GetID(),
							 ImVec2(256 * scale, 256 * scale), ImVec2(0, 1), ImVec2(1, 0));
			}
			break;

		case LightType::SPOT:
			ImGui::Image((ImTextureID)(intptr_t)shadowTex->GetID(),
						 ImVec2(256 * scale, 256 * scale), ImVec2(0, 1), ImVec2(1, 0));
			break;

		case LightType::POINT: {
			static auto img0 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img1 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img2 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img3 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img4 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img5 = Texture::GetTemporary(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto blitShader = Shader::Create("EditorBlitCubeShader", ShaderType::OTHER);

			if (blitShader->GetDirty()) {
				const char* blit_vs =
					"in vec3 vertex_position;"
					"out vec2 out_uv;"
					"void main()"
					"{"
					"	out_uv = vertex_position.xy * 0.5 + 0.5;"
					"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);"
					"}";
				const char* blit_fs =
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

			// Use the light's actual world position so the cube faces
			// are oriented around the light, not around the origin.
			Vector4 lightPos(
				lightNode->GetWorldPosition().x,
				lightNode->GetWorldPosition().y,
				lightNode->GetWorldPosition().z,
				1.0f);
			std::array<Matrix4, 6> dirMatrices;
			dirMatrices[0].LookAt(lightPos, lightPos + Vector4(1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
			dirMatrices[1].LookAt(lightPos, lightPos + Vector4(-1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
			dirMatrices[2].LookAt(lightPos, lightPos + Vector4(0.0f, 1.0f, 0.0f), Vector4(0.0f, 0.0f, 1.0f));
			dirMatrices[3].LookAt(lightPos, lightPos + Vector4(0.0f, -1.0f, 0.0f), Vector4(0.0f, 0.0f, -1.0f));
			dirMatrices[4].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, 1.0f), Vector4(0.0f, -1.0f, 0.0f));
			dirMatrices[5].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, -1.0f), Vector4(0.0f, -1.0f, 0.0f));

			auto render = RenderUtil::Instance();
			std::shared_ptr<Texture> faces[6] = {img0, img1, img2, img3, img4, img5};
			for (int i = 0; i < 6; ++i) {
				blitShader->Bind();
				blitShader->BindMatrix("matrix", dirMatrices[i]);
				render->Blit(shadowTex, faces[i], blitShader);
			}

			ImGui::BeginGroup();
			for (int row = 0; row < 3; ++row) {
				ImGui::Image((ImTextureID)(intptr_t)faces[row * 2]->GetID(),
							 ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
				ImGui::SameLine(140);
				ImGui::Image((ImTextureID)(intptr_t)faces[row * 2 + 1]->GetID(),
							 ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
			}
			ImGui::EndGroup();
			break;
		}
		}

		ImGui::Separator();
	};

	if (g_SelectedShadowLightIndex >= 0 &&
		g_SelectedShadowLightIndex < (int)shadowLightNodes.size()) {
		renderOneSection(shadowLightNodes[g_SelectedShadowLightIndex]);
	}
}
} // namespace

void RenderProfilerWindow(bool* open) {
	ImGui::SetNextWindowSize(ImVec2(420, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Profiler", open)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("ProfilerTabs")) {
		if (ImGui::BeginTabItem("Perf")) {
			RenderProfilerPerfTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("GBuffer")) {
			RenderProfilerGBufferTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Shadows")) {
			RenderProfilerShadowsTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

// ----------------------------------------------------------------
// Scene Inspector
// ----------------------------------------------------------------
namespace {
// Per-row rename state.
struct RenameState {
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
struct PendingReparent {
	SceneNode* source;
	SceneNode* target;
};
static std::vector<PendingReparent> g_PendingReparents;

// Deferred add/delete, same reasoning as the reparent queue.
struct PendingAdd {
	SceneNode::Ptr parent;
	SceneNode::Ptr child;
};
struct PendingDelete {
	SceneNode* target;
};
static std::vector<PendingAdd> g_PendingAdds;
static std::vector<PendingDelete> g_PendingDeletes;

// ImGui drag-drop payload key. Pointer-sized payloads are safe
// because both source and target live in the same process.
static constexpr const char* kSceneNodeDragPayload = "FURY_SCENE_NODE";

// True iff target is reachable from ancestor.
bool IsDescendantOf(SceneNode* ancestor, SceneNode* target) {
	if (!ancestor || !target) return false;
	for (unsigned int i = 0; i < ancestor->GetChildCount(); ++i) {
		auto child = ancestor->GetChildAt(i);
		if (child.get() == target) return true;
		if (IsDescendantOf(child.get(), target)) return true;
	}
	return false;
}

// Adjust node's local TRS so the world transform stays the same after reparenting. Call BEFORE RemoveChild/AddChild.
void PreserveWorldTransformOnReparent(SceneNode* node, SceneNode* newParent) {
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
// inspector-generated names to never collide. Delegates to the
// engine-level `UniqueName` helper (EntityUtil.h) so asset
// rename/duplicate and node rename share the same suffix scheme.
std::string UniqueChildName(SceneNode* parent, const std::string& base) {
	if (!parent) return base;
	return UniqueName(base, [&](const std::string& n) {
		return parent->FindChild(n) != nullptr;
	});
}

// Action: create a fresh child under `target` and select it.
void DoAddChild(SceneNode* target) {
	if (!target) return;
	auto name = UniqueChildName(target, "Node");
	auto child = SceneNode::Create(name);

	g_PendingAdds.push_back({target->shared_from_this(), child});
	SetSelectedSceneNode(child.get());
	Editor::MarkSceneDirty();
}

// Action: deep-clone `target` (including descendants) under the
// same parent, with a "(copy)" / "(copy N)" suffix. The clone
// becomes the new selection. The root node is not duplicable.
void DoDuplicate(SceneNode* target) {
	if (!target) return;
	auto parent = target->GetParent();
	if (!parent) return; // root — caller filters the menu
	std::string baseName = target->GetName();
	if (baseName.empty()) baseName = "Node";
	std::string copyName = baseName + " (copy)";
	if (parent->FindChild(copyName)) {
		for (int i = 1; i < 100000; ++i) {
			copyName = baseName + " (copy " + std::to_string(i) + ")";
			if (!parent->FindChild(copyName)) break;
		}
	}
	auto clone = target->CloneTree(copyName);
	// Defer parent attachment + SceneManager re-registration.
	g_PendingAdds.push_back({parent, clone});
	SetSelectedSceneNode(clone.get());
	Editor::MarkSceneDirty();
}

// Action: detach `target` from its parent. If the editor's
// selection pointed at the deleted node, clear it.
void DoDelete(SceneNode* target) {
	if (!target) return;
	if (!target->GetParent()) return; // root — caller filters the menu
	if (g_SelectedSceneNode == target)
		SetSelectedSceneNode(nullptr);
	g_RenameStates.erase(target);
	g_PendingDeletes.push_back({target});
	Editor::MarkSceneDirty();
}

// Action: start in-place rename of `target`. Cancels any other
// in-flight rename so only one field is active at a time.
void DoRenameActivate(SceneNode* target) {
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
void HandleRowInteractions(SceneNode* node, int depth, bool isOpen) {
	if (!node) return;
	const bool isRoot = (depth == 0);
	const std::string popupId = "NodeMenu##" + std::to_string(reinterpret_cast<uintptr_t>(node));

	// --- Right-click context menu ------------------------------
	if (ImGui::BeginPopupContextItem(popupId.c_str())) {
		if (ImGui::MenuItem("Add Child")) DoAddChild(node);
		if (!isRoot) {
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate")) DoDuplicate(node);
			if (ImGui::MenuItem("Rename")) DoRenameActivate(node);
			if (ImGui::MenuItem("Delete")) DoDelete(node);
		}
		ImGui::EndPopup();
	}

	// --- Drag source --------------------------------------------
	// Disabled for the root: the spec only allows reparenting
	// across parents, never "out of" the root.
	if (!isRoot && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
		SceneNode* payload = node;
		ImGui::SetDragDropPayload(kSceneNodeDragPayload, &payload, sizeof(payload));
		ImGui::Text("%s", node->GetName().empty() ? "(unnamed)" : node->GetName().c_str());
		ImGui::EndDragDropSource();
	}

	// --- Drop target --------------------------------------------
	// Queue the reparent; the actual mutation runs at the end of
	// the frame (see RenderSceneInspectorWindow's tail) so we don't
	// mutate the scene graph while ImGui is mid-tree.
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneNodeDragPayload)) {
			IM_ASSERT(payload->DataSize == sizeof(SceneNode*));
			SceneNode* dragged = *(SceneNode**)payload->Data;
			if (dragged && dragged != node && !IsDescendantOf(dragged, node)) {
				g_PendingReparents.push_back({dragged, node});
			}
		}
		ImGui::EndDragDropTarget();
	}

	// --- Hover-to-expand during drag ---------------------------
	// Track first-hover time on a collapsed row; expand the row
	// once the dwell exceeds the threshold.
	if (ImGui::IsDragDropActive() && ImGui::IsItemHovered() && !isOpen && node->GetChildCount() > 0) {
		double now = ImGui::GetTime();
		auto it = g_HoverExpandStart.find(node);
		if (it == g_HoverExpandStart.end())
			g_HoverExpandStart[node] = now;
		else if (now - it->second > kHoverExpandDelay) {
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
			g_HoverExpandStart.erase(it);
		}
	} else {
		// Cursor left the row — reset the dwell timer.
		g_HoverExpandStart.erase(node);
	}
}

// Shared row renderer used by both the Scene::Active path and the
// Lua-provided TreeNode path. `node` may be nullptr for synthetic
// rows supplied by Lua (read-only display only — interactions
// are skipped when node is null).
void RenderNodeRow(SceneNode* node, const std::string& displayName, int depth, bool* outOpen) {
	const bool isRoot = (depth == 0);
	// OpenOnArrow (click the triangle) is handled by ImGui. We don't
	// pass OpenOnDoubleClick because the double-click branch below
	// flips the storage directly — a reliable, version-agnostic path
	// that avoids depending on the ImGui flag firing in this context.
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
	if (isRoot) flags |= ImGuiTreeNodeFlags_DefaultOpen;
	if (node && g_SelectedSceneNode == node)
		flags |= ImGuiTreeNodeFlags_Selected;
	bool isLeaf = true;
	if (node)
		isLeaf = (node->GetChildCount() == 0);
	else
		isLeaf = false; // unknown child count for synthetic rows
	if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	bool open = false;
	auto rit = node ? g_RenameStates.find(node) : g_RenameStates.end();
	if (rit != g_RenameStates.end() && rit->second.active) {
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
		if (rit->second.needsFocus) {
			ImGui::SetKeyboardFocusHere(-1);
			rit->second.needsFocus = false;
		}
		bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
		bool deactivated = ImGui::IsItemDeactivated();
		if (committed || (deactivated && !cancelled)) {
			if (node->GetName() != rit->second.buffer) {
				node->SetName(rit->second.buffer);
				Editor::MarkSceneDirty();
			}
			rit->second.active = false;
		} else if (cancelled) {
			rit->second.active = false;
		}
		ImGui::PopItemWidth();
	} else {
		open = ImGui::TreeNodeEx((void*)node, flags, "%s",
								 displayName.empty() ? "(unnamed)" : displayName.c_str());
	}

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		if (node) SetSelectedSceneNode(node);
	}

	// Double-click on any non-root row: request a camera frame AND
	// toggle expand/collapse. We flip the storage directly instead of
	// using ImGuiTreeNodeFlags_OpenOnDoubleClick — the flag is unreliable
	// in this layout (its press detection races with our own click
	// check, and in some ImGui builds the toggle never fires on the
	// label area). The next frame's TreeNodeEx picks up the flipped
	// state and the children appear. Leaves can't expand (zero
	// children), so the frame is the only visible effect for them.
	// Rename is no longer reachable via double-click — use F2 or the
	// context menu's "Rename" entry instead.
	if (node && !isRoot && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		Editor::FrameSelection(node);
		ImGuiID id = ImGui::GetID((void*)node);
		ImGuiStorage* storage = ImGui::GetStateStorage();
		int current = storage->GetInt(id, 0);
		storage->SetInt(id, current == 0 ? 1 : 0);
	}

	// Only the C++ scene-graph path exposes mutations: synthetic
	// Lua-owned rows are read-only.
	if (node) HandleRowInteractions(node, depth, open);

	if (outOpen) *outOpen = open;
}

void RenderSceneNodeRecursive(const std::shared_ptr<SceneNode>& node, int depth) {
	if (!node) return;
	bool open = false;
	RenderNodeRow(node.get(), node->GetName(), depth, &open);
	if (open && node->GetChildCount() > 0) {
		for (unsigned int i = 0; i < node->GetChildCount(); ++i)
			RenderSceneNodeRecursive(node->GetChildAt(i), depth + 1);
		ImGui::TreePop();
	}
}

void RenderTreeFromProvider(const TreeNode& tn, int depth) {
	bool open = false;
	RenderNodeRow(tn.node, tn.name, depth, &open);
	if (open && !tn.children.empty()) {
		for (auto& c : tn.children)
			RenderTreeFromProvider(c, depth + 1);
		ImGui::TreePop();
	}
}
} // namespace

void RenderSceneInspectorWindow(bool* open) {
	ImGui::SetNextWindowSize(ImVec2(280, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Scene Inspector", open)) {
		ImGui::End();
		return;
	}

	// F2 activates rename on the currently selected node. We only
	// trigger if the inspector window itself has focus — otherwise
	// F2 in the viewport or another panel could surprise the user.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2) && g_SelectedSceneNode && g_SelectedSceneNode->GetParent() != nullptr) {
		DoRenameActivate(g_SelectedSceneNode);
	}

	// Clear hover-expand bookkeeping whenever no drag is in flight
	// so a fresh drag doesn't inherit stale timers.
	if (!ImGui::IsDragDropActive())
		g_HoverExpandStart.clear();

	if (g_TreeProvider) {
		try {
			TreeNode tree = g_TreeProvider();
			RenderTreeFromProvider(tree, 0);
		} catch (...) {}
	} else if (Scene::Active) {
		auto root = Scene::Active->GetRootNode();
		RenderSceneNodeRecursive(root, 0);
	} else {
		ImGui::TextDisabled("(no active scene)");
	}

	// Empty-space drop target: dropping on the trailing blank area
	// reparents the dragged node to the root. A drop on the title
	// bar / window chrome is ignored because that area isn't part of
	// the inspector's draw list.
	SceneNode* root = Scene::Active ? Scene::Active->GetRootNode().get() : nullptr;
	ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneNodeDragPayload)) {
			IM_ASSERT(payload->DataSize == sizeof(SceneNode*));
			SceneNode* dragged = *(SceneNode**)payload->Data;
			if (dragged && root && dragged != root && !IsDescendantOf(dragged, root)) {
				g_PendingReparents.push_back({dragged, root});
			}
		}
		ImGui::EndDragDropTarget();
	}

	// Apply deferred mutations. Re-register with SceneManager so the
	// renderer sees the new state.
	for (auto& pr : g_PendingReparents) {
		if (!pr.source || !pr.target) continue;
		PreserveWorldTransformOnReparent(pr.source, pr.target);
		auto sp = pr.source->shared_from_this();
		auto srcParent = pr.source->GetParent();
		if (srcParent) srcParent->RemoveChild(sp);
		pr.target->AddChild(sp);
		if (Scene::Active) {
			Scene::Active->GetSceneManager()->AddSceneNodeRecursively(sp);
		}
	}
	for (auto& pa : g_PendingAdds) {
		if (!pa.parent || !pa.child) continue;
		pa.parent->AddChild(pa.child);
		if (Scene::Active) {
			Scene::Active->GetSceneManager()->AddSceneNodeRecursively(pa.child);
		}
	}
	for (auto& pd : g_PendingDeletes) {
		if (!pd.target) continue;
		auto sp = pd.target->shared_from_this();
		if (Scene::Active) {
			Scene::Active->GetSceneManager()->RemoveSceneNode(sp);
		}
		pd.target->RemoveFromParent();
	}
	if (!g_PendingReparents.empty() ||
		!g_PendingAdds.empty() ||
		!g_PendingDeletes.empty()) {
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
namespace {
ImVec4 ColorForLevel(LogLevel l) {
	switch (l) {
	case LogLevel::Debug:
		return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
	case LogLevel::Info:
		return ImVec4(0.5f, 0.85f, 1.0f, 1.0f);
	case LogLevel::Warn:
		return ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
	case LogLevel::Error:
		return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
	case LogLevel::Critical:
		return ImVec4(0.8f, 0.1f, 0.1f, 1.0f);
	}
	return ImVec4(1, 1, 1, 1);
}
} // namespace

void RenderConsoleWindow(bool* open) {
	ImGui::SetNextWindowSize(ImVec2(640, 320), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console", open)) {
		ImGui::End();
		return;
	}

	if (ImGui::Button("Clear")) GlobalLogBuffer().Clear();

	const float footer_h = ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("##log", ImVec2(0, -footer_h), true,
					  ImGuiWindowFlags_HorizontalScrollbar);

	auto entries = GlobalLogBuffer().Snapshot();
	for (const auto& e : entries) {
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
						 ImGuiInputTextFlags_EnterReturnsTrue)) {
		if (buf[0] != '\0') {
			std::string line = buf;
			GlobalLogBuffer().Push(LogLevel::Info, std::string("> ") + line);
			if (g_CommandHandler) {
				try {
					g_CommandHandler(line);
				} catch (...) {}
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
// Renders the in-scene assets (every Mesh and Material in the
// active Scene's EntityManager) as an icon grid, Unity/UE4-style.
// Replaces the legacy flat file-listing (directory_iterator over
// Resource/Scene/). File open / import / save stay in the File
// menu and the Open/Import/Save-As modals.

// (type_index, name) of the currently-selected tile. Replaces the
// legacy std::string g_SelectedFile. type_index lets the inspector's
// jump-to-asset disambiguate Mesh vs Material of the same name.
// External linkage so Editor::SelectAssetInBrowser (Editor.cpp)
// can set it; the grid renders + reads it every frame.
std::optional<std::pair<std::type_index, std::string>> g_SelectedAsset;
std::optional<std::string> g_PendingScrollToAsset;

namespace {
// Inline-rename state. When g_RenamingAsset is set, the
// matching tile's label row is replaced with an InputText.
static std::optional<std::pair<std::type_index, std::string>> g_RenamingAsset;
static char g_RenameBuffer[256];

// Right-click → Refresh sets this. No-op since we re-enumerate
// every frame, but the menu item exists per spec.
static bool g_NeedsRefresh = false;

// Display mode for the Content Browser grid. Persisted to
// imgui.ini via the Settings handler (see Editor.cpp's
// FuryEditor Settings handler — we piggy-back on the existing
// `ContentBrowser.DisplayMode` ini key).
enum class DisplayMode { List = 0,
						 Thumbnail = 1 };
static DisplayMode g_DisplayMode = DisplayMode::Thumbnail;

// Tile geometry. The thumbnail is 96px (larger than the
// original 64px so the tile occupies a usable size), and the
// tile pitch is 112px (96 + 16 padding/label gutter).
constexpr float kTileThumbnail = 96.0f;
constexpr float kTilePitch = 112.0f;
// Fixed height of the label row below the thumbnail.
constexpr float kTileLabelH = 18.0f;

struct TileEntry {
	std::type_index type;
	std::string name;
	std::shared_ptr<void> ptr;
};

// Content-browser filter state (window-local). g_FilterType indexes
// kFilterTypeNames; g_FilterText is a case-insensitive fuzzy
// (subsequence) match against asset names.
static int g_FilterType = 0;
static char g_FilterText[128] = "";
constexpr const char* kFilterTypeNames[] = {
	"All", "Mesh", "Material", "Texture", "AnimationClip"};

// Case-insensitive subsequence: every char of `pattern` appears in
// `text` in order ("spz" matches "Sponza").
bool FuzzyMatch(const char* pattern, const char* text) {
	if (!pattern || !text) return false;
	auto lower = [](char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
	};
	for (; *text && *pattern; ++text)
		if (lower(*text) == lower(*pattern)) ++pattern;
	return *pattern == '\0';
}

// True when `tile` passes the active type + text filters.
bool TilePassesFilter(const TileEntry& tile) {
	if (g_FilterType != 0) {
		const std::type_info* want = nullptr;
		switch (g_FilterType) {
		case 1: want = &typeid(Mesh); break;
		case 2: want = &typeid(Material); break;
		case 3: want = &typeid(Texture); break;
		case 4: want = &typeid(AnimationClip); break;
		}
		if (want && tile.type != *want) return false;
	}
	if (g_FilterText[0] != '\0' && !FuzzyMatch(g_FilterText, tile.name.c_str()))
		return false;
	return true;
}

void CollectTiles(std::vector<TileEntry>& tiles) {
	if (!Scene::Active) return;
	auto em = Scene::Active->GetEntityManager();
	if (!em) return;
	em->ForEach<Mesh>([&](const std::shared_ptr<Mesh>& m) {
		tiles.push_back({typeid(Mesh), m->GetName(),
						 std::static_pointer_cast<void>(m)});
		return true;
	});
	em->ForEach<Material>([&](const std::shared_ptr<Material>& m) {
		tiles.push_back({typeid(Material), m->GetName(),
						 std::static_pointer_cast<void>(m)});
		return true;
	});
	em->ForEach<Texture>([&](const std::shared_ptr<Texture>& t) {
		tiles.push_back({typeid(Texture), t->GetName(),
						 std::static_pointer_cast<void>(t)});
		return true;
	});
	em->ForEach<AnimationClip>([&](const std::shared_ptr<AnimationClip>& c) {
		tiles.push_back({typeid(AnimationClip), c->GetName(),
						 std::static_pointer_cast<void>(c)});
		return true;
	});
}

bool IsTileSelected(const TileEntry& tile) {
	return g_SelectedAsset &&
		   g_SelectedAsset->first == tile.type &&
		   g_SelectedAsset->second == tile.name;
}

// Material thumbnail per task 4.5. Returns true if a
// thumbnail was drawn (caller skips the default placeholder).
void RenderMaterialThumbnail(const std::shared_ptr<Material>& mat,
							 const ImVec2& thumb_min) {
	auto diffuse = mat->GetTexture(Material::DIFFUSE_TEXTURE);
	if (diffuse) {
		// Flip Y to match the engine's texture convention
		// (EditorNodeProperties.cpp:304 pattern).
		ImGui::SetCursorScreenPos(thumb_min);
		ImGui::Image((ImTextureID)(intptr_t)diffuse->GetID(),
					 ImVec2(kTileThumbnail, kTileThumbnail),
					 ImVec2(0, 1), ImVec2(1, 0));
		return;
	}

	auto u = mat->GetUniform(Material::DIFFUSE_COLOR);
	if (u) {
		auto col4 = std::dynamic_pointer_cast<Uniform4f>(u);
		if (col4) {
			ImVec4 c(col4->GetDataAt(0), col4->GetDataAt(1),
					 col4->GetDataAt(2), col4->GetDataAt(3));
			ImGui::GetWindowDrawList()->AddRectFilled(thumb_min,
													  ImVec2(thumb_min.x + kTileThumbnail,
															 thumb_min.y + kTileThumbnail),
													  ImGui::GetColorU32(c));
			return;
		}
	}

	// Checkerboard placeholder.
	ImVec2 p_max(thumb_min.x + kTileThumbnail,
				 thumb_min.y + kTileThumbnail);
	ImGui::GetWindowDrawList()->AddRectFilled(thumb_min, p_max,
											  ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f)));
	ImGui::GetWindowDrawList()->AddRectFilled(
		ImVec2(thumb_min.x, thumb_min.y),
		ImVec2(thumb_min.x + kTileThumbnail / 2,
			   thumb_min.y + kTileThumbnail / 2),
		ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)));
	ImGui::GetWindowDrawList()->AddRectFilled(
		ImVec2(thumb_min.x + kTileThumbnail / 2,
			   thumb_min.y + kTileThumbnail / 2),
		p_max,
		ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)));
}

// Counts how many MeshRender components across the scene
// graph reference the asset (mesh.lock() == target for a
// mesh delete, materials[i].lock() == target for a material
// delete). Used by the Delete action's in-use guard (D10).
unsigned int CountAssetReferences(std::type_index type,
								  const std::shared_ptr<void>& target) {
	unsigned int count = 0;
	if (!Scene::Active) return count;
	auto root = Scene::Active->GetRootNode();
	if (!root) return count;

	std::function<void(const SceneNode::Ptr&)> walk =
		[&](const SceneNode::Ptr& node) {
			if (!node) return;
			auto mr = node->GetComponent<MeshRender>();
			if (mr) {
				if (type == typeid(Mesh)) {
					auto m = std::static_pointer_cast<Mesh>(target);
					if (auto bound = mr->GetMesh())
						if (bound == m) ++count;
				} else if (type == typeid(Material)) {
					auto mat = std::static_pointer_cast<Material>(target);
					for (unsigned int i = 0; i < mr->GetMaterialCount(); ++i)
						if (mr->GetMaterial(i) == mat) ++count;
				}
			}
			for (unsigned int i = 0; i < node->GetChildCount(); ++i)
				walk(node->GetChildAt(i));
		};
	walk(root);
	return count;
}

// Helper: ellipsize `name` to fit `width` pixels, appending "…".
static std::string EllipsizeName(const std::string& name, float width) {
	ImVec2 ts = ImGui::CalcTextSize(name.c_str());
	if (ts.x <= width)
		return name;
	float ellW = ImGui::CalcTextSize("…").x;
	float budget = width - ellW;
	if (budget <= 0)
		return "…";
	size_t lo = 0, hi = name.size(), best = 0;
	while (lo <= hi) {
		size_t mid = (lo + hi) / 2;
		std::string sub = name.substr(0, mid);
		if (ImGui::CalcTextSize(sub.c_str()).x <= budget) {
			best = mid;
			lo = mid + 1;
		} else
			hi = mid - 1;
	}
	return name.substr(0, best) + "…";
}

// Render a single asset tile. The whole tile (thumbnail + label)
// is one Selectable, so the user can click anywhere on the tile
// to select it — not just on the label text. In Thumbnail mode
// the tile shows a 96×96 thumbnail + name; in List mode it shows
// just the name as a row.
void RenderAssetTile(const TileEntry& tile, bool& anyTileScrolled) {
	const std::string& name = tile.name;
	ImGui::PushID(name.c_str());

	// Capture the tile's top-left for drawing the type badge.
	ImVec2 tile_min = ImGui::GetCursorScreenPos();

	bool sel = IsTileSelected(tile);
	bool renamingThisTile = g_RenamingAsset &&
							g_RenamingAsset->first == tile.type &&
							g_RenamingAsset->second == name;

	// --- Thumbnail mode: thumbnail + label below ---
	// We render the thumbnail as a non-interactive image, then the
	// label as a Selectable. To make the WHOLE tile clickable (not
	// just the label), we also render an InvisibleButton covering
	// the thumbnail area and forward its click to the Selectable.
	// Both share the same PushID namespace so their hovered/active
	// state is unified.
	if (g_DisplayMode == DisplayMode::Thumbnail) {
		ImGui::BeginGroup();

		ImVec2 thumb_min = ImGui::GetCursorScreenPos();

	// Thumbnail (non-interactive image).
	if (tile.type == typeid(Material)) {
		auto mat = std::static_pointer_cast<Material>(tile.ptr);
		RenderMaterialThumbnail(mat, thumb_min);
	} else if (tile.type == typeid(Texture)) {
		// Texture tile — render the texture's own GL image as the
		// thumbnail. Textures are first-class assets now, registered
		// in the EntityManager by Scene::Load and GltfImporter.
		auto tex = std::static_pointer_cast<Texture>(tile.ptr);
		ImGui::SetCursorScreenPos(thumb_min);
		ImGui::Image((ImTextureID)(intptr_t)tex->GetID(),
					 ImVec2(kTileThumbnail, kTileThumbnail),
					 ImVec2(0, 1), ImVec2(1, 0));
	} else if (tile.type == typeid(AnimationClip)) {
		// AnimationClip tile — no GPU thumbnail; render a flat
		// placeholder rect. The top-left badge (added below) already
		// labels the tile type, so the rect itself has no text.
		ImGui::Dummy(ImVec2(kTileThumbnail, kTileThumbnail));
		ImVec2 p0 = ImGui::GetItemRectMin();
		ImVec2 p1 = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddRectFilled(p0, p1,
												  ImGui::GetColorU32(ImVec4(0.20f, 0.30f, 0.22f, 1.0f)));
	} else // Mesh
	{
			auto mesh = std::static_pointer_cast<Mesh>(tile.ptr);
			unsigned int texId = GetMeshThumbnail(mesh);
			if (texId) {
				ImGui::SetCursorScreenPos(thumb_min);
				ImGui::Image((ImTextureID)(intptr_t)texId,
							 ImVec2(kTileThumbnail, kTileThumbnail),
							 ImVec2(0, 1), ImVec2(1, 0));
			} else {
				ImGui::Dummy(ImVec2(kTileThumbnail, kTileThumbnail));
				ImVec2 p0 = ImGui::GetItemRectMin();
				ImVec2 p1 = ImGui::GetItemRectMax();
				ImGui::GetWindowDrawList()->AddRectFilled(p0, p1,
														  ImGui::GetColorU32(ImVec4(0.25f, 0.25f, 0.28f, 1.0f)));
			}
		}

	// Type badge (M / Mat / T / Anim) in the top-left corner.
	const char* badge =
		(tile.type == typeid(Mesh)) ? "M" :
		(tile.type == typeid(Material)) ? "Mat" :
		(tile.type == typeid(AnimationClip)) ? "Anim" : "T";
		ImGui::GetWindowDrawList()->AddText(thumb_min,
											ImGui::GetColorU32(ImVec4(1, 1, 0, 0.9f)), badge);

		// Invisible button over the thumbnail area so the whole
		// tile is clickable. We use SetCursorScreenPos to place
		// it exactly over the thumbnail rect.
		ImGui::SetCursorScreenPos(thumb_min);
		ImGui::InvisibleButton("##thumb_hit", ImVec2(kTileThumbnail, kTileThumbnail));
		bool thumbClicked = ImGui::IsItemClicked(0);
		bool thumbDblClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);

	// Label below: plain text with small horizontal margins so the
	// name isn't flush against the tile edges.
	constexpr float kLabelMargin = 4.0f;
	std::string display = EllipsizeName(name, kTilePitch - 2 * kLabelMargin - 8.0f);
	ImGui::SetCursorScreenPos(
		ImVec2(thumb_min.x + kLabelMargin, thumb_min.y + kTileThumbnail + 2));
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kTilePitch - 2 * kLabelMargin);
		if (renamingThisTile) {
			ImGui::SetNextItemWidth(kTilePitch);
			bool committed = ImGui::InputText("##rename", g_RenameBuffer,
											  sizeof(g_RenameBuffer),
											  ImGuiInputTextFlags_EnterReturnsTrue |
												  ImGuiInputTextFlags_AutoSelectAll);
			bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
			if (committed) {
				std::string newName(g_RenameBuffer);
				if (!newName.empty() && newName != name) {
					auto em = Scene::Active->GetEntityManager();
					if (em) {
						auto pred = [&](const std::string& n) {
							if (tile.type == typeid(Mesh))
								return em->Get<Mesh>(n) != nullptr;
							return em->Get<Material>(n) != nullptr;
						};
						if (pred(newName))
							newName = UniqueName(newName, pred);
						if (tile.type == typeid(Mesh)) {
							auto asset = em->Get<Mesh>(name);
							if (asset) {
								em->Remove<Mesh>(name);
								asset->SetName(newName);
								em->Add<Mesh>(asset);
								g_SelectedAsset = {typeid(Mesh), newName};
								Editor::MarkSceneDirty();
							}
						} else {
							auto asset = em->Get<Material>(name);
							if (asset) {
								em->Remove<Material>(name);
								asset->SetName(newName);
								em->Add<Material>(asset);
								g_SelectedAsset = {typeid(Material), newName};
								Editor::MarkSceneDirty();
							}
						}
					}
				}
				g_RenamingAsset.reset();
			} else if (cancelled) {
				g_RenamingAsset.reset();
			}
			if (g_RenamingAsset)
				ImGui::SetKeyboardFocusHere(-1);
		} else {
			// Plain text label — no Selectable, no hover background.
			// Click handling is done by the InvisibleButton over the
			// thumbnail (##thumb_hit) and the whole-tile InvisibleButton
			// added below.
			ImGui::TextUnformatted(display.c_str());
			if (thumbClicked && !ImGui::IsMouseDoubleClicked(0))
				g_SelectedAsset = std::make_pair(tile.type, name);
			if (thumbDblClicked) {
				if (tile.type == typeid(Mesh))
					OpenMeshEditor(std::static_pointer_cast<Mesh>(tile.ptr));
				else if (tile.type == typeid(AnimationClip))
					Editor::SetWindowVisible("Animation", true);
				else
					OpenMaterialEditor(std::static_pointer_cast<Material>(tile.ptr));
			}
		}
		ImGui::PopTextWrapPos();

		// Whole-tile InvisibleButton so clicking anywhere on the
		// tile (not just the thumbnail) selects the asset.
		ImGui::SetCursorScreenPos(tile_min);
		ImGui::InvisibleButton("##tile_hit",
			ImVec2(kTilePitch, kTileThumbnail + kTileLabelH + 4));
		if (ImGui::IsItemClicked(0))
			g_SelectedAsset = std::make_pair(tile.type, name);
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
			if (tile.type == typeid(Mesh))
				OpenMeshEditor(std::static_pointer_cast<Mesh>(tile.ptr));
			else
				OpenMaterialEditor(std::static_pointer_cast<Material>(tile.ptr));
		}

		ImGui::EndGroup();
	} else // List mode: just the name as a Selectable row.
	{
		ImGui::BeginGroup();

		std::string display = EllipsizeName(name, ImGui::GetContentRegionAvail().x);
		if (renamingThisTile) {
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			bool committed = ImGui::InputText("##rename", g_RenameBuffer,
											  sizeof(g_RenameBuffer),
											  ImGuiInputTextFlags_EnterReturnsTrue |
												  ImGuiInputTextFlags_AutoSelectAll);
			bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
			if (committed) {
				std::string newName(g_RenameBuffer);
				if (!newName.empty() && newName != name) {
					auto em = Scene::Active->GetEntityManager();
					if (em) {
						auto pred = [&](const std::string& n) {
							if (tile.type == typeid(Mesh))
								return em->Get<Mesh>(n) != nullptr;
							return em->Get<Material>(n) != nullptr;
						};
						if (pred(newName))
							newName = UniqueName(newName, pred);
						if (tile.type == typeid(Mesh)) {
							auto asset = em->Get<Mesh>(name);
							if (asset) {
								em->Remove<Mesh>(name);
								asset->SetName(newName);
								em->Add<Mesh>(asset);
								g_SelectedAsset = {typeid(Mesh), newName};
								Editor::MarkSceneDirty();
							}
						} else {
							auto asset = em->Get<Material>(name);
							if (asset) {
								em->Remove<Material>(name);
								asset->SetName(newName);
								em->Add<Material>(asset);
								g_SelectedAsset = {typeid(Material), newName};
								Editor::MarkSceneDirty();
							}
						}
					}
				}
				g_RenamingAsset.reset();
			} else if (cancelled) {
				g_RenamingAsset.reset();
			}
			if (g_RenamingAsset)
				ImGui::SetKeyboardFocusHere(-1);
		} else {
			// Plain text label — no Selectable, no hover background.
			ImGui::TextUnformatted(display.c_str());
			// InvisibleButton over the row for click handling.
			ImGui::SetCursorScreenPos(tile_min);
			ImGui::InvisibleButton("##list_hit",
				ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight()));
			if (ImGui::IsItemClicked(0))
				g_SelectedAsset = std::make_pair(tile.type, name);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				if (tile.type == typeid(Mesh))
					OpenMeshEditor(std::static_pointer_cast<Mesh>(tile.ptr));
				else
					OpenMaterialEditor(std::static_pointer_cast<Material>(tile.ptr));
			}
		}

		ImGui::EndGroup();
	}

	// Selection highlight: border around the whole tile group.
	ImVec2 grp_min = ImGui::GetItemRectMin();
	ImVec2 grp_max = ImGui::GetItemRectMax();
	bool selected = IsTileSelected(tile);
	if (selected) {
		ImGui::GetWindowDrawList()->AddRect(
			grp_min, grp_max,
			ImGui::GetColorU32(ImGuiCol_HeaderActive),
			0.0f, 0, 2.0f);
	}

	// Right-click context menu (on the whole tile group).
	std::string ctx_id = "ctx_" + name;
	if (ImGui::BeginPopupContextItem(ctx_id.c_str())) {
		if (ImGui::MenuItem("Duplicate")) {
			std::string baseName = name + " (copy)";
			auto pred = [&](const std::string& n) {
				auto em = Scene::Active->GetEntityManager();
				if (!em) return false;
				if (tile.type == typeid(Mesh))
					return em->Get<Mesh>(n) != nullptr;
				return em->Get<Material>(n) != nullptr;
			};
			std::string newName = UniqueName(baseName, pred);

			if (tile.type == typeid(Mesh)) {
				auto orig = std::static_pointer_cast<Mesh>(tile.ptr);
				auto copy = Mesh::Create("");
				FileUtil::DeserializeFromString(copy,
												FileUtil::SerializeToString(orig));
				copy->SetName(newName);
				Scene::Active->GetEntityManager()->Add<Mesh>(copy);
				g_SelectedAsset = {typeid(Mesh), newName};
				Editor::MarkSceneDirty();
			} else {
				auto orig = std::static_pointer_cast<Material>(tile.ptr);
				auto copy = Material::Create("");
				FileUtil::DeserializeFromString(copy,
												FileUtil::SerializeToString(orig));
				copy->SetName(newName);
				Scene::Active->GetEntityManager()->Add<Material>(copy);
				g_SelectedAsset = {typeid(Material), newName};
				Editor::MarkSceneDirty();
			}
		}
		if (ImGui::MenuItem("Rename")) {
			g_RenamingAsset = std::make_pair(tile.type, name);
			std::snprintf(g_RenameBuffer, sizeof(g_RenameBuffer),
						  "%s", name.c_str());
		}
		if (ImGui::MenuItem("Delete")) {
			unsigned int refs = CountAssetReferences(tile.type, tile.ptr);
			auto em = Scene::Active->GetEntityManager();
			if (refs > 0) {
				char msg[256];
				std::snprintf(msg, sizeof(msg),
							  "%s is in use by %u MeshRender(s). Delete anyway?",
							  name.c_str(), refs);
				auto type = tile.type;
				auto aname = name;
				RequestConfirmDialog("Delete Asset", msg,
									 [type, aname, em](bool yes) {
										 if (!yes) return;
										 if (type == typeid(Mesh))
											 em->Remove<Mesh>(aname);
										 else if (type == typeid(Material))
											 em->Remove<Material>(aname);
										 g_SelectedAsset.reset();
										 Editor::MarkSceneDirty();
									 });
			} else {
				if (tile.type == typeid(Mesh))
					em->Remove<Mesh>(name);
				else if (tile.type == typeid(Material))
					em->Remove<Material>(name);
				g_SelectedAsset.reset();
				Editor::MarkSceneDirty();
			}
		}
		// Refresh (Mesh only) — invalidate the cached thumbnail
		// so the next periodic poll re-hashes and re-renders.
		// Wired to the disk-cache spec's "right-click → Refresh"
		// path. Materials are not thumbnails, so no Refresh item
		// for them.
		if (tile.type == typeid(Mesh)) {
			if (ImGui::MenuItem("Refresh")) {
				auto mesh = std::static_pointer_cast<Mesh>(tile.ptr);
				Editor::RefreshMeshThumbnailNow(mesh);
			}
		}
		ImGui::Separator();
		// Display mode switcher.
		if (ImGui::BeginMenu("Display")) {
			if (ImGui::MenuItem("List", nullptr, g_DisplayMode == DisplayMode::List))
				g_DisplayMode = DisplayMode::List;
			if (ImGui::MenuItem("Thumbnail", nullptr, g_DisplayMode == DisplayMode::Thumbnail))
				g_DisplayMode = DisplayMode::Thumbnail;
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}

	// F2 on a selected tile enters inline rename mode.
	if (IsTileSelected(tile) && !g_RenamingAsset &&
		ImGui::IsKeyPressed(ImGuiKey_F2)) {
		g_RenamingAsset = std::make_pair(tile.type, name);
		std::snprintf(g_RenameBuffer, sizeof(g_RenameBuffer),
					  "%s", name.c_str());
	}

	ImGui::PopID();
}
} // namespace

void RenderContentBrowserWindow(bool* open) {
	ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Content Browser", open)) {
		ImGui::End();
		return;
	}

	if (!Scene::Active || !Scene::Active->GetEntityManager()) {
		ImGui::TextDisabled("(no assets in active scene)");
		ImGui::End();
		return;
	}

	// Collect every Mesh and Material into a name-sorted list.
	std::vector<TileEntry> tiles;
	CollectTiles(tiles);
	if (tiles.empty()) {
		ImGui::TextDisabled("(no assets in active scene)");
		ImGui::End();
		return;
	}
	std::sort(tiles.begin(), tiles.end(),
			  [](const TileEntry& a, const TileEntry& b) {
				  return a.name < b.name;
			  });

	// Build the set of live BufferIds from the current tiles,
	// then evict stale mesh-thumbnail cache entries (task 7.3).
	// Uses the UNFILTERED set — filtering the grid must never
	// evict thumbnails of hidden tiles.
	std::unordered_set<size_t> liveBufferIds;
	for (const auto& tile : tiles) {
		if (tile.type == typeid(Mesh)) {
			auto mesh = std::static_pointer_cast<Mesh>(tile.ptr);
			liveBufferIds.insert(mesh->GetBufferId());
		}
	}
	EvictStaleMeshThumbnails(liveBufferIds);

	// Filter toolbar: type combo + fuzzy name search.
	ImGui::SetNextItemWidth(110.0f);
	ImGui::Combo("##cb_type", &g_FilterType, kFilterTypeNames,
				 IM_ARRAYSIZE(kFilterTypeNames));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##cb_search", "Search…", g_FilterText,
							 IM_ARRAYSIZE(g_FilterText));
	ImGui::Separator();

	// Apply the filters (type AND fuzzy name); grid keeps name sort.
	tiles.erase(std::remove_if(tiles.begin(), tiles.end(),
							   [](const TileEntry& t) { return !TilePassesFilter(t); }),
				tiles.end());
	if (tiles.empty()) {
		ImGui::TextDisabled("(no assets in active scene)");
		ImGui::End();
		return;
	}

	// Evict the Refresh flag (no-op — we re-enumerate every
	// frame — but the menu item exists per spec).
	g_NeedsRefresh = false;

	// Wrapping grid. panel_right_x is the screen-space right
	// edge of the content region, captured once before any tile.
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float panel_right_x =
		ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

	bool anyScrolled = false;
	for (const auto& tile : tiles) {
		RenderAssetTile(tile, anyScrolled);

		// If a scroll-to is pending and this tile matches, scroll
		// it into view (task 4.12).
		if (g_PendingScrollToAsset && *g_PendingScrollToAsset == tile.name) {
			float top = ImGui::GetItemRectMin().y;
			float bot = ImGui::GetItemRectMax().y;
			float sy = ImGui::GetScrollY();
			float sh = ImGui::GetScrollMaxY();
			float vis_top = sy;
			float vis_bot = sy + ImGui::GetWindowHeight();
			if (top < vis_top || bot > vis_bot)
				ImGui::SetScrollHereY();
			g_PendingScrollToAsset.reset();
		}

		// Wrap: if the next tile fits on this line, SameLine.
		float tile_right_x = ImGui::GetItemRectMax().x;
		if (tile_right_x + spacing + kTilePitch <= panel_right_x)
			ImGui::SameLine();
	}

	// Right-click on empty grid space → Refresh + Display.
	if (tiles.empty() == false) {
		ImGui::InvisibleButton("##empty_grid", ImGui::GetContentRegionAvail());
		if (ImGui::BeginPopupContextItem("ctx_empty")) {
			if (ImGui::MenuItem("Refresh"))
				g_NeedsRefresh = true;
			ImGui::Separator();
			if (ImGui::BeginMenu("Display")) {
				if (ImGui::MenuItem("List", nullptr, g_DisplayMode == DisplayMode::List))
					g_DisplayMode = DisplayMode::List;
				if (ImGui::MenuItem("Thumbnail", nullptr, g_DisplayMode == DisplayMode::Thumbnail))
					g_DisplayMode = DisplayMode::Thumbnail;
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
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
// Viewport top toolbar: gizmo controls flush-left, the Debug
// Overlays combo flush-right. Consumes one line of the window's
// content region — RenderViewportWindow captures the scene rect
// AFTER this runs, so gizmo / picking coordinates stay correct.
void RenderViewportToolbar() {
	// --- Left: gizmo mode + snap + grid -----------------------
	bool changed = false;
	if (ImGui::RadioButton("Translate", g_GizmoOp == ImGuizmo::TRANSLATE)) {
		g_GizmoOp = ImGuizmo::TRANSLATE;
		changed = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", g_GizmoOp == ImGuizmo::ROTATE)) {
		g_GizmoOp = ImGuizmo::ROTATE;
		changed = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", g_GizmoOp == ImGuizmo::SCALE)) {
		g_GizmoOp = ImGuizmo::SCALE;
		changed = true;
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Snap", &g_SnapEnabled)) {
		changed = true;
	}
	if (changed) ImGui::MarkIniSettingsDirty();

	// Reference grid toggle (persisted; applied per-frame below so
	// pipeline recreation can't silently reset it). Settings →
	// Editor → Show Grid binds the same global.
	ImGui::SameLine();
	if (ImGui::Checkbox("Grid", &g_ShowGrid)) {
		ImGui::MarkIniSettingsDirty();
	}

	// --- Right: debug overlays combo (moved from Profiler) ----
	// Multi-select: scene-debug toggles are independent and toggled
	// together often, so DontClosePopups lets the user flip several
	// in one open. Preview shows the single-selected name, or a
	// count when more are active.
	static bool draw_light_bounds = false;
	static bool draw_mesh_bounds = false;
	static bool draw_custom_bounds = false;
	static bool draw_octree_bounds = false;
	static bool lod_debug_on = false;

	const char* overlayItems[] = {
		"Draw Light Bounds",
		"Draw Mesh Bounds",
		"Draw Custom Bounds",
		"Draw OcTree Bounds",
		"LOD Debug Colors"
	};
	bool overlayState[] = {
		draw_light_bounds,
		draw_mesh_bounds,
		draw_custom_bounds,
		draw_octree_bounds,
		lod_debug_on
	};
	int selectedCount = 0;
	int firstSelected = -1;
	for (int i = 0; i < 5; ++i) {
		if (overlayState[i]) {
			++selectedCount;
			if (firstSelected < 0) firstSelected = i;
		}
	}
	std::string overlayPreview;
	if (selectedCount == 0)       overlayPreview = "(none)";
	else if (selectedCount == 1)  overlayPreview = overlayItems[firstSelected];
	else                          overlayPreview = std::to_string(selectedCount) + " overlays selected";

	// Right-align on the SAME line: SameLine first so the cursor is
	// on the bar's line (after an item, GetCursorPosX reports the
	// next line's start X — measuring without SameLine wrapped the
	// combo onto a second line).
	const float combo_w = 200.0f;
	ImGui::SameLine();
	const float remaining = ImGui::GetContentRegionAvail().x;
	if (remaining > combo_w)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + remaining - combo_w);
	ImGui::SetNextItemWidth(combo_w);
	if (ImGui::BeginCombo("##debug_overlays", overlayPreview.c_str())) {
		for (int i = 0; i < 5; ++i) {
			if (ImGui::Selectable(overlayItems[i], overlayState[i],
								  ImGuiSelectableFlags_DontClosePopups)) {
				overlayState[i] = !overlayState[i];
			}
		}
		ImGui::EndCombo();
	}
	draw_light_bounds   = overlayState[0];
	draw_mesh_bounds    = overlayState[1];
	draw_custom_bounds  = overlayState[2];
	draw_octree_bounds  = overlayState[3];
	lod_debug_on        = overlayState[4];

	if (Pipeline::Active) {
		Pipeline::Active->SetSwitch(PipelineSwitch::EDITOR_GRID, g_ShowGrid);
		Pipeline::Active->SetSwitch(PipelineSwitch::LIGHT_BOUNDS, draw_light_bounds);
		Pipeline::Active->SetSwitch(PipelineSwitch::MESH_BOUNDS, draw_mesh_bounds);
		Pipeline::Active->SetSwitch(PipelineSwitch::CUSTOM_BOUNDS, draw_custom_bounds);
		Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, draw_octree_bounds);
		Pipeline::Active->SetSwitch(PipelineSwitch::LOD_DEBUG_COLORS, lod_debug_on);
	}
}

void RenderViewportWindow(bool* open) {
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
					  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus)) {
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

	// Top toolbar (gizmo + debug overlays) — consumes one line, so
	// the scene rect captured below already excludes the bar.
	RenderViewportToolbar();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const ImVec2 pos = ImGui::GetCursorScreenPos();

	// Capture the content rect for the gizmo + picking regardless
	// of whether we have a usable size this frame.
	g_ViewportContentMin = pos;
	g_ViewportContentSize = avail;
	g_ViewportHovered = ImGui::IsWindowHovered();

	if (avail.x > 0.0f && avail.y > 0.0f && Pipeline::Active) {
		const int w = static_cast<int>(avail.x);
		const int h = static_cast<int>(avail.y);

		// Resize the RT to the content rect (no-op if unchanged).
		// If the RT fails to allocate (e.g. FBO incomplete), skip
		// the image this frame rather than crash on a null texture.
		if (!rt->Resize(w, h)) {
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
		if (auto camNode = Pipeline::Active->GetCurrentCamera()) {
			if (auto cam = camNode->GetComponent<Camera>()) {
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

		// Joint-skeleton debug overlay (toggled from the Animator
		// inspector). Projects each joint's world position through
		// the active camera and draws a line/marker on top of the
		// viewport image so we can see how the skeleton is posed.
		RenderJointDebugOverlay(pos, avail);
	} else {
		g_ViewportVisible = false;
		if (Pipeline::Active)
			Pipeline::Active->SetRenderTarget(nullptr);
	}

	ImGui::End();
}

// Public wrappers for the Edit menu (called from Editor.cpp).

void DeleteSelectedSceneNode() {
	if (g_SelectedSceneNode) DoDelete(g_SelectedSceneNode);
}

void DuplicateSelectedSceneNode() {
	if (g_SelectedSceneNode) DoDuplicate(g_SelectedSceneNode);
}

void AddChildToSelectedSceneNode() {
	if (g_SelectedSceneNode) DoAddChild(g_SelectedSceneNode);
}
} // namespace Editor
} // namespace fury

#endif // WITH_EDITOR
