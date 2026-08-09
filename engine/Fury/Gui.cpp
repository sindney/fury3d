#include <cstring>
#include <vector>

// imgui.h has its own #pragma warning(push/pop) (lines 135/4506), so
// don't wrap -- a surrounding push/pop would be consumed by imgui's
// inner pop and trigger C4193. Escape warnings are silenced via
// /wd4127 in CMakeLists.txt.
#include "ImGui/imconfig.h"
#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_sfml3.h"

#include "Fury/Gui.h"
#include "Fury/Log.h"

#include <SFML/Window.hpp>

#undef DELETE

namespace fury
{
	namespace Gui
	{
		static sf::Window *m_Window = nullptr;

		static float m_GlobalScale = 1.0f;

		// Optional Lua-supplied callback invoked inside the main menu bar.
		// Cleared by the launcher before sol::state destruction.
		static std::function<void()> m_MenuBarCallback;

		bool Initialize(sf::Window *window, float scale, float fontScale)
		{
			m_Window = window;
			m_GlobalScale = scale;

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			// Restrict window dragging to the title bar only. The ImGui
			// default lets a user click+drag on any empty space inside a
			// window to move it, which conflicts with the editor's
			// content-relative input: dragging inside the Viewport's
			// ImGui::Image of the render target counts as "empty space"
			// (Image has no interactive id) and would both rotate the
			// camera AND drag the undocked Viewport window. Title-bar-
			// only matches what users expect from Unity/Unreal/Godot and
			// keeps the Viewport content rect a clean input surface.
			io.ConfigWindowsMoveFromTitleBarOnly = true;

			ImGuiStyle& style = ImGui::GetStyle();
			ImGui::StyleColorsDark(&style);
			// ImGui 1.92 renamed io.FontGlobalScale -> style.FontScaleMain.
			style.FontScaleMain = fontScale;
			style.ScaleAllSizes(scale);

			if (!ImGui_ImplSFML3_Init(window))
			{
				FURYE << "ImGui SFML3 backend init failed";
				return false;
			}

			// 3.3 core matches the GL context the engine already requests.
			if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
			{
				FURYE << "ImGui OpenGL3 backend init failed";
				return false;
			}

			return true;
		}

		void Shutdown()
		{
			if (ImGui::GetCurrentContext() == nullptr) return;

			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplSFML3_Shutdown();
			ImGui::DestroyContext();
			m_Window = nullptr;
		}

		void HandleEvent(sf::Event &event)
		{
			if (ImGui::GetCurrentContext() == nullptr) return;
			ImGui_ImplSFML3_ProcessEvent(event);
		}

		void NewFrame(float frameTime)
		{
			if (ImGui::GetCurrentContext() == nullptr) return;
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplSFML3_NewFrame(m_Window, frameTime);
			ImGui::NewFrame();
		}

		bool WantCaptureMouse()
		{
			if (ImGui::GetCurrentContext() == nullptr) return false;
			return ImGui::GetIO().WantCaptureMouse;
		}

		bool WantCaptureKeyboard()
		{
			if (ImGui::GetCurrentContext() == nullptr) return false;
			return ImGui::GetIO().WantCaptureKeyboard;
		}

		std::pair<bool, bool> Begin(const char* title, bool open)
		{
			bool p_open = open;
			bool visible = ImGui::Begin(title, &p_open);
			return {p_open, visible};
		}

		void End()
		{
			ImGui::End();
		}

		float SliderFloat(const char* label, float current, float vmin, float vmax)
		{
			float v = current;
			ImGui::SliderFloat(label, &v, vmin, vmax);
			return v;
		}

		bool Checkbox(const char* label, bool current)
		{
			bool v = current;
			ImGui::Checkbox(label, &v);
			return v;
		}

		bool Button(const char* label)
		{
			return ImGui::Button(label);
		}

		void Separator()
		{
			ImGui::Separator();
		}

		void Text(const char* str)
		{
			ImGui::Text("%s", str);
		}

		bool BeginMenu(const char* label)
		{
			return ImGui::BeginMenu(label);
		}

		void EndMenu()
		{
			ImGui::EndMenu();
		}

		bool MenuItem(const char* label)
		{
			return ImGui::MenuItem(label);
		}

		std::string InputText(const char* label, const std::string &current, int max_len)
		{
			if (max_len < 1) max_len = 1;
			std::vector<char> buf(static_cast<size_t>(max_len) + 1, 0);
			std::strncpy(buf.data(), current.c_str(), buf.size() - 1);
			ImGui::InputText(label, buf.data(), buf.size());
			return std::string(buf.data());
		}

		void SetMenuBarCallback(std::function<void()> cb)
		{
			m_MenuBarCallback = std::move(cb);
		}

		void InvokeMenuBarCallback()
		{
			if (m_MenuBarCallback) m_MenuBarCallback();
		}

		void CloseWindow()
		{
			// Idempotent: m_Window is null after Shutdown(), and isOpen()
			// returns false once SFML has processed the close. A second
			// Close() call is a no-op rather than a crash.
			if (m_Window != nullptr && m_Window->isOpen())
			{
				m_Window->close();
			}
		}

		void ShowDefault(float /*dt*/)
		{
#if !WITH_EDITOR
			// Editor-less builds keep a minimal main menu bar that only
			// hosts the script-emitted menus (e.g. Camera). With the editor
			// compiled in, Editor::Tick() owns the menu bar end-to-end.
			if (m_MenuBarCallback)
			{
				if (ImGui::BeginMainMenuBar())
				{
					m_MenuBarCallback();
					ImGui::EndMainMenuBar();
				}
			}
#endif
		}

		void Render()
		{
			if (ImGui::GetCurrentContext() == nullptr) return;
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		}
	}
}
