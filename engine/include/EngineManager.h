#ifndef _FURY_ENGINE_MANAGER_H_
#define _FURY_ENGINE_MANAGER_H_

#include <iostream>
#include <functional>
#include <string>

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>

#include "Log.h"

namespace fury
{
	class FURY_API EngineManager 
	{
	public:

		static bool Initialize(sf::Window &window, int numThreads, LogLevel level = LogLevel::EROR, const char* logfile = nullptr, 
			bool console = true, const LogFormatter &formatter = Formatter::Simple, bool append = false);

		static void HandleEvent(sf::Event &event);

		static std::pair<int, int> GetGLVersion();
	};
}

#endif // _FURY_ENGINE_MANAGER_H_