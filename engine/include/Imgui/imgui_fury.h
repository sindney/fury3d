#ifndef _FURY_IMGUI_H_
#define _FURY_IMGUI_H_

#include <float.h>

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>

#include "../Macros.h"
#include "imconfig.h"

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

namespace ImGui
{
	// Plot value over time
	// Pass FLT_MAX value to draw without adding a new value
	void IMGUI_API PlotVar(const char* label, float value, float scale_min = FLT_MAX, float scale_max = FLT_MAX, float width = 200, size_t buffer_size = 120);

	// Call this periodically to discard old/unused data
	void IMGUI_API PlotVarFlushOldEntries();
}

#endif // _FURY_IMGUI_H_