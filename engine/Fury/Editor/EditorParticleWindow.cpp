#include "Fury/Editor/EditorParticleWindow.h"

#if WITH_EDITOR

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Fury/BoxBounds.h"
#include "Fury/Color.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Vector4.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Editor/Editor3DPreview.h"
#include "Fury/Editor/EditorAssetPicker.h"
#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			// Open particle editor windows, keyed by popup ID
			// ("ParticleEditor:<system-name>"). Inserting opens
			// the window next frame; closing (X button / `open`
			// bool flipped to false) erases the entry.
			std::unordered_set<std::string> g_OpenParticleEditors;

			// Per-window preview state. Manual time slider + play
			// toggle -- decoupled from the scene clock so the
			// artist can scrub lifetime curves without playing.
			struct PreviewState
			{
				float time = 0.0f;
				bool playing = true;
				float lastWallClock = 0.0f;
			};
			std::unordered_map<std::string, PreviewState> g_PreviewState;

			PreviewState &PreviewFor(const std::string &popup_id)
			{
				return g_PreviewState.emplace(popup_id, PreviewState{}).first->second;
			}

			// 3D preview of the live ParticleSystem. Mirrors
			// RenderMeshPreview's shape but draws the system's
			// emitter + a single-tint billboard pass via the
			// shared Editor3DPreview FBO + orbit camera.
			void RenderParticlePreview(
				const std::shared_ptr<ParticleSystem> &system,
				const std::shared_ptr<ParticleRenderer> &renderer,
				const std::string &popup_id, const ImVec2 &size)
			{
				if (!system || !renderer)
				{
					ImGui::BeginChild("preview", size, true, ImGuiWindowFlags_NoScrollbar);
					ImGui::TextDisabled("(no ParticleSystem)");
					ImGui::EndChild();
					return;
				}

				const ImVec2 pad(8.0f, 8.0f);
				const int w = std::max(32,
					static_cast<int>(size.x - 2.0f * pad.x + 0.5f));
				const int h = std::max(32,
					static_cast<int>(size.y - 2.0f * pad.y + 0.5f));
				const float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.0f;

				auto &rt = EnsureRT(popup_id, w, h);
				if (rt.fbo == 0)
				{
					ImGui::BeginChild("preview", size, false, ImGuiWindowFlags_NoScrollbar);
					ImGui::TextDisabled("(3D preview - FBO incomplete)");
					ImGui::EndChild();
					return;
				}

				// Frame on a default radius (emitter AABB is not
				// directly tracked; use a fixed 1m sphere that
				// covers the wood-pile scene's emitters).
				const Vector4 aabb_center(0.0f, 0.5f, 0.0f, 1.0f);
				const float radius = 1.5f;

				OrbitState &os = OrbitFor(popup_id);
				// Reframe on first show or when the bound renderer
				// switches -- NOT every frame (a per-frame reframe
				// resets the user's wheel zoom to the initial distance).
				const bool mesh_changed =
					(os.framed_mesh != nullptr && os.framed_mesh != renderer.get());
				if (!os.initialized || mesh_changed)
					ReframeOrbit(os, aabb_center, radius, aspect, mesh_changed);
				os.framed_mesh = renderer.get();

				auto vp = ComputeViewProj(os, aabb_center, radius, aspect);

				glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
				glViewport(0, 0, w, h);
				glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				glEnable(GL_DEPTH_TEST);

				// Sync the renderer's mesh from the live pool + draw.
				// Camera axes = rows 0/1 of the view matrix (the camera's
				// right/up in world space). Explicit view/proj overload:
				// the particle shader binds camera matrices by name, so
				// a null camera would leave them at whatever the scene
				// pass last set.
				Vector4 camRight(vp.view.Raw[0], vp.view.Raw[4], vp.view.Raw[8], 0.0f);
				Vector4 camUp(vp.view.Raw[1], vp.view.Raw[5], vp.view.Raw[9], 0.0f);
				renderer->UpdateMesh(camRight, camUp);
				glEnable(GL_BLEND);
				glDepthMask(GL_FALSE);
				if (renderer->GetBlendMode() == ParticleBlend::ADDITIVE)
					glBlendFunc(GL_ONE, GL_ONE);
				else
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				renderer->Draw(vp.view, vp.proj);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);

				DrawGroundGrid(vp.view, vp.proj, aabb_center, 0.0f, radius);

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDisable(GL_DEPTH_TEST);

				ImGui::BeginChild("preview", size, false, ImGuiWindowFlags_NoScrollbar);
				ImGui::SetCursorPos(pad);
				const ImVec2 img_size(size.x - 2.0f * pad.x,
					size.y - 2.0f * pad.y);
				ImGui::Image((ImTextureID)(intptr_t)rt.colorRT->GetID(),
					img_size, ImVec2(0, 1), ImVec2(1, 0));

				const bool hovered = ImGui::IsItemHovered() || ImGui::IsWindowHovered();
				if (hovered)
				{
					const ImGuiIO &io = ImGui::GetIO();
					ApplyCameraInput(os, vp.eye, io, radius);
				}
				ImGui::EndChild();
			}

			void RenderModuleInspector(ParticleSystem *system)
			{
				if (!system) return;

				ImGui::TextDisabled("Live particles: %u / %u",
					system->GetAliveCount(), system->GetMaxParticles());

				if (ImGui::CollapsingHeader("Emission"))
				{
					float rate = system->GetEmission().rateOverTime;
					if (ImGui::DragFloat("rateOverTime", &rate, 1.0f, 0.0f, 1024.0f))
						system->GetEmission().rateOverTime = rate;

					auto &bursts = system->GetEmission().bursts;
					for (size_t i = 0; i < bursts.size(); ++i)
					{
						ImGui::PushID(static_cast<int>(i));
						ImGui::Text("Burst %zu", i);
						float t = bursts[i].time;
						int c = bursts[i].count;
						float p = bursts[i].probability;
						if (ImGui::DragFloat("time", &t, 0.01f, 0.0f, 60.0f))
							bursts[i].time = t;
						if (ImGui::DragInt("count", &c, 1, 0, 1024))
							bursts[i].count = c;
						if (ImGui::DragFloat("probability", &p, 0.05f, 0.0f, 1.0f))
							bursts[i].probability = p;
						ImGui::SameLine();
						if (ImGui::Button("-")) bursts.erase(bursts.begin() + i);
						ImGui::PopID();
					}
					if (ImGui::Button("+ Add Burst"))
					{
						Burst b;
						b.time = static_cast<float>(bursts.size());
						b.count = 10;
						bursts.push_back(b);
					}
				}

				if (ImGui::CollapsingHeader("Shape"))
				{
					auto &shape = system->GetShape();
					const char *types[] = { "BOX", "SPHERE", "CONE" };
					int t = static_cast<int>(shape.type);
					if (ImGui::Combo("type", &t, types, 3))
						shape.type = static_cast<ParticleShape>(t);

					float scale[3] = { shape.scale.x, shape.scale.y, shape.scale.z };
					if (ImGui::DragFloat3("scale", scale, 0.01f))
					{
						shape.scale.x = scale[0];
						shape.scale.y = scale[1];
						shape.scale.z = scale[2];
					}
					if (ImGui::DragFloat("radius", &shape.radius, 0.01f, 0.0f, 50.0f))
						(void)shape.radius;
					if (ImGui::DragFloat("angle", &shape.angle, 1.0f, 0.0f, 89.0f))
						(void)shape.angle;
				}

				if (ImGui::CollapsingHeader("Velocity"))
				{
					auto &v = system->GetVelocity();
					float linear[3] = { v.linear.x, v.linear.y, v.linear.z };
					if (ImGui::DragFloat3("linear", linear, 0.01f))
					{
						v.linear.x = linear[0];
						v.linear.y = linear[1];
						v.linear.z = linear[2];
					}
					if (ImGui::DragFloat("speed", &v.speed, 0.01f, 0.0f, 50.0f))
						(void)v.speed;
					bool inh = v.inheritFromParent;
					if (ImGui::Checkbox("inheritFromParent", &inh))
						v.inheritFromParent = inh;
				}

				if (ImGui::CollapsingHeader("Color Over Lifetime"))
				{
					auto &g = system->GetColorOverLifetime().color;
					ImGui::TextDisabled("(gradient -- %zu keys)", g.keys.size());
					for (size_t i = 0; i < g.keys.size(); ++i)
					{
						ImGui::PushID(static_cast<int>(i));
						float time = g.keys[i].time;
						Color c = g.keys[i].color;
						float col[4] = { c.r, c.g, c.b, c.a };
						if (ImGui::DragFloat("time", &time, 0.01f, 0.0f, 1.0f))
							g.keys[i].time = time;
						if (ImGui::ColorEdit4("color", col))
							g.keys[i].color = Color(col[0], col[1], col[2], col[3]);
						ImGui::SameLine();
						if (ImGui::Button("-")) g.keys.erase(g.keys.begin() + i);
						ImGui::PopID();
					}
					if (ImGui::Button("+ Add Key"))
						g.keys.push_back({ 1.0f, Color::White });
				}

				if (ImGui::CollapsingHeader("Size Over Lifetime"))
				{
					auto &c = system->GetSizeOverLifetime().size;
					ImGui::TextDisabled("(curve -- %zu keys)", c.keys.size());
					for (size_t i = 0; i < c.keys.size(); ++i)
					{
						ImGui::PushID(1000 + static_cast<int>(i));
						float time = c.keys[i].time;
						float val = c.keys[i].value;
						if (ImGui::DragFloat("time", &time, 0.01f, 0.0f, 1.0f))
							c.keys[i].time = time;
						if (ImGui::DragFloat("value", &val, 0.05f, 0.0f, 10.0f))
							c.keys[i].value = val;
						ImGui::SameLine();
						if (ImGui::Button("-")) c.keys.erase(c.keys.begin() + i);
						ImGui::PopID();
					}
					if (ImGui::Button("+ Add Key"))
						c.keys.push_back({ 1.0f, 1.0f });
				}

				if (ImGui::CollapsingHeader("Rotation Over Lifetime"))
				{
					float av = system->GetRotationOverLifetime().angularVelocity;
					if (ImGui::DragFloat("angularVelocity", &av, 1.0f, -720.0f, 720.0f))
						system->GetRotationOverLifetime().angularVelocity = av;
				}

				if (ImGui::CollapsingHeader("Renderer"))
				{
					auto &r = system->GetRenderer();
					const char *modes[] = { "ALPHA", "ADDITIVE" };
					int m = static_cast<int>(r.blendMode);
					if (ImGui::Combo("blendMode", &m, modes, 2))
						r.blendMode = static_cast<ParticleBlend>(m);
					bool rs = r.receiveShadows;
					if (ImGui::Checkbox("receiveShadows", &rs))
						r.receiveShadows = rs;
					ImGui::SameLine();
					ImGui::TextDisabled("(ALPHA only)");
					ImGui::TextDisabled("material: %s",
						r.materialName.empty() ? "(none)" : r.materialName.c_str());
					if (ImGui::Button("Browse Material..."))
						ImGui::OpenPopup("ParticleMaterialPicker");
					RenderAssetPickerModal("ParticleMaterialPicker", "Pick Material",
						typeid(Material),
						[system](std::shared_ptr<void> picked)
						{
							auto mat = std::static_pointer_cast<Material>(picked);
							if (mat && system) system->GetRenderer().materialName = mat->GetName();
						});
				}

				ImGui::Separator();
				ImGui::TextDisabled("Lifetime: %.2fs   StartSize: %.2f",
					system->GetLifetime(), system->GetStartSize());
				float lt = system->GetLifetime();
				float ss = system->GetStartSize();
				if (ImGui::DragFloat("lifetime", &lt, 0.05f, 0.05f, 60.0f))
					system->SetLifetime(lt);
				if (ImGui::DragFloat("startSize", &ss, 0.01f, 0.0f, 10.0f))
					system->SetStartSize(ss);
			}
		} // namespace

		void RenderParticleEditorWindow(
			const std::shared_ptr<ParticleSystem> &system, bool *p_open)
		{
			if (!system) return;

			ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
			ImGuiViewport *vp = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->GetCenter().y),
				ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

			std::string title = "Particle: " + system->GetName();
			if (!ImGui::Begin(title.c_str(), p_open))
			{
				ImGui::End();
				return;
			}

			ImGui::TextDisabled("ParticleSystem: %s", system->GetName().c_str());
			ImGui::Separator();

			std::string popup_id = "ParticleEditor:" + system->GetName();

			// Two-pane layout: viewer (~65%) + inspector (~35%).
			ImVec2 avail = ImGui::GetContentRegionAvail();
			const float inspector_w = std::max(280.0f, avail.x * 0.35f);
			const float viewer_w = std::max(120.0f, avail.x - inspector_w - 8.0f);

			// Bound ParticleRenderer: find one anywhere in the scene that
			// references this system (by name) -- drives the preview's
			// material binding + blend mode.
			auto renderer = std::shared_ptr<ParticleRenderer>();
			if (Scene::Active)
			{
				auto root = Scene::Active->GetRootNode();
				if (root)
				{
					std::function<void(const std::shared_ptr<SceneNode>&)> find = nullptr;
					find = [&](const std::shared_ptr<SceneNode> &node)
					{
						if (!node || renderer) return;
						auto pr = node->GetComponent<ParticleRenderer>();
						if (pr && pr->GetSystemName() == system->GetName())
							renderer = pr;
						for (unsigned int i = 0; i < node->GetChildCount(); ++i)
							find(node->GetChildAt(i));
					};
					find(root);
				}
			}

			// Preview control row: play/pause + time slider.
			PreviewState &ps = PreviewFor(popup_id);
			if (ImGui::Button(ps.playing ? "[Pause]" : "[Play]"))
				ps.playing = !ps.playing;
			ImGui::SameLine();
			if (ImGui::Button("[Reset]"))
			{
				system->Reset();
				ps.time = 0.0f;
			}
			ImGui::SameLine();
			ImGui::PushItemWidth(200.0f);
			ImGui::SliderFloat("preview time", &ps.time, 0.0f, 10.0f);
			ImGui::PopItemWidth();

			// Drive the simulation in-place: when playing, advance by
			// wall-clock dt; otherwise stay frozen at the slider's
			// timestamp (the user can scrub curves).
			const float now = static_cast<float>(ImGui::GetTime());
			if (ps.lastWallClock == 0.0f) ps.lastWallClock = now;
			const float wallDt = static_cast<float>(now - ps.lastWallClock);
			ps.lastWallClock = now;
			if (ps.playing)
			{
				system->Update(wallDt);
				ps.time += wallDt;
			}
			else
			{
				// Reset to t=0 + substep up to the slider's preview
				// time. One giant Update(t) would leave every particle
				// at age 0 (spawning happens at the end of Update), so
				// the seek integrates in fixed 1/30 steps -- bounded,
				// deterministic, cheap at the 4096 cap.
				system->Reset();
				const float h = 1.0f / 30.0f;
				float remaining = ps.time;
				int guard = 600; // 20s of substeps max
				while (remaining > 1e-6f && guard-- > 0)
				{
					const float step = std::min(h, remaining);
					system->Update(step);
					remaining -= step;
				}
			}

			RenderParticlePreview(system, renderer, popup_id, ImVec2(viewer_w, avail.y - 32.0f));

			ImGui::SameLine();
			ImGui::BeginChild("inspector", ImVec2(inspector_w, avail.y - 32.0f), true);
			RenderModuleInspector(system.get());
			ImGui::EndChild();

			ImGui::End();
		}

		void OpenParticleEditor(const std::shared_ptr<ParticleSystem> &system)
		{
			if (!system) return;
			std::string popup = "ParticleEditor:" + system->GetName();
			g_OpenParticleEditors.insert(popup);
			// The preview drives Update() manually from here on --
			// suspend the wall-clock tick until the window closes.
			system->SetExternallyDriven(true);
		}

		void RenderAllOpenParticleEditors()
		{
			std::vector<std::string> closed;
			for (const auto &popup : g_OpenParticleEditors)
			{
				bool open = true;
				auto pos = popup.find(':');
				std::string name = (pos != std::string::npos)
					? popup.substr(pos + 1) : popup;
				// ParticleSystem is a top-level asset -- resolve via
				// the active scene's EntityManager.
				std::shared_ptr<ParticleSystem> system;
				if (Scene::Active)
					if (auto em = Scene::Active->GetEntityManager())
						system = em->Get<ParticleSystem>(name);
				if (system)
				{
					// Idempotent per-frame re-set: covers a scene swap
					// under an open window (the new same-named system
					// never saw OpenParticleEditor).
					system->SetExternallyDriven(true);
					RenderParticleEditorWindow(system, &open);
				}
				if (!open)
				{
					closed.push_back(popup);
					if (system) system->SetExternallyDriven(false);
				}
			}
			for (const auto &popup : closed) g_OpenParticleEditors.erase(popup);
		}
	}
}

#endif // WITH_EDITOR