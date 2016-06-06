#include <map>
#include <cstddef> // offsetof

#include "Imgui/imgui.h"
#include "Imgui/imgui_fury.h"

#include "ArrayBuffers.h"
#include "Texture.h"
#include "Shader.h"
#include "Log.h"
#include "EnumUtil.h"
#include "InputUtil.h"

#include "../GLLoader.h"

#undef DELETE

namespace fury
{
	namespace ImGuiBridge
	{
		static std::shared_ptr<Shader> m_Shader;

		static sf::Window *m_Window;

		static unsigned int m_VAO = 0;

		static unsigned int m_VBO = 0;

		static unsigned int m_EAB = 0;

		static unsigned int m_FontTexture;

		static bool m_MousePressed[3];

		static float m_MouseWheel = 0.0f;

		bool Initialize(sf::Window *window)
		{
			m_MousePressed[0] = m_MousePressed[1] = m_MousePressed[2] = false;
			m_Window = window;

			ImGuiIO& io = ImGui::GetIO();

			io.KeyMap[ImGuiKey_Tab] = sf::Keyboard::Tab;
			io.KeyMap[ImGuiKey_LeftArrow] = sf::Keyboard::Left;
			io.KeyMap[ImGuiKey_RightArrow] = sf::Keyboard::Right;
			io.KeyMap[ImGuiKey_UpArrow] = sf::Keyboard::Up;
			io.KeyMap[ImGuiKey_DownArrow] = sf::Keyboard::Down;
			io.KeyMap[ImGuiKey_Home] = sf::Keyboard::Home;
			io.KeyMap[ImGuiKey_End] = sf::Keyboard::End;
			io.KeyMap[ImGuiKey_Delete] = sf::Keyboard::Delete;
			io.KeyMap[ImGuiKey_Backspace] = sf::Keyboard::BackSpace;
			io.KeyMap[ImGuiKey_Enter] = sf::Keyboard::Return;
			io.KeyMap[ImGuiKey_Escape] = sf::Keyboard::Escape;
			io.KeyMap[ImGuiKey_A] = sf::Keyboard::A;
			io.KeyMap[ImGuiKey_C] = sf::Keyboard::C;
			io.KeyMap[ImGuiKey_V] = sf::Keyboard::V;
			io.KeyMap[ImGuiKey_X] = sf::Keyboard::X;
			io.KeyMap[ImGuiKey_Y] = sf::Keyboard::Y;
			io.KeyMap[ImGuiKey_Z] = sf::Keyboard::Z;

			io.DisplaySize = ImVec2((float)m_Window->getSize().x, (float)m_Window->getSize().y);
			io.RenderDrawListsFn = RenderDrawLists;

			ImGuiStyle& style = ImGui::GetStyle();

			const ImGuiStyle def;
			style = def;

			style.Alpha                   = 1.0f;
			style.WindowPadding           = ImVec2(8,8);
			style.WindowMinSize           = ImVec2(32,32);
			style.WindowRounding          = 0.0f;
			style.WindowTitleAlign        = ImGuiAlign_Left;
			style.ChildWindowRounding     = 0.0f;
			style.FramePadding            = ImVec2(4,3);
			style.FrameRounding           = 0.0f;
			style.ItemSpacing             = ImVec2(8,4);
			style.ItemInnerSpacing        = ImVec2(4,4);
			style.TouchExtraPadding       = ImVec2(0,0);
			style.WindowFillAlphaDefault  = 1.0f;
			style.IndentSpacing           = 22.0f;
			style.ColumnsMinSpacing       = 6.0f;
			style.ScrollbarSize           = 16.0f;
			style.ScrollbarRounding       = 9.0f;
			style.GrabMinSize             = 10.0f;
			style.GrabRounding            = 0.0f;
			style.DisplayWindowPadding    = ImVec2(22,22);
			style.DisplaySafeAreaPadding  = ImVec2(4,4);
			style.AntiAliasedLines        = true;
			style.AntiAliasedShapes       = true;
			style.CurveTessellationTol    = 1.25f;

			style.Colors[ImGuiCol_Text]                   = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
			style.Colors[ImGuiCol_TextDisabled]           = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
			style.Colors[ImGuiCol_WindowBg]               = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
			style.Colors[ImGuiCol_ChildWindowBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
			style.Colors[ImGuiCol_Border]                 = ImVec4(0.078f, 0.078f, 0.078f, 1.0f);
			style.Colors[ImGuiCol_BorderShadow]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
			style.Colors[ImGuiCol_FrameBg]                = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
			style.Colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
			style.Colors[ImGuiCol_FrameBgActive]          = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
			style.Colors[ImGuiCol_TitleBg]                = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);
			style.Colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);
			style.Colors[ImGuiCol_TitleBgActive]          = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);
			style.Colors[ImGuiCol_MenuBarBg]              = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
			style.Colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
			style.Colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
			style.Colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_ComboBg]                = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
			style.Colors[ImGuiCol_CheckMark]              = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_SliderGrab]             = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
			style.Colors[ImGuiCol_SliderGrabActive]       = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
			style.Colors[ImGuiCol_ButtonHovered]          = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_ButtonActive]           = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_Header]                 = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
			style.Colors[ImGuiCol_HeaderHovered]          = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_HeaderActive]           = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_Column]                 = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
			style.Colors[ImGuiCol_ColumnHovered]          = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_ColumnActive]           = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_ResizeGrip]             = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
			style.Colors[ImGuiCol_ResizeGripHovered]      = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_ResizeGripActive]       = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_CloseButton]            = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
			style.Colors[ImGuiCol_CloseButtonHovered]     = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_CloseButtonActive]      = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_PlotLines]              = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			style.Colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_PlotHistogram]          = ImVec4(1.0f, 0.59f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.0f, 0.78f, 0.0f, 1.0f);
			style.Colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
			style.Colors[ImGuiCol_TooltipBg]              = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
			style.Colors[ImGuiCol_ModalWindowDarkening]   = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);

			const char *vertex_shader =
				"#version 330\n"
				"uniform mat4 ProjMtx;\n"
				"in vec2 Position;\n"
				"in vec2 UV;\n"
				"in vec4 Color;\n"
				"out vec2 Frag_UV;\n"
				"out vec4 Frag_Color;\n"
				"void main()\n"
				"{\n"
				"	Frag_UV = UV;\n"
				"	Frag_Color = Color;\n"
				"	gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
				"}\n";

			const char* fragment_shader =
				"#version 330\n"
				"uniform sampler2D Texture;\n"
				"in vec2 Frag_UV;\n"
				"in vec4 Frag_Color;\n"
				"out vec4 Out_Color;\n"
				"void main()\n"
				"{\n"
				"	Out_Color = Frag_Color * texture( Texture, Frag_UV.st);\n"
				"}\n";

			m_Shader = Shader::Create("GUIShader", ShaderType::OTHER);
			if (!m_Shader->Compile(vertex_shader, fragment_shader, ""))
			{
				FURYE << "Failed to compile gui shader!";
				return false;
			}

			m_Shader->Bind();

			auto shaderId = m_Shader->GetProgram();

			auto attribLocationPosition = glGetAttribLocation(shaderId, "Position");
			auto attribLocationUV = glGetAttribLocation(shaderId, "UV");
			auto attribLocationColor = glGetAttribLocation(shaderId, "Color");

			glGenBuffers(1, &m_VBO);
			glGenBuffers(1, &m_EAB);
			glGenVertexArrays(1, &m_VAO);

			if (m_VBO == 0 || m_EAB == 0 || m_VAO == 0)
				return false;

			glBindVertexArray(m_VAO);
			glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
			glEnableVertexAttribArray(attribLocationPosition);
			glEnableVertexAttribArray(attribLocationUV);
			glEnableVertexAttribArray(attribLocationColor);

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
			glVertexAttribPointer(attribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
			glVertexAttribPointer(attribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
			glVertexAttribPointer(attribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

			// Load Font
			unsigned char* pixels;
			int width, height;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

			glGenTextures(1, &m_FontTexture);
			glBindTexture(GL_TEXTURE_2D, m_FontTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

			// Store our identifier
			io.Fonts->TexID = (void *)(intptr_t)m_FontTexture;

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);

			m_Shader->UnBind();

			return true;
		}

		void Shutdown()
		{
			if (m_VAO != 0)
			{
				glDeleteVertexArrays(1, &m_VAO);
				m_VAO = 0;
			}

			m_Window = nullptr;
			m_Shader = nullptr;

			if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
			if (m_VBO) glDeleteBuffers(1, &m_VBO);
			if (m_EAB) glDeleteBuffers(1, &m_EAB);
			m_VAO = m_VBO = m_EAB = 0;

			if (m_FontTexture)
			{
				glDeleteTextures(1, &m_FontTexture);
				ImGui::GetIO().Fonts->TexID = 0;
				m_FontTexture = 0;
			}
		}

		void RenderDrawLists(ImDrawData* draw_data)
		{
			// Backup GL state
			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
			GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
			GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
			GLint last_element_array_buffer; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
			GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
			GLint last_blend_src; glGetIntegerv(GL_BLEND_SRC, &last_blend_src);
			GLint last_blend_dst; glGetIntegerv(GL_BLEND_DST, &last_blend_dst);
			GLint last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, &last_blend_equation_rgb);
			GLint last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &last_blend_equation_alpha);
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
			GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
			GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
			GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
			GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

			// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
			glEnable(GL_BLEND);
			glBlendEquation(GL_FUNC_ADD);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_CULL_FACE);
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_SCISSOR_TEST);
			glActiveTexture(GL_TEXTURE0);

			// Handle cases of screen coordinates != from framebuffer coordinates (e.g. retina displays)
			ImGuiIO& io = ImGui::GetIO();
			int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
			int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
			draw_data->ScaleClipRects(io.DisplayFramebufferScale);

			// Setup viewport, orthographic projection matrix
			glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
			const float ortho_projection[4][4] =
			{
				{ 2.0f / io.DisplaySize.x, 0.0f, 0.0f, 0.0f },
				{ 0.0f, 2.0f / -io.DisplaySize.y, 0.0f, 0.0f },
				{ 0.0f, 0.0f, -1.0f, 0.0f },
				{ -1.0f, 1.0f, 0.0f, 1.0f },
			};

			m_Shader->Bind();
			m_Shader->BindMatrix("ProjMtx", &ortho_projection[0][0]);

			glBindVertexArray(m_VAO);

			for (int n = 0; n < draw_data->CmdListsCount; n++)
			{
				const ImDrawList* cmd_list = draw_data->CmdLists[n];
				const ImDrawIdx* idx_buffer_offset = 0;

				glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
				glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.size() * sizeof(ImDrawVert), (GLvoid*)&cmd_list->VtxBuffer.front(), GL_STREAM_DRAW);

				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EAB);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx), (GLvoid*)&cmd_list->IdxBuffer.front(), GL_STREAM_DRAW);

				for (const ImDrawCmd* pcmd = cmd_list->CmdBuffer.begin(); pcmd != cmd_list->CmdBuffer.end(); pcmd++)
				{
					if (pcmd->UserCallback)
					{
						pcmd->UserCallback(cmd_list, pcmd);
					}
					else
					{
						glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
						glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
						glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset);
					}
					idx_buffer_offset += pcmd->ElemCount;
				}
			}

			m_Shader->UnBind();

			// Restore modified GL state
			glUseProgram(last_program);
			glBindTexture(GL_TEXTURE_2D, last_texture);
			glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
			glBindVertexArray(last_vertex_array);
			glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
			glBlendFunc(last_blend_src, last_blend_dst);
			if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
			if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
			if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
			if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
			glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
		}

		void HandleEvent(sf::Event &event)
		{
			ImGuiIO& io = ImGui::GetIO();
			switch (event.type)
			{
			case sf::Event::MouseButtonPressed: // fall-through
			case sf::Event::MouseButtonReleased:
				m_MousePressed[event.mouseButton.button] = (event.type == sf::Event::MouseButtonPressed);
				break;
			case sf::Event::MouseWheelMoved:
				m_MouseWheel += (float)event.mouseWheel.delta;
				break;
			case sf::Event::KeyPressed: // fall-through
			case sf::Event::KeyReleased:
				io.KeysDown[event.key.code] = (event.type == sf::Event::KeyPressed);
				io.KeyCtrl = event.key.control;
				io.KeyShift = event.key.shift;
				io.KeyAlt = event.key.alt;
				break;
			case sf::Event::TextEntered:
				if (event.text.unicode > 0 && event.text.unicode < 0x10000) {
					io.AddInputCharacter(event.text.unicode);
				}
				break;
			default:
				break;
			}
		}

		void NewFrame(float frameTime)
		{
			auto &io = ImGui::GetIO();
			auto &inputMgr = InputUtil::Instance();
			auto winSize = m_Window->getSize();

			// Setup display size (every frame to accommodate for window resizing)
			io.DisplaySize = ImVec2((float)winSize.x, (float)winSize.y);
			io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

			// Setup time step
			io.DeltaTime = frameTime;

			// Setup inputs
			// Mouse position, in pixels (set to -1,-1 if no mouse / on another screen, etc.)
			sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(*m_Window));
			io.MousePos = ImVec2(mousePos.x, mousePos.y);

			// If a mouse press event came, always pass it as "mouse held this frame"
			// so we don't miss click-release events that are shorter than 1 frame.
			io.MouseDown[0] = m_MousePressed[0] || inputMgr->GetMouseDown(sf::Mouse::Left);
			io.MouseDown[1] = m_MousePressed[1] || inputMgr->GetMouseDown(sf::Mouse::Right);
			io.MouseDown[2] = m_MousePressed[2] || inputMgr->GetMouseDown(sf::Mouse::Middle);
			m_MousePressed[0] = m_MousePressed[1] = m_MousePressed[2] = false;

			io.MouseWheel = m_MouseWheel;
			m_MouseWheel = 0.0f;

			// Hide OS mouse cursor if ImGui is drawing it
			m_Window->setMouseCursorVisible(!io.MouseDrawCursor);

			// Start the frame
			ImGui::NewFrame();
		}
	}
}

namespace ImGui 
{
	struct PlotVarData
	{
	    ImGuiID        	ID;
	    ImVector<float>	Data;
	    int            	DataInsertIdx;
	    int            	LastFrame;

	    PlotVarData() : ID(0), DataInsertIdx(0), LastFrame(-1) {}
	};

	typedef std::map<ImGuiID, PlotVarData> PlotVarsMap;
	static PlotVarsMap  g_PlotVarsMap;

	// Plot value over time
	// Call with 'value == FLT_MAX' to draw without adding new value to the buffer
	void PlotVar(const char* label, float value, float scale_min, float scale_max, float width, size_t buffer_size)
	{
	    IM_ASSERT(label);
	    if (buffer_size == 0)
	        buffer_size = 120;

	    ImGui::PushID(label);
	    ImGuiID id = ImGui::GetID("");

	    // Lookup O(log N)
	    PlotVarData& pvd = g_PlotVarsMap[id];

	    // Setup
	    if (pvd.Data.capacity() != buffer_size)
	    {
	        pvd.Data.resize(buffer_size);
	        memset(&pvd.Data[0], 0, sizeof(float) * buffer_size);
	        pvd.DataInsertIdx = 0;
	        pvd.LastFrame = -1;
	    }

	    // Insert (avoid unnecessary modulo operator)
	    if (pvd.DataInsertIdx == buffer_size)
	        pvd.DataInsertIdx = 0;
	    int display_idx = pvd.DataInsertIdx;
	    if (value != FLT_MAX)
	        pvd.Data[pvd.DataInsertIdx++] = value;

	    // Draw
	    int current_frame = ImGui::GetFrameCount();
	    if (pvd.LastFrame != current_frame)
	    {
	    	std::string hint = std::string(label) + " " + std::to_string((int)value);
	        ImGui::PlotLines("##plot", &pvd.Data[0], buffer_size, pvd.DataInsertIdx, hint.c_str(), scale_min, scale_max, ImVec2(width, 40));
	        pvd.LastFrame = current_frame;
	    }

	    ImGui::PopID();
	}

	void PlotVarFlushOldEntries()
	{
	    int current_frame = ImGui::GetFrameCount();
	    for (PlotVarsMap::iterator it = g_PlotVarsMap.begin(); it != g_PlotVarsMap.end(); )
	    {
	        PlotVarData& pvd = it->second;
	        if (pvd.LastFrame < current_frame - std::max(400,(int)pvd.Data.size()))
	            it = g_PlotVarsMap.erase(it);
	        else
	            ++it;
	    }
	}
}