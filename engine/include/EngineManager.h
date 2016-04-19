#ifndef _FURY_ENGINE_MANAGER_H_
#define _FURY_ENGINE_MANAGER_H_

#include <iostream>
#include <functional>
#include <string>

#include "Log.h"

namespace fury
{
	class FURY_API EngineManager 
	{
	public:

		static bool Initialize(int numThreads, LogLevel level = LogLevel::EROR, const LogFormatter &formatter = Formatter::Simple,
			bool console = true, const char* logfile = nullptr, bool append = false);

		static std::pair<int, int> GetGLVersion();

	private:

		static bool SetupGL();
	};
}

#endif // _FURY_ENGINE_MANAGER_H_