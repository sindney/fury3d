#ifndef _FURY_IMGUI_H_
#define _FURY_IMGUI_H_

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>

#include "../Macros.h"

struct ImDrawData;

namespace fury
{
	namespace ImGuiBridge
	{
		bool FURY_API Initialize(sf::Window *window);

		void FURY_API Shutdown();

		void FURY_API HandleEvent(sf::Event &event);

		void FURY_API NewFrame(float frameTime);

		void FURY_API RenderDrawLists(ImDrawData* draw_data);
	}
}

#endif // _FURY_IMGUI_H_