#ifndef _FURY_EDITOR_THEME_H_
#define _FURY_EDITOR_THEME_H_

#include "ImGui/imgui.h"

namespace fury
{
    namespace Editor
    {
        // Dracula-derived but neutralized: black/dark-grey base, muted grey
        // interactables, single restrained accent (soft green) instead of Dracula's
        // blue/purple/pink pops. For long profiling sessions.
        inline void SetupImGuiProgrammerStyle()
        {
            ImGuiStyle& style = ImGui::GetStyle();
            ImVec4* colors = style.Colors;

            // --- 1. Sizing and Spacing (Clean & Balanced) ---
            style.WindowPadding = ImVec2(10.0f, 10.0f);
            style.FramePadding = ImVec2(6.0f, 4.0f);
            style.ItemSpacing = ImVec2(8.0f, 6.0f);
            style.ScrollbarSize = 14.0f;
            style.GrabMinSize = 12.0f;

            // --- 2. Borders & Rounding ---
            style.WindowRounding = 5.0f;
            style.FrameRounding = 3.0f;
            style.PopupRounding = 4.0f;
            style.ScrollbarRounding = 12.0f;
            style.GrabRounding = 3.0f;
            style.TabRounding = 4.0f;

            style.WindowBorderSize = 1.0f;
            style.FrameBorderSize = 1.0f;

            // --- 3. Neutral dark palette (black / dark grey, no color cast) ---
            // Base: #16181a | Panel: #1d2023 | Surface: #26292d | Edge: #33373c
            // Text: #d8dcdf | Muted: #7a828a | Accent: #6cc07a (soft green)

            // Text
            colors[ImGuiCol_Text] = ImVec4(0.85f, 0.86f, 0.87f, 1.00f); // #d8dcdf
            colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.51f, 0.54f, 1.00f); // #7a828a

            // Backgrounds
            colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f); // #16181a
            colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.13f, 0.14f, 1.00f); // #1d2023
            colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.13f, 0.98f);

            // Borders
            colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f); // #33373c
            colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

            // Frames (Inputs, etc.)
            colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.16f, 0.18f, 1.00f); // #26292d
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.26f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.30f, 0.33f, 1.00f);

            // Title Bars
            colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.13f, 0.14f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);

            // Menus
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);

            // Scrollbars
            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.26f, 0.28f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.34f, 0.37f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.42f, 0.45f, 1.00f);

            // Interactables (muted grey with a single soft-green accent)
            colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.75f, 0.48f, 1.00f); // #6cc07a
            colors[ImGuiCol_SliderGrab] = ImVec4(0.38f, 0.40f, 0.43f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.42f, 0.75f, 0.48f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.33f, 0.36f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.55f, 0.40f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.33f, 0.36f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.36f, 0.55f, 0.40f, 1.00f);

            // Separators and Resizing
            colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
            colors[ImGuiCol_SeparatorHovered] = ImVec4(0.32f, 0.34f, 0.37f, 1.00f);
            colors[ImGuiCol_SeparatorActive] = ImVec4(0.42f, 0.75f, 0.48f, 1.00f);
            colors[ImGuiCol_ResizeGrip] = ImVec4(0.24f, 0.26f, 0.28f, 0.80f);
            colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.32f, 0.34f, 0.37f, 1.00f);
            colors[ImGuiCol_ResizeGripActive] = ImVec4(0.42f, 0.75f, 0.48f, 1.00f);

            // Tabs
            colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.33f, 0.36f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
            colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
            colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);

            // Tables
            colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
            colors[ImGuiCol_TableBorderStrong] = ImVec4(0.24f, 0.26f, 0.29f, 1.00f);
            colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
            colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

            // Misc
            colors[ImGuiCol_PlotLines] = ImVec4(0.55f, 0.75f, 0.60f, 1.00f);
            colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.42f, 0.75f, 0.48f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(0.55f, 0.75f, 0.60f, 1.00f);
            colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.42f, 0.75f, 0.48f, 1.00f);
            colors[ImGuiCol_TextSelectedBg] = ImVec4(0.28f, 0.42f, 0.32f, 0.55f);
            colors[ImGuiCol_DragDropTarget] = ImVec4(0.42f, 0.75f, 0.48f, 0.95f);
            colors[ImGuiCol_NavHighlight] = ImVec4(0.42f, 0.75f, 0.48f, 1.00f);
            colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.85f, 0.86f, 0.87f, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.10f, 0.11f, 0.12f, 0.50f);
            colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.60f);

#ifdef IMGUI_HAS_DOCK
            colors[ImGuiCol_DockingPreview] = ImVec4(0.42f, 0.75f, 0.48f, 0.45f);
            colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
#endif
        }
    }
}

#endif // _FURY_EDITOR_THEME_H_
