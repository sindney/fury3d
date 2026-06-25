// dear imgui: Platform Backend for SFML 3.1
// This needs to be used along with a Renderer (e.g. OpenGL3, Vulkan, ...)
// (Custom written for fury3d — modeled after upstream imgui_impl_sdl3.{cpp,h}.)
//
// Implemented features:
//  [X] Platform: Mouse cursor shape and visibility (ImGuiBackendFlags_HasMouseCursors).
//  [X] Platform: Clipboard support.
//  [X] Platform: Keyboard arrays indexed using ImGuiKey values.
//
// You can use unmodified imgui_impl_* files in your project. See examples/
// folder for examples of using this. Prefer including the entire imgui/
// repository into your project (either as a copy or as a submodule), and only
// build the backends you need.

#pragma once
#include "imgui.h"      // IMGUI_IMPL_API
#ifndef IMGUI_DISABLE

namespace sf
{
    class Window;
    class Event;
}

IMGUI_IMPL_API bool ImGui_ImplSFML3_Init(sf::Window* window);
IMGUI_IMPL_API void ImGui_ImplSFML3_Shutdown();
IMGUI_IMPL_API void ImGui_ImplSFML3_NewFrame(sf::Window* window, float deltaTime);
IMGUI_IMPL_API void ImGui_ImplSFML3_ProcessEvent(const sf::Event& event);

#endif // #ifndef IMGUI_DISABLE
