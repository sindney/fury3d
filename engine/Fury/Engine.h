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

	class FURY_API Engine
	{
	public:

		static bool Initialize(sf::Window &window, float guiScale, int numThreads,
			LogLevel level = LogLevel::EROR, const char* logfile = nullptr,
			bool console = true, const LogFormatter &formatter = Formatter::Simple, bool append = false);

		static void HandleEvent(sf::Event &event);

		static Signal<float>::Ptr OnUpdate;

		static Signal<>::Ptr OnFixedUpdate;

		static void Update(float dt);

		static void FixedUpdate();

		static void Shutdown();

		static void Run(sf::Window &window, const EngineCallbacks &cb);

		static std::pair<int, int> GetGLVersion();
	};
}

#endif // _FURY_ENGINE_H_