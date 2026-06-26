#ifndef _FURY_EDITOR_PICKING_HPP_
#define _FURY_EDITOR_PICKING_HPP_

#ifdef WITH_EDITOR

#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace Picking
		{
			// Schedule a pick at the given pixel coordinate, expressed in
			// the engine window's full-window pixel space (origin = top-
			// left of the SFML window, ImGui-style top-down). The pixel
			// is captured now; the actual id-pass + readback runs across
			// the next two TickPostRender calls.
			void RequestPickAt(ImVec2 viewport_px);

			// Drive the picking state machine. Called from Engine.cpp once
			// per frame, after the user pipeline runs and before Gui::Render.
			void TickPostRender();

			// True when a pick is currently in flight (RenderRequested or
			// AwaitingReadback). Used by the gizmo to avoid fighting with
			// pick-frame draws.
			bool IsPickInFlight();
		}
	}
}

#endif // WITH_EDITOR

#endif // _FURY_EDITOR_PICKING_HPP_
