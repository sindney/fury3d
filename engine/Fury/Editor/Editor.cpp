#ifdef WITH_EDITOR

#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorThemes.h"
#include "Fury/Editor/EditorLog.h"
#include "Fury/Gui.h"
#include "Fury/Log.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/FileUtil.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <filesystem>

namespace fury
{
	namespace Editor
	{
		// Forward declarations of window-rendering functions (defined in
		// EditorWindows.cpp).
		void RenderSettingsWindow(bool* open);
		void RenderProfilerWindow(bool* open);
		void RenderSceneInspectorWindow(bool* open);
		void RenderConsoleWindow(bool* open);
		void RenderContentBrowserWindow(bool* open);

		// Public-ish accessors used by the window rendering code, kept in
		// this TU so we don't multiply globals.
		extern SceneIO g_SceneIO;
		extern SceneTreeProvider g_TreeProvider;
		extern CommandHandler g_CommandHandler;
		extern std::vector<CameraControl> g_CameraControls;
		extern std::unordered_map<std::string, bool> g_ImportFlags;
		extern SceneNode* g_SelectedSceneNode;
		extern bool g_ShowSettings;
		extern bool g_ShowProfiler;
		extern bool g_ShowSceneInspector;
		extern bool g_ShowConsole;
		extern bool g_ShowContentBrowser;
		extern bool g_SaveAsModalOpen;

		// Per-TU storage. Defined here, declared as extern above so
		// EditorWindows.cpp can read them without a header dependency.
		SceneIO g_SceneIO;
		SceneTreeProvider g_TreeProvider;
		CommandHandler g_CommandHandler;
		std::vector<CameraControl> g_CameraControls;
		std::unordered_map<std::string, bool> g_ImportFlags;
		SceneNode* g_SelectedSceneNode = nullptr;
		bool g_ShowSettings = false;
		bool g_ShowProfiler = false;
		bool g_ShowSceneInspector = true;   // visible by default — docked left
		bool g_ShowConsole = true;          // visible by default — bottom dock
		bool g_ShowContentBrowser = true;   // visible by default — bottom dock
		bool g_SaveAsModalOpen = false;

		namespace
		{
			bool s_FirstFrame = true;
			bool s_RequestRebuildLayout = false;
			ImGuiID s_DockspaceID = 0;

			// Did imgui.ini exist at startup? Determines whether we should
			// run BuildDefaultLayout on the first frame.
			bool s_HadIniOnStartup = false;

			void HookEngineLog()
			{
				if (auto log = fury::Log<0>::Instance())
				{
					log->SetExtraSink([](const fury::Record& rec) {
						LogLevel lvl = LogLevel::Info;
						if      (rec.level == "EROR") lvl = LogLevel::Error;
						else if (rec.level == "WARN") lvl = LogLevel::Warn;
						else if (rec.level == "DBUG") lvl = LogLevel::Debug;
						std::string text = rec.stream.str();
						GlobalLogBuffer().Push(lvl, std::move(text));
					});
				}
			}

			void UnhookEngineLog()
			{
				if (auto log = fury::Log<0>::Instance())
				{
					log->SetExtraSink(nullptr);
				}
			}

			// imgui.ini settings handler — persists the active theme index
			// alongside ImGui's window layout state. Single key under
			// [FuryEditor][Editor]: `Theme=N`.
			void* SettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/)
			{
				return (void*)1; // non-null sentinel — we have a single entry
			}

			void SettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
			{
				int v = 0;
				if (std::sscanf(line, "Theme=%d", &v) == 1)
				{
					SetCurrentThemeIndex(v);
				}
			}

			void SettingsHandler_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*)
			{
				// Apply the persisted theme once ImGui finishes loading the ini.
				ApplyPersistedTheme();
			}

			void SettingsHandler_WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
			{
				buf->appendf("[%s][Editor]\n", handler->TypeName);
				buf->appendf("Theme=%d\n", GetCurrentThemeIndex());
				buf->append("\n");
			}

			void RegisterSettingsHandler()
			{
				ImGuiSettingsHandler ini;
				ini.TypeName = "FuryEditor";
				ini.TypeHash = ImHashStr("FuryEditor");
				ini.ReadOpenFn  = SettingsHandler_ReadOpen;
				ini.ReadLineFn  = SettingsHandler_ReadLine;
				ini.ApplyAllFn  = SettingsHandler_ApplyAll;
				ini.WriteAllFn  = SettingsHandler_WriteAll;
				ImGui::AddSettingsHandler(&ini);
			}

			void BuildDefaultLayout(ImGuiID dockspace_id)
			{
				ImGui::DockBuilderRemoveNode(dockspace_id);
				ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

				ImGuiID center = dockspace_id;
				ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
				ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

				ImGui::DockBuilderDockWindow("Scene Inspector", left);
				ImGui::DockBuilderDockWindow("Console",         bottom);
				ImGui::DockBuilderDockWindow("Content Browser", bottom);

				ImGui::DockBuilderFinish(dockspace_id);
			}

			void RenderSaveAsModal()
			{
				static char filename[256] = "scene_saved.json";

				if (g_SaveAsModalOpen)
				{
					ImGui::OpenPopup("Save Scene As");
					g_SaveAsModalOpen = false;
				}

				if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				{
					ImGui::Text("Filename (under %s):", GetSceneDir().c_str());
					ImGui::InputText("##save_as_name", filename, IM_ARRAYSIZE(filename));
					ImGui::Separator();

					if (ImGui::Button("Save", ImVec2(120, 0)))
					{
						if (g_SceneIO.on_save_as)
						{
							try { g_SceneIO.on_save_as(filename); }
							catch (...) {}
						}
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(120, 0)))
					{
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			void RenderMenuBar()
			{
				if (!ImGui::BeginMainMenuBar()) return;

				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("New"))
					{
						if (g_SceneIO.on_new) try { g_SceneIO.on_new(); } catch (...) {}
						g_SelectedSceneNode = nullptr;
					}

					std::vector<std::string> files;
					if (g_SceneIO.list_files)
					{
						try { files = g_SceneIO.list_files(); } catch (...) {}
					}

					if (ImGui::BeginMenu("Open", !files.empty()))
					{
						for (const auto& f : files)
						{
							if (ImGui::MenuItem(f.c_str()))
							{
								if (g_SceneIO.on_open) try { g_SceneIO.on_open(f); } catch (...) {}
								g_SelectedSceneNode = nullptr;
							}
						}
						ImGui::EndMenu();
					}

					if (ImGui::BeginMenu("Import", !files.empty()))
					{
						for (const auto& f : files)
						{
							if (ImGui::MenuItem(f.c_str()))
							{
								if (g_SceneIO.on_import) try { g_SceneIO.on_import(f); } catch (...) {}
							}
						}
						ImGui::EndMenu();
					}

					if (ImGui::MenuItem("Save As..."))
					{
						g_SaveAsModalOpen = true;
					}

					ImGui::Separator();

					if (ImGui::MenuItem("Settings"))
					{
						g_ShowSettings = !g_ShowSettings;
					}

					ImGui::Separator();

					if (ImGui::MenuItem("Quit"))
					{
						Gui::CloseWindow();
					}

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Window"))
				{
					ImGui::MenuItem("Profiler",         nullptr, &g_ShowProfiler);
					ImGui::MenuItem("Scene Inspector",  nullptr, &g_ShowSceneInspector);
					ImGui::MenuItem("Console",          nullptr, &g_ShowConsole);
					ImGui::MenuItem("Content Browser",  nullptr, &g_ShowContentBrowser);
					ImGui::Separator();
					if (ImGui::MenuItem("Reset Layout"))
					{
						s_RequestRebuildLayout = true;
						g_ShowSettings = false;
						g_ShowProfiler = false;
					}
					ImGui::EndMenu();
				}

				// Script-emitted menus render between Window and the trailing
				// (currently empty) built-ins, matching today's contract.
				Gui::InvokeMenuBarCallback();

				ImGui::EndMainMenuBar();
			}
		}

		// --------------------------------------------------------------
		// Public API
		// --------------------------------------------------------------
		void Initialize()
		{
			std::error_code ec;
			s_HadIniOnStartup = std::filesystem::exists("imgui.ini", ec);

			RegisterSettingsHandler();
			ApplyPersistedTheme();
			HookEngineLog();
		}

		void Tick()
		{
			RenderMenuBar();

			s_DockspaceID = ImGui::DockSpaceOverViewport(0,
				ImGui::GetMainViewport(),
				ImGuiDockNodeFlags_PassthruCentralNode);

			if ((s_FirstFrame && !s_HadIniOnStartup) || s_RequestRebuildLayout)
			{
				BuildDefaultLayout(s_DockspaceID);
				s_RequestRebuildLayout = false;
			}
			s_FirstFrame = false;

			RenderSaveAsModal();

			if (g_ShowSettings)        RenderSettingsWindow(&g_ShowSettings);
			if (g_ShowProfiler)        RenderProfilerWindow(&g_ShowProfiler);
			if (g_ShowSceneInspector)  RenderSceneInspectorWindow(&g_ShowSceneInspector);
			if (g_ShowConsole)         RenderConsoleWindow(&g_ShowConsole);
			if (g_ShowContentBrowser)  RenderContentBrowserWindow(&g_ShowContentBrowser);
		}

		void Shutdown()
		{
			UnhookEngineLog();
			ClearSceneIO();
			ClearSceneTreeProvider();
			ClearCommandHandler();
			ClearCameraControls();
			g_SelectedSceneNode = nullptr;
		}

		SceneNode* GetSelectedSceneNode()
		{
			return g_SelectedSceneNode;
		}

		void SetWindowVisible(const char* name, bool visible)
		{
			if (!name) return;
			if      (std::strcmp(name, "Settings")       == 0) g_ShowSettings = visible;
			else if (std::strcmp(name, "Profiler")       == 0) g_ShowProfiler = visible;
			else if (std::strcmp(name, "SceneInspector") == 0) g_ShowSceneInspector = visible;
			else if (std::strcmp(name, "Console")        == 0) g_ShowConsole = visible;
			else if (std::strcmp(name, "ContentBrowser") == 0) g_ShowContentBrowser = visible;
		}

		bool GetWindowVisible(const char* name)
		{
			if (!name) return false;
			if      (std::strcmp(name, "Settings")       == 0) return g_ShowSettings;
			else if (std::strcmp(name, "Profiler")       == 0) return g_ShowProfiler;
			else if (std::strcmp(name, "SceneInspector") == 0) return g_ShowSceneInspector;
			else if (std::strcmp(name, "Console")        == 0) return g_ShowConsole;
			else if (std::strcmp(name, "ContentBrowser") == 0) return g_ShowContentBrowser;
			return false;
		}

		void SetImportFlag(const char* name, bool value)
		{
			if (!name) return;
			g_ImportFlags[name] = value;
		}

		bool GetImportFlag(const char* name, bool default_value)
		{
			if (!name) return default_value;
			auto it = g_ImportFlags.find(name);
			if (it == g_ImportFlags.end()) return default_value;
			return it->second;
		}

		void Log(const char* level, const char* text)
		{
			if (!text) return;
			GlobalLogBuffer().Push(LevelFromString(level), text);
		}

		void SetSceneIO(SceneIO io) { g_SceneIO = std::move(io); }
		void ClearSceneIO()         { g_SceneIO = {}; }

		std::string GetSceneDir()
		{
			if (g_SceneIO.scene_dir)
			{
				try
				{
					std::string dir = g_SceneIO.scene_dir();
					if (!dir.empty()) return dir;
				}
				catch (...) {}
			}
			return FileUtil::GetAbsPath("Resource/Scene/");
		}

		void SetSceneTreeProvider(SceneTreeProvider p) { g_TreeProvider = std::move(p); }
		void ClearSceneTreeProvider()                  { g_TreeProvider = {}; }

		void SetCommandHandler(CommandHandler h) { g_CommandHandler = std::move(h); }
		void ClearCommandHandler()               { g_CommandHandler = {}; }

		void SetCameraControls(std::vector<CameraControl> controls) { g_CameraControls = std::move(controls); }
		void ClearCameraControls()                                  { g_CameraControls.clear(); }
	}
}

#endif // WITH_EDITOR
