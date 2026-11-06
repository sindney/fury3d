// Editor UI automation host. Compiled only into furye-ui-tests (the target
// defines FURY_UI_TEST_HOST); production binaries never see this file.

#if defined(FURY_UI_TEST_HOST) && WITH_EDITOR

#include "Fury/Editor/UiTestHost.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "imgui_te_exporters.h"
#include "imgui_te_internal.h" // ImGuiTestRunTask definition

#include "Fury/Color.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Editor/Editor3DPreview.h"
#include "Fury/Editor/EditorGpuProfilerWindow.h"
#include "Fury/Editor/EditorRenderJobs.h"
#include "Fury/EntityManager.h"
#include "Fury/Gui.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderSettings.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/SceneManager.h"
#include "Fury/Transform.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>

namespace fury
{
	namespace UiTestHost
	{
		namespace
		{
			ImGuiTestEngine *s_Engine = nullptr;

			bool s_Active = false;
			bool s_Ran = false;
			bool s_Started = false;      // engine started + tests queued
			bool s_Finished = false;     // results exported, window closing
			bool s_FixtureReady = false; // fixture exists in the active scene
			bool s_QueuePerfGroup = false;
			int s_ExitCode = 0;
			int s_FailureSettleFrames = 0;

			std::string s_Filter;
			std::string s_ArtifactDir = "ui_test_artifacts";
			std::string s_JunitPath;
			std::string s_IniPath;
			float s_TimeoutSeconds = 120.0f;
			std::chrono::steady_clock::time_point s_StartTime;

			std::deque<std::string> s_OpenDialogResults;
			std::deque<std::string> s_SaveDialogResults;

			// Fixture asset identities (stable names; never written to disk).
			const char *kSubjectName = "UiTestSubject";   // persisted on save
			const char *kTargetName = "SmokeTarget";      // editor-only
			const char *kMeshPath = "uitest_cube.mesh";   // content-root asset

			// Exit codes: failed test 1, empty selection 2, timeout 3.
			constexpr int kExitFailed = 1;
			constexpr int kExitEmpty = 2;
			constexpr int kExitTimeout = 3;

			bool MatchArg(const char *a, const char *name, const char **value, int argc, char **argv, int &i)
			{
				const size_t n = std::strlen(name);
				if (std::strncmp(a, name, n) != 0)
					return false;
				if (a[n] == '=')
				{
					*value = a + n + 1;
					return true;
				}
				if (a[n] == '\0' && i + 1 < argc)
				{
					*value = argv[++i];
					return true;
				}
				return false;
			}

			std::string IniPath() { return s_ArtifactDir + "/imgui.ini"; }
			std::string DefaultJunitPath() { return s_ArtifactDir + "/junit.xml"; }

			std::shared_ptr<SceneNode> FindNode(const char *name)
			{
				if (!Scene::Active || !Scene::Active->GetRootNode())
					return nullptr;
				return Scene::Active->GetRootNode()->FindChildRecursively(name);
			}

			void RegisterWithSceneManager(const std::shared_ptr<SceneNode> &node)
			{
				if (Scene::Active && Scene::Active->GetSceneManager())
					Scene::Active->GetSceneManager()->AddSceneNodeRecursively(node);
			}

			std::shared_ptr<SceneNode> MakeFixtureNode(const char *name, bool editorOnly,
				const Vector4 &pos)
			{
				auto node = SceneNode::Create(name);
				node->SetEditorOnly(editorOnly);
				node->SetLocalPosition(pos);
				node->Recompose(false);
				node->AddComponent(Transform::Create());
				auto light = Light::Create();
				light->SetType(LightType::POINT);
				light->SetColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
				light->SetIntensity(editorOnly ? 1.0f : 7.0f);
				light->SetCastShadows(true);
				light->CalculateAABB();
				node->AddComponent(light);
				node->Recompose(true);
				return node;
			}

			// Deterministic cube + quad LOD chain registered as a content
			// asset so the Content Browser / mesh editor have real content.
			std::shared_ptr<Mesh> MakeFixtureMesh()
			{
				auto mesh = MeshUtil::CreateCube(kMeshPath,
					Vector4(-1.0f, -1.0f, -1.0f, 1.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
				auto lod1 = MeshUtil::CreateQuad(std::string(kMeshPath) + "#lod1",
					Vector4(-1.0f, 0.0f, -1.0f, 1.0f), Vector4(1.0f, 0.0f, 1.0f, 1.0f));
				mesh->SetLodMeshes({lod1}, {0.5f});
				return mesh;
			}

			// Fixture pieces are re-added whenever they go missing (File ->
			// Open replaces the scene), so tests can rely on them existing.
			void EnsureFixture()
			{
				if (!Scene::Active || !Scene::Active->GetRootNode())
					return;
				auto root = Scene::Active->GetRootNode();
				if (!FindNode(kTargetName))
				{
					auto node = MakeFixtureNode(kTargetName, true, Vector4(10.0f, 20.0f, 30.0f, 1.0f));
					root->AddChild(node);
					RegisterWithSceneManager(node);
					FURYI << "[uitest] fixture node SmokeTarget created";
				}
				if (!FindNode(kSubjectName))
				{
					auto node = MakeFixtureNode(kSubjectName, false, Vector4(1.0f, 2.0f, 3.0f, 1.0f));
					root->AddChild(node);
					RegisterWithSceneManager(node);
					FURYI << "[uitest] fixture node UiTestSubject created";
				}
				if (auto em = Scene::Active->GetEntityManager())
				{
					if (!em->Get<Mesh>(kMeshPath))
					{
						em->Add<Mesh>(MakeFixtureMesh());
						FURYI << "[uitest] fixture mesh uitest_cube.mesh registered";
					}
				}
				s_FixtureReady = true;
			}

			void ExportResults()
			{
				if (s_JunitPath.empty())
					return;
				ImGuiTestEngine_ExportEx(s_Engine, ImGuiTestEngineExportFormat_JUnitXml, s_JunitPath.c_str());
				FURYI << "[uitest] JUnit written to " << s_JunitPath;
			}

			void Finish(int exitCode)
			{
				if (s_Finished)
					return;
				s_Finished = true;
				s_ExitCode = exitCode;
				if (s_Started)
				{
					// ImGuiTestEngine_Stop is deferred to Shutdown(): Stop joins and nulls the
					// test coroutine, but the engine's EndFrame hook keeps
					// resuming it on ImGui::Render calls this frame and
					// crashes. The empty queue is inert, so letting the
					// coroutine idle until shutdown is safe.
					ImGuiTestEngine_PrintResultSummary(s_Engine);
					ExportResults();
				}
				std::printf("[uitest] result: exit=%d filter='%s' junit='%s'\n",
					s_ExitCode, s_Filter.c_str(), s_JunitPath.c_str());
				std::fflush(stdout);
				// Unattended exit: close the window; the main loop unwinds and
				// Engine::Shutdown tears everything down in order.
				Gui::CloseWindow();
			}

			// Failure captures land in a cwd-relative "output/" tree; relocate
			// them into the artifact dir so runs never leave files in the
			// working directory.
			void RelocateCaptures()
			{
				std::error_code ec;
				if (!std::filesystem::exists("output", ec))
					return;
				const std::string dest = s_ArtifactDir + "/captures";
				std::filesystem::create_directories(dest, ec);
				for (std::filesystem::recursive_directory_iterator it("output", ec), end;
					 !ec && it != end; it.increment(ec))
				{
					if (!it->is_regular_file(ec))
						continue;
					const std::string target = dest + "/" + it->path().filename().string();
					std::filesystem::rename(it->path(), target, ec);
					if (ec)
					{
						ec.clear();
						std::filesystem::copy_file(it->path(), target, ec);
						std::filesystem::remove(it->path(), ec);
					}
				}
				std::filesystem::remove_all("output", ec);
			}

			// Reads the whole backbuffer and copies the requested region with
			// a vertical flip (GL origin is bottom-left; ImGui capture is
			// top-left). Runs on the presentation thread inside PostSwap.
			bool ScreenCaptureRHI(ImGuiID, int x, int y, int w, int h,
				unsigned int *pixels, void *)
			{
				if (GRHIDevice == nullptr || x < 0 || w <= 0 || h <= 0)
					return false;
				RHIPixelData data;
				if (!GRHIDevice->ReadbackBackbuffer(data) || data.Width <= 0 || data.Height <= 0)
					return false;
				for (int row = 0; row < h; ++row)
				{
					const int srcY = data.Height - 1 - (y + row);
					const int copyW = x + w <= data.Width ? w : data.Width - x;
					if (srcY < 0 || srcY >= data.Height || copyW <= 0)
						continue;
					std::memcpy(reinterpret_cast<unsigned char *>(pixels) + static_cast<size_t>(row) * w * 4,
						data.Pixels.data() +
							(static_cast<size_t>(srcY) * data.Width + x) * 4,
						static_cast<size_t>(copyW) * 4);
				}
				return true;
			}

			// ----- tests ---------------------------------------------------

			// Finds an ImGui child window ("Parent/Prefix_HASH") by its parent
			// window and id prefix. Item refs cannot traverse child scopes, so
			// tests re-base onto the child window.
			ImGuiWindow *FindChildWindow(const char *parentName, const char *prefix)
			{
				const std::string head = std::string(parentName) + "/" + prefix;
				for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
					if (strncmp(w->Name, head.c_str(), head.size()) == 0)
						return w;
				return nullptr;
			}

			// Selects the fixture node through the real hierarchy row and edits
			// a component value through the real inspector widget, verifying
			// both the editor state and the underlying model values.
			void TestSelectAndEditKnownNode(ImGuiTestContext *ctx)
			{
				IM_CHECK(Scene::Active != nullptr);
				auto node = FindNode(kTargetName);
				IM_CHECK(node != nullptr);

				// Selection through the hierarchy row's stable ID. Rows are
				// scoped by the tree structure (see NodeAutomationId).
				ctx->WindowFocus("//Scene Inspector");
				ctx->SetRef("Scene Inspector");
				ctx->ItemClick("hier:RootNode/hier:SmokeTarget");
				ctx->Yield(2);
				IM_CHECK(Editor::GetSelectedSceneNode() == node.get());

				// Edit through the inspector widget; the model must follow.
				ctx->WindowFocus("//Node Properties");
				ctx->SetRef("Node Properties");
				ctx->ItemInputValue("Light/Intensity", 2.5f);
				ctx->Yield(2);
				auto light = node->GetComponent<Light>();
				IM_CHECK(light != nullptr);
				IM_CHECK(light->GetIntensity() == 2.5f);

				// The widget must display the committed value.
				float shown = 0.0f;
				IM_CHECK(ctx->ItemReadAsScalar("Light/Intensity", ImGuiDataType_Float, &shown));
				IM_CHECK(shown == 2.5f);
			}

			// Plain / ctrl / shift row selection, inspector contents, and
			// selection state reset after the scene is replaced.
			void TestHierarchySelection(ImGuiTestContext *ctx)
			{
				auto target = FindNode(kTargetName);
				auto subject = FindNode(kSubjectName);
				IM_CHECK(target != nullptr && subject != nullptr);
				auto root = Scene::Active->GetRootNode();
				IM_CHECK(root != nullptr);

				ctx->WindowFocus("//Scene Inspector");
				ctx->SetRef("Scene Inspector");

				// Plain click: single selection, anchor = clicked row.
				ctx->ItemClick("hier:RootNode/hier:SmokeTarget");
				ctx->Yield(2);
				IM_CHECK(Editor::GetSelectedSceneNode() == target.get());
				IM_CHECK(Editor::GetSelectionSet().members.size() == 1);

				// Inspector shows the selected node's sections. The Node TRS
				// row is a DragFloat3: per-component items at $$0/$$1/$$2.
				ctx->WindowFocus("//Node Properties");
				ctx->SetRef("Node Properties");
				IM_CHECK(ctx->ItemExists("Node"));
				IM_CHECK(ctx->ItemExists("ins:node/Position/$$1"));
				IM_CHECK(ctx->ItemExists("Light/Intensity"));
				IM_CHECK(ctx->ItemExists("Transform/Position/$$1"));
				ctx->WindowFocus("//Scene Inspector");
				ctx->SetRef("Scene Inspector");

				// Ctrl+click adds to the set without dropping the anchor row.
				ctx->KeyDown(ImGuiMod_Ctrl);
				ctx->ItemClick("hier:RootNode/hier:UiTestSubject");
				ctx->KeyUp(ImGuiMod_Ctrl);
				ctx->Yield(2);
				IM_CHECK(Editor::GetSelectionSet().members.size() == 2);
				IM_CHECK(Editor::GetSelectedSceneNode() == subject.get());

				// Ctrl+click on the same row toggles it back out.
				ctx->KeyDown(ImGuiMod_Ctrl);
				ctx->ItemClick("hier:RootNode/hier:UiTestSubject");
				ctx->KeyUp(ImGuiMod_Ctrl);
				ctx->Yield(2);
				IM_CHECK(Editor::GetSelectionSet().members.size() == 1);

				// Shift+click selects the contiguous visible range between the
				// anchor and the clicked row (both directions).
				ctx->ItemClick("hier:RootNode/hier:UiTestSubject");
				ctx->Yield(2);
				ctx->KeyDown(ImGuiMod_Shift);
				ctx->ItemClick("hier:RootNode/hier:SmokeTarget");
				ctx->KeyUp(ImGuiMod_Shift);
				ctx->Yield(2);
				const auto &range = Editor::GetSelectionSet();
				IM_CHECK(range.members.size() == 2);

				// A final plain click collapses back to a single selection.
				ctx->ItemClick("hier:RootNode/hier:SmokeTarget");
				ctx->Yield(2);
				IM_CHECK(Editor::GetSelectedSceneNode() == target.get());
				IM_CHECK(Editor::GetSelectionSet().members.size() == 1);

				// Same-named siblings keep independent row identity and open state.
				auto first = MakeFixtureNode("UiTestDuplicate", true, Vector4(0.0f, 0.0f, 0.0f, 1.0f));
				auto second = MakeFixtureNode("UiTestDuplicate", true, Vector4(1.0f, 0.0f, 0.0f, 1.0f));
				first->AddChild(SceneNode::Create("FirstChild"));
				second->AddChild(SceneNode::Create("SecondChild"));
				root->AddChild(first);
				root->AddChild(second);
				RegisterWithSceneManager(first);
				RegisterWithSceneManager(second);
				ctx->Yield(2);
				const std::string firstId = "hier:RootNode/hier:UiTestDuplicate[" + first->GetUUID() + "]";
				const std::string secondId = "hier:RootNode/hier:UiTestDuplicate[" + second->GetUUID() + "]";
				ctx->ItemOpen(firstId.c_str());
				ctx->Yield(2);
				IM_CHECK(Editor::IsNodeOpen(first.get()));
				IM_CHECK(!Editor::IsNodeOpen(second.get()));
				ctx->ItemOpen(secondId.c_str());
				ctx->Yield(2);
				IM_CHECK(Editor::IsNodeOpen(first.get()));
				IM_CHECK(Editor::IsNodeOpen(second.get()));
				ctx->ItemClose(firstId.c_str());
				ctx->Yield(2);
				IM_CHECK(!Editor::IsNodeOpen(first.get()));
				IM_CHECK(Editor::IsNodeOpen(second.get()));
				ctx->ItemClick(secondId.c_str());
				ctx->Yield(2);
				IM_CHECK(Editor::GetSelectedSceneNode() == second.get());
				first->RemoveFromParent();
				second->RemoveFromParent();
				ctx->Yield(2);
			}

			// Commits a transform and a component edit, verifies live model
			// values + dirty state, and exercises the input widget's built-in undo.
			void TestInspectorEditDirtyUndo(ImGuiTestContext *ctx)
			{
				auto node = FindNode(kTargetName);
				IM_CHECK(node != nullptr);

				ctx->WindowFocus("//Scene Inspector");
				ctx->SetRef("Scene Inspector");
				ctx->ItemClick("hier:RootNode/hier:SmokeTarget");
				ctx->Yield(2);

				Editor::ClearSceneDirty();
				IM_CHECK(!Editor::IsSceneDirty());

				// Component edit: committed value lands in the model, the
				// widget shows it, and the scene is marked dirty.
				ctx->WindowFocus("//Node Properties");
				ctx->SetRef("Node Properties");
				ctx->ItemInputValue("Light/Intensity", 3.25f);
				ctx->Yield(2);
				auto light = node->GetComponent<Light>();
				IM_CHECK(light != nullptr);
				IM_CHECK(light->GetIntensity() == 3.25f);
				float shown = 0.0f;
				IM_CHECK(ctx->ItemReadAsScalar("Light/Intensity", ImGuiDataType_Float, &shown));
				IM_CHECK(shown == 3.25f);
				IM_CHECK(Editor::IsSceneDirty());

				// Transform edit through the Node TRS widget. DragFloat3
				// components are separate items; $$1 is the Y field.
				ctx->ItemInput("ins:node/Position/$$1");
				ctx->KeyCharsReplaceEnter("25");
				ctx->Yield(2);
				IM_CHECK(node->GetLocalPosition().y == 25.0f);
				IM_CHECK(Editor::IsSceneDirty());

				// Supported undo: while the field is still in edit mode, Ctrl+Z
				// steps back through the edit records (typed char, then the
				// replace-selection delete) to the value at activation.
				ctx->ItemInput("ins:node/Position/$$1");
				ctx->KeyCharsReplace("9");
				ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
				ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
				ctx->KeyPress(ImGuiKey_Enter);
				ctx->Yield(2);
				IM_CHECK(node->GetLocalPosition().y == 25.0f);
			}

			// Drives the clustered setting and debug selector through real editor widgets.
			void TestClusterSettingsDebug(ImGuiTestContext *ctx)
			{
				IM_CHECK(Pipeline::Active != nullptr);
				auto settings = Scene::Active ? Scene::Active->GetRenderSettings() : nullptr;
				IM_CHECK(settings != nullptr);
				auto mesh = Scene::Active->GetEntityManager()->Get<Mesh>(kMeshPath);
				IM_CHECK(mesh != nullptr);
				auto material = Material::Create("uitest_cluster_surface.mat");
				Scene::Active->GetEntityManager()->Add(material);
				auto surface = SceneNode::Create("UiTestClusterSurface");
				surface->SetEditorOnly(true);
				surface->SetLocalScale(Vector4(100.0f, 100.0f, 100.0f, 1.0f));
				surface->AddComponent(Transform::Create());
				surface->AddComponent(MeshRender::Create(material, mesh));
				Scene::Active->GetRootNode()->AddChild(surface);
				surface->Recompose(true);
				RegisterWithSceneManager(surface);
				ctx->Yield(4);
				settings->SetClusteredLighting(false);
				Pipeline::Active->ApplyRenderSettings(*settings);
				Editor::ClearSceneDirty();

				Editor::SetWindowVisible("Settings", true);
				ctx->Yield(2);
				ctx->WindowFocus("//Settings");
				ctx->SetRef("Settings");
				ctx->ItemOpen("Engine");
				ctx->ItemClick("Clustered Lighting");
				ctx->Yield(2);
				IM_CHECK(settings->IsClusteredLighting());
				IM_CHECK(Editor::IsSceneDirty());
				if (GRHIDevice && GRHIDevice->GetBackend() == RHIBackend::OPENGL)
				{
					ctx->Yield(4);
					IM_CHECK(!Pipeline::Active->IsClusteredLightingEffective());
					IM_CHECK(Pipeline::Active->GetClusteredLightingDiagnostic()[0] != '\0');
				}

				Editor::ClearSceneDirty();
				ctx->WindowFocus("//Viewport");
				ctx->SetRef("Viewport");
				ctx->ItemClick("##debug_views");
				ctx->MenuClick("//$FOCUSED/Clusters/Cluster Grid");
				ctx->Yield(4);
				IM_CHECK(Pipeline::Active->IsSwitchOn(PipelineSwitch::CLUSTER_GRID_VIEW));
				IM_CHECK(!Editor::IsSceneDirty());
				ImGui::ClosePopupToLevel(0, true);
				ctx->Yield(2);
				ctx->CaptureScreenshotWindow("//Viewport", ImGuiCaptureFlags_HideMouseCursor);

				ctx->ItemClick("##debug_views");
				ctx->MenuClick("//$FOCUSED/Clusters/Cluster Light Count");
				ctx->Yield(4);
				IM_CHECK(Pipeline::Active->IsSwitchOn(PipelineSwitch::CLUSTER_LIGHT_COUNT_VIEW));
				IM_CHECK(!Editor::IsSceneDirty());
				ImGui::ClosePopupToLevel(0, true);
				ctx->Yield(2);
				ctx->CaptureScreenshotWindow("//Viewport", ImGuiCaptureFlags_HideMouseCursor);

				ctx->ItemClick("##debug_views");
				ctx->MenuClick("//$FOCUSED/Clusters/Off");
				ctx->Yield(2);
				IM_CHECK(!Pipeline::Active->IsSwitchOn(PipelineSwitch::CLUSTER_GRID_VIEW));
				IM_CHECK(!Pipeline::Active->IsSwitchOn(PipelineSwitch::CLUSTER_LIGHT_COUNT_VIEW));
				IM_CHECK(!Editor::IsSceneDirty());
				ImGui::ClosePopupToLevel(0, true);
				ctx->Yield(2);
			}

			// Drives Content Browser filtering into the mesh editor: metadata,
			// LOD selection, and orbit-camera controls.
			void TestContentBrowserMeshPreview(ImGuiTestContext *ctx)
			{
				if (auto em = Scene::Active ? Scene::Active->GetEntityManager() : nullptr)
					IM_CHECK(em->Get<Mesh>(kMeshPath) != nullptr);

				Editor::SetWindowVisible("ContentBrowser", true);
				ctx->WindowFocus("//Content Browser");
				ctx->Yield(2);
				ctx->SetRef("Content Browser");

				// Type filter + fuzzy search narrow the grid to the fixture.
				ctx->ComboClick("##cb_type/Mesh");
				ctx->Yield(2);
				// Tiles render inside the "##cb_tiles" child window; item refs
				// cannot traverse child scopes, so gather from the child
				// (clip-independent) and look for the tile's button ID.
				ImGuiWindow *tilesChild = FindChildWindow("Content Browser", "##cb_tiles");
				IM_CHECK(tilesChild != nullptr);
				const ImGuiID tileScope = ImHashStr(
					(std::string("cb:Mesh:") + kMeshPath).c_str(), 0, tilesChild->ID);
				const ImGuiID tileBtn = ImHashStr("##cb_tile", 0, tileScope);
				const ImGuiID thumbBtn = ImHashStr("##cb_thumb", 0, tileScope);
				auto tileVisible = [&]() -> bool {
					ImGuiTestItemList items;
					ctx->GatherItems(&items, ImGuiTestRef(tilesChild->ID), 3);
					return items.GetByID(tileBtn) != nullptr;
				};
				IM_CHECK(tileVisible());
				ctx->ItemInputValue("##cb_search", "cube");
				ctx->Yield(2);
				IM_CHECK(tileVisible());
				ctx->ItemInputValue("##cb_search", "zzzz");
				ctx->Yield(2);
				IM_CHECK(!tileVisible());
				ctx->ItemInputValue("##cb_search", "");
				ctx->Yield(2);
				IM_CHECK(tileVisible());

				// Double-click opens the mesh editor on the expected asset.
				// ##cb_thumb overlaps ##cb_tile, so drive the thumb button.
				// Scroll it into view first (double-click needs hover).
				ctx->ScrollToItem(ImGuiTestRef(thumbBtn), ImGuiAxis_Y);
				ctx->ItemDoubleClick(ImGuiTestRef(thumbBtn));
				ctx->Yield(3);
				const std::string meshWin = std::string("Mesh: uitest_cube.mesh###") + kMeshPath;
				IM_CHECK(ImGui::FindWindowByName(meshWin.c_str()) != nullptr);
				ctx->SetRef(meshWin.c_str());

				// Metadata + renderable content from the model.
				if (auto em = Scene::Active->GetEntityManager())
				{
					auto mesh = em->Get<Mesh>(kMeshPath);
					IM_CHECK(mesh != nullptr);
					IM_CHECK(mesh->GetLodCount() == 2);
					IM_CHECK(!mesh->Positions.Data.empty());
					IM_CHECK(mesh->GetLodMesh(1) != nullptr);
				}

				// LOD dropdown: selecting LOD 1 switches the preview override.
				// The combo lives in the "metadata" child window; re-base the
				// ref onto it (item paths cannot traverse child scopes).
				ImGuiWindow *meta = FindChildWindow(meshWin.c_str(), "metadata");
				IM_CHECK(meta != nullptr);
				ctx->SetRef(meta);
				IM_CHECK(ctx->ItemExists("##mesh_lod"));
				Editor::OrbitState &orbit = Editor::OrbitFor(std::string("MeshEditor:") + kMeshPath);
				ctx->ItemClick("##mesh_lod");
				ctx->Yield(2);
				ctx->ItemClick("//$FOCUSED/LOD 1");
				ctx->Yield(2);
				IM_CHECK(orbit.preview_lod_override == 1);
				ctx->SetRef(meshWin.c_str());

				// Preview camera: LMB drag orbits, wheel zooms. Aim at the
				// viewer pane (left ~70% of the window) by coordinates.
				const float yaw0 = orbit.yaw;
				const float dist0 = orbit.distance;
				ImGuiWindow *meshWinPtr = ImGui::FindWindowByName(meshWin.c_str());
				IM_CHECK(meshWinPtr != nullptr);
				ctx->MouseTeleportToPos(ImVec2(
					meshWinPtr->Pos.x + meshWinPtr->Size.x * 0.35f,
					meshWinPtr->Pos.y + meshWinPtr->Size.y * 0.5f));
				ctx->Yield(1);
				ctx->MouseDragWithDelta(ImVec2(80.0f, 0.0f));
				ctx->Yield(2);
				IM_CHECK(orbit.yaw != yaw0);
				ctx->MouseWheelY(-2.0f);
				ctx->Yield(2);
				IM_CHECK(orbit.distance != dist0);

				// Per-test cleanup: close the editor, reset the LOD override
				// and the browser filters.
				orbit.preview_lod_override = -1;
				ImGuiWindow *mwClose = ImGui::FindWindowByName(meshWin.c_str());
				IM_CHECK(mwClose != nullptr);
				ctx->ItemClick(ImGuiTestRef(mwClose->GetID("#CLOSE")));
				ctx->Yield(2);
				ctx->SetRef("Content Browser");
				ctx->ComboClick("##cb_type/All");
				ctx->ItemInputValue("##cb_search", "");
				ctx->Yield(2);
			}

			// Visits the Perf/GBuffer/Shadows tabs and loads the minimal GPU
			// graph fixture; asserts populated data, never timings.
			void TestProfilerTabs(ImGuiTestContext *ctx)
			{
				Editor::SetWindowVisible("Profiler", true);
				ctx->Yield(2); // window is closed by default; let it appear
				ctx->WindowFocus("//Profiler");
				ctx->Yield(1);
				ImGuiWindow *profWin = ImGui::FindWindowByName("Profiler");
				IM_CHECK(profWin != nullptr);
				// Tab items live under the tab-bar scope, which item path
				// refs cannot traverse; address them by computed ID.
				const ImGuiID barId = profWin->GetID("ProfilerTabs");
				const ImGuiID tabPerf = ImHashStr("prof:perf", 0, barId);
				const ImGuiID tabGBuffer = ImHashStr("prof:gbuffer", 0, barId);
				const ImGuiID tabShadows = ImHashStr("prof:shadows", 0, barId);
				ctx->SetRef(profWin);

				// Perf: the camera-readout Copy button and the per-run
				// counters are populated (PlotLines is draw-only, no item).
				ctx->ItemClick(ImGuiTestRef(tabPerf));
				ctx->Yield(2);
				IM_CHECK(ctx->ItemExists("**/Copy"));
				IM_CHECK(RenderUtil::Instance()->GetDrawCall() > 0);

				// GBuffer: the LDR pipeline's named targets render preview
				// blits onto published editor surfaces. Image widgets use
				// id 0 (invisible to the test engine), so verify the
				// published preview texture instead of an item.
				ctx->ItemClick(ImGuiTestRef(tabGBuffer));
				ctx->Yield(5);
				IM_CHECK(Editor::DisplayTextureId("GBufferPreview:gbuffer_depth") != 0);
				IM_CHECK(Editor::DisplayTextureId("GBufferPreview:gbuffer_normal") != 0);

				// Shadows: two fixture point lights cast, so the light picker
				// and shadow map sections render.
				ctx->ItemClick(ImGuiTestRef(tabShadows));
				ctx->Yield(3);
				IM_CHECK(Scene::Active && Scene::Active->GetRootNode());
				const ImGuiID shadowPicker = ImHashStr("##shadow_light_picker", 0, tabShadows);
				IM_CHECK(ctx->ItemExists(ImGuiTestRef(shadowPicker)));

				Editor::SetWindowVisible("Profiler", false);
			}

			// Opens the GPU capture viewer on the checked-in minimal fixture
			// and verifies render-graph + texture-window content.
			void TestGpuGraphCapture(ImGuiTestContext *ctx)
			{
				std::error_code ec;
				const std::filesystem::path fixture =
					std::filesystem::absolute(std::filesystem::path("../tests/lua/_fixtures/gpu_dump_min"), ec);
				IM_CHECK(!ec && std::filesystem::exists(fixture / "rendergraph.json", ec));

				Editor::OpenGpuProfilerWindow(fixture.lexically_normal().string());
				ctx->Yield(8); // node editor layout settle frames
				ImGuiWindow *gpuWin = ImGui::FindWindowByName("GPU Profiler");
				IM_CHECK(gpuWin != nullptr);
				ctx->SetRef(gpuWin);
				// Tab items live under the tab-bar scope; address by ID.
				const ImGuiID barId = gpuWin->GetID("gpu_profiler_tabs");
				const ImGuiID tabGraph = ImHashStr("Render Graph", 0, barId);
				const ImGuiID tabTex = ImHashStr("Textures", 0, barId);

				// Render Graph tab (default): details pane content from the
				// capture; formatting exercises the graph layout path.
				ctx->ItemClick(ImGuiTestRef(tabGraph));
				ctx->Yield(1);
				ImGuiWindow *graphDetails = FindChildWindow("GPU Profiler", "gpu_profiler_details");
				IM_CHECK(graphDetails != nullptr);
				ctx->SetRef(graphDetails);
				IM_CHECK(ctx->ItemExists("Format graph"));
				ctx->ItemClick("Format graph");
				ctx->Yield(2);
				ctx->SetRef(gpuWin);

				// Textures tab: rows live in the ScrollY table's child window (rows
				// are PushID(index) + Selectable, i.e. "$$N/Name"); six-slice
				// SkyLut exposes a slice slider, single-slice GBufferA does not.
				ctx->ItemClick(ImGuiTestRef(tabTex));
				ctx->Yield(2);
				ImGuiWindow *texTable = FindChildWindow("GPU Profiler", "gpu_profiler_textures");
				IM_CHECK(texTable != nullptr);
				// Table columns add scope levels, so address rows by the
				// stable texture-name label wildcard.
				ctx->SetRef(texTable);
				IM_CHECK(ctx->ItemExists("**/GBufferA"));
				IM_CHECK(ctx->ItemExists("**/ShadowMap"));
				IM_CHECK(ctx->ItemExists("**/SkyLut"));
				ctx->ItemClick("**/SkyLut");
				ctx->Yield(2);
				ImGuiWindow *texDetails = FindChildWindow("GPU Profiler", "gpu_profiler_tex_details");
				IM_CHECK(texDetails != nullptr);
				ctx->SetRef(texDetails);
				IM_CHECK(ctx->ItemExists("$$3/slice"));
				ctx->SetRef(texTable);
				ctx->ItemClick("**/GBufferA");
				ctx->Yield(2);
				ctx->SetRef(texDetails);
				IM_CHECK(!ctx->ItemExists("$$0/slice"));
				ctx->SetRef(gpuWin);

				// Per-test cleanup: close the capture viewer.
				if (ImGuiWindow *gp = ImGui::FindWindowByName("GPU Profiler"))
					ctx->ItemClick(ImGuiTestRef(gp->GetID("#CLOSE")));
				ctx->Yield(2);
			}

			// File menu + shortcut open/save against injected dialog paths, then
			// selection/state checks on the reloaded scene. The saved file is
			// verified by the headless round-trip lane.
			void TestFileSaveOpen(ImGuiTestContext *ctx)
			{
				const std::string saved = s_ArtifactDir + "/saved_scene.json";
				std::error_code ec;
				std::filesystem::remove(saved, ec);

				auto subject = FindNode(kSubjectName);
				IM_CHECK(subject != nullptr);

				// Modify a persisted value through the inspector.
				ctx->WindowFocus("//Scene Inspector");
				ctx->SetRef("Scene Inspector");
				ctx->ItemClick("hier:RootNode/hier:UiTestSubject");
				ctx->Yield(2);
				ctx->WindowFocus("//Node Properties");
				ctx->SetRef("Node Properties");
				ctx->ItemInputValue("Light/Intensity", 9.5f);
				ctx->Yield(2);
				IM_CHECK(Editor::IsSceneDirty());

				// File -> Save As with an injected temp path. MenuAction
				// hashes the leading window name from the current ref seed,
				// so base the ref on the resolved menu-bar window first.
				ImGuiWindow *menuBar = ImGui::FindWindowByName("##MainMenuBar");
				IM_CHECK(menuBar != nullptr);
				QueueSaveDialogResult(saved);
				ctx->SetRef(menuBar);
				ctx->MenuClick("File/file:save_as");
				ctx->Yield(4);
				IM_CHECK(Editor::GetCurrentScenePath() == saved);
				IM_CHECK(!Editor::IsSceneDirty());
				IM_CHECK(std::filesystem::exists(saved, ec) && !ec);

				// Ctrl+S saves in place at the tracked native path.
				ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Super | ImGuiKey_S);
				ctx->Yield(4);
				IM_CHECK(Editor::GetCurrentScenePath() == saved);
				IM_CHECK(!Editor::IsSceneDirty());

				// A cancelled Save As leaves the tracked path untouched.
				QueueSaveDialogResult("");
				ctx->SetRef(menuBar);
				ctx->MenuClick("File/file:save_as");
				ctx->Yield(3);
				IM_CHECK(Editor::GetCurrentScenePath() == saved);

				// File -> Open replaces the scene: selection resets and the
				// persisted edit is live in the model.
				QueueOpenDialogResult(saved);
				ctx->SetRef(menuBar);
				ctx->MenuClick("File/file:open");
				ctx->Yield(8);
				IM_CHECK(Editor::GetSelectionSet().members.empty());
				IM_CHECK(Editor::GetCurrentScenePath() == saved);
				auto reloaded = FindNode(kSubjectName);
				IM_CHECK(reloaded != nullptr);
				auto light = reloaded->GetComponent<Light>();
				IM_CHECK(light != nullptr);
				IM_CHECK(light->GetIntensity() == 9.5f);
				ctx->WindowFocus("//Scene Inspector");
				ctx->SetRef("Scene Inspector");
				IM_CHECK(ctx->ItemExists("hier:RootNode/hier:UiTestSubject"));
				ctx->ItemClick("hier:RootNode/hier:UiTestSubject");
				ctx->Yield(2);
				IM_CHECK(Editor::GetSelectedSceneNode() == reloaded.get());
			}

			// Confirmation handling through the real Yes/No modal: the console
			// queues a confirm dialog from Lua and the test answers it.
			void TestConfirmDialog(ImGuiTestContext *ctx)
			{
				Editor::SetWindowVisible("Console", true);
				ctx->WindowFocus("//Console");
				ctx->Yield(2);
				ctx->SetRef("Console");
				ctx->ItemInput("##cmd");
				ctx->KeyCharsReplaceEnter(
					"Editor.RequestConfirmDialog(\"UiTest Confirm\",\"Proceed?\","
					"function(y) Editor.SetImportFlag(\"uitest_confirm\",y) end)");
				ctx->Yield(3);
				IM_CHECK(ctx->WindowInfo("//UiTest Confirm").ID != 0);
				ctx->ItemClick("//UiTest Confirm/Yes");
				ctx->Yield(2);
				IM_CHECK(Editor::GetImportFlag("uitest_confirm", false));
			}

			// Deliberate failure in the perf group; never queued by the
			// functional suite. Run with --ui-test-group=perf to verify the
			// failure-capture/JUnit evidence path.
			void TestFailureProbe(ImGuiTestContext *ctx)
			{
				// Intentional failure; the surrounding run verifies the
				// capture + JUnit evidence it produces.
				IM_CHECK(1 == 2);
			}
		}

		bool ConsumeLauncherArg(int argc, char **argv, int &i)
		{
			const char *a = argv[i];
			const char *value = nullptr;

			if (std::strcmp(a, "--ui-test") == 0)
			{
				s_Active = true;
				return true;
			}
			if (MatchArg(a, "--ui-test-filter", &value, argc, argv, i))
			{
				s_Filter = value;
				return true;
			}
			if (MatchArg(a, "--ui-test-artifacts", &value, argc, argv, i))
			{
				s_ArtifactDir = value;
				return true;
			}
			if (MatchArg(a, "--ui-test-junit", &value, argc, argv, i))
			{
				s_JunitPath = value;
				return true;
			}
			if (MatchArg(a, "--ui-test-timeout", &value, argc, argv, i))
			{
				s_TimeoutSeconds = std::strtof(value, nullptr);
				if (s_TimeoutSeconds <= 0.0f)
					s_TimeoutSeconds = 120.0f;
				return true;
			}
			if (MatchArg(a, "--ui-test-group", &value, argc, argv, i))
			{
				// "perf" queues the non-default group (failure probes);
				// anything else queues the functional Tests group.
				s_QueuePerfGroup = std::strcmp(value, "perf") == 0;
				return true;
			}
			return false;
		}

		bool Active() { return s_Active; }

		std::string IniOverridePath() { return IniPath(); }

		void ApplyWindowConstraints(unsigned int &w, unsigned int &h)
		{
			w = 1280;
			h = 720;
		}

		float ForceGuiScale(float /*scale*/) { return 1.0f; }

		void Initialize()
		{
			if (!s_Active || s_Engine != nullptr)
				return;

			std::error_code ec;
			std::filesystem::create_directories(s_ArtifactDir, ec);
			if (s_JunitPath.empty())
				s_JunitPath = DefaultJunitPath();

			// Complete ini override: ImGui reads/writes only inside the
			// artifact dir, and the editor's early manual readers follow
			// via Editor::SetPersistedIniPath (set by the launcher);
			// io.IniFilename borrows our string for the whole run.
			s_IniPath = IniPath();
			ImGui::GetIO().IniFilename = s_IniPath.c_str();

			s_Engine = ImGuiTestEngine_CreateContext();
			ImGuiTestEngineIO &io = ImGuiTestEngine_GetIO(s_Engine);
			io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
			io.ConfigSavedSettings = false;
			// Captures run on the presentation thread; failures leave a
			// screenshot in the artifact dir (relocated from ./output).
			io.ConfigCaptureEnabled = true;
			io.ConfigCaptureOnError = true;
			io.ScreenCaptureFunc = ScreenCaptureRHI;
			io.ConfigWatchdogWarning = s_TimeoutSeconds * 0.5f;
			io.ConfigWatchdogKillTest = s_TimeoutSeconds * 0.75f;
			// Crash-safe export: a crash mid-test still leaves JUnit output.
			io.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;
			io.ExportResultsFilename = s_JunitPath.c_str();

			ImGuiTest *test = nullptr;
			test = IM_REGISTER_TEST(s_Engine, "editor", "select_and_edit_known_node");
			test->TestFunc = TestSelectAndEditKnownNode;
			test = IM_REGISTER_TEST(s_Engine, "editor", "hierarchy_selection");
			test->TestFunc = TestHierarchySelection;
			test = IM_REGISTER_TEST(s_Engine, "editor", "inspector_edit_dirty_undo");
			test->TestFunc = TestInspectorEditDirtyUndo;
			test = IM_REGISTER_TEST(s_Engine, "editor", "cluster_settings_debug");
			test->TestFunc = TestClusterSettingsDebug;
			test = IM_REGISTER_TEST(s_Engine, "editor", "content_browser_mesh_preview");
			test->TestFunc = TestContentBrowserMeshPreview;
			test = IM_REGISTER_TEST(s_Engine, "editor", "profiler_tabs");
			test->TestFunc = TestProfilerTabs;
			test = IM_REGISTER_TEST(s_Engine, "editor", "gpu_graph_capture");
			test->TestFunc = TestGpuGraphCapture;
			test = IM_REGISTER_TEST(s_Engine, "editor", "file_save_open");
			test->TestFunc = TestFileSaveOpen;
			test = IM_REGISTER_TEST(s_Engine, "editor", "confirm_dialog");
			test->TestFunc = TestConfirmDialog;
			// Intentional failure; only queued via --ui-test-group=perf.
			test = IM_REGISTER_TEST(s_Engine, "perf", "failure_probe");
			test->TestFunc = TestFailureProbe;

			ImGuiTestEngine_Start(s_Engine, ImGui::GetCurrentContext());
			s_Started = true;
			s_Ran = true;
			s_StartTime = std::chrono::steady_clock::now();

			ImGuiTestEngine_QueueTests(s_Engine,
				s_QueuePerfGroup ? ImGuiTestGroup_Perfs : ImGuiTestGroup_Tests,
				s_Filter.empty() ? nullptr : s_Filter.c_str());
			ImVector<ImGuiTestRunTask> queue;
			ImGuiTestEngine_GetTestQueue(s_Engine, &queue);
			if (queue.empty())
			{
				FURYE << "[uitest] filter '" << s_Filter << "' matched no tests";
				Finish(kExitEmpty);
				return;
			}
			FURYI << "[uitest] queued " << queue.Size << " test(s), timeout "
				<< s_TimeoutSeconds << "s, artifacts " << s_ArtifactDir;
		}

		void Pump()
		{
			if (!s_Active || s_Engine == nullptr || s_Finished)
				return;

			// Fixture must exist before every test frame; re-created after a
			// File -> Open replaces the scene.
			EnsureFixture();

			if (!s_Started)
				return; // empty selection already finished in Initialize

			if (ImGuiTestEngine_IsTestQueueEmpty(s_Engine))
			{
				ImGuiTestEngineResultSummary summary;
				ImGuiTestEngine_GetResultSummary(s_Engine, &summary);
				const bool failed = summary.CountTested > 0 && summary.CountSuccess != summary.CountTested;
				if (failed && s_FailureSettleFrames < 15)
				{
					// Let in-flight failure captures land before shutdown.
					++s_FailureSettleFrames;
					return;
				}
				Finish(failed ? kExitFailed : 0);
				return;
			}

			const double elapsed = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - s_StartTime).count();
			if (elapsed > s_TimeoutSeconds)
			{
				FURYE << "[uitest] global timeout after " << elapsed << "s";
				ImGuiTestEngine_TryAbortEngine(s_Engine);
				Finish(kExitTimeout);
			}
		}

		void PreSwap() {}
		void PostSwap()
		{
			if (s_Engine != nullptr)
				ImGuiTestEngine_PostSwap(s_Engine);
		}

		void Shutdown()
		{
			if (s_Engine == nullptr)
				return;
			if (s_Started)
			{
				// Stop while the ImGui context is alive: joining runs the
				// coroutine, which asserts on the bound context.
				ImGuiTestEngine_Stop(s_Engine);
				s_Started = false;
				if (!s_Finished)
					ExportResults();
			}
			RelocateCaptures();
		}

		void DestroyEngine()
		{
			if (s_Engine == nullptr)
				return;
			// Documented order: DestroyContext after ImGui::DestroyContext.
			ImGuiTestEngine_DestroyContext(s_Engine);
			s_Engine = nullptr;
		}

		bool Ran() { return s_Ran; }
		int ExitCode() { return s_ExitCode; }

		const std::string &ArtifactDir() { return s_ArtifactDir; }

		void QueueOpenDialogResult(const std::string &path) { s_OpenDialogResults.push_back(path); }
		void QueueSaveDialogResult(const std::string &path) { s_SaveDialogResults.push_back(path); }

		bool ConsumeOpenDialogResult(std::string &out)
		{
			if (s_OpenDialogResults.empty())
				return false;
			out = s_OpenDialogResults.front();
			s_OpenDialogResults.pop_front();
			return true;
		}

		bool ConsumeSaveDialogResult(std::string &out)
		{
			if (s_SaveDialogResults.empty())
				return false;
			out = s_SaveDialogResults.front();
			s_SaveDialogResults.pop_front();
			return true;
		}
	}
}

#endif // FURY_UI_TEST_HOST && WITH_EDITOR
