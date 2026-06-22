#ifndef _FURY_ENGINE_H_
#define _FURY_ENGINE_H_

#include <iostream>
#include <functional>
#include <string>

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>

#include "Fury/Log.h"
#include "Fury/Signal.h"

namespace fury
{
	struct EngineCallbacks
	{
		std::function<void()> OnInit;
		std::function<void(float)> OnUpdate;
		std::function<void()> OnFixedUpdate;
		std::function<void()> OnShutdown;
	};

	// Runtime options for Engine::Run. Defaults are deliberately conservative
	// — see openspec changes/fix-demo-fps-profiler-retina for rationale.
	struct EngineOptions
	{
		// Frame-rate cap applied via sf::Window::setFramerateLimit. 0 disables.
		int max_fps = 144;
		// Multiplier passed to ImGuiStyle::ScaleAllSizes.
		float gui_scale = 1.0f;
		// Value assigned to ImGuiIO::FontGlobalScale.
		float gui_font_scale = 1.0f;
	};

	class FURY_API Engine
	{
	public:

		static bool Initialize(sf::Window &window, int numThreads,
			LogLevel level = LogLevel::EROR, const char* logfile = nullptr,
			bool console = true, const LogFormatter &formatter = Formatter::Simple, bool append = false);

		static void HandleEvent(sf::Event &event);

		static Signal<float>::Ptr OnUpdate;

		static Signal<>::Ptr OnFixedUpdate;

		static void Update(float dt);

		static void FixedUpdate();

		static void Shutdown();

		static void Run(sf::Window &window, const EngineCallbacks &cb);

		static void Run(sf::Window &window, const EngineCallbacks &cb, const EngineOptions &opts);

		static std::pair<int, int> GetGLVersion();
	};
}

#endif // _FURY_ENGINE_H_