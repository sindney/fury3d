#ifdef _FURY_GUI_IMP_

#ifndef _FURY_GUI_H_
#define _FURY_GUI_H_

#include <float.h>
#include <memory>

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>

#include "Fury/Macros.h"

struct ImDrawData;

namespace fury
{
	namespace Gui
	{
		bool FURY_API Initialize(sf::Window *window);

		void FURY_API Shutdown();

		void FURY_API HandleEvent(sf::Event &event);

		void FURY_API NewFrame(float frameTime);

		void FURY_API RenderDrawLists(ImDrawData* draw_data);

		void FURY_API ShowDefault(float dt);

		void FURY_API Render();
	}
}

#endif // _FURY_GUI_H_

#endif // _FURY_GUI_IMP_