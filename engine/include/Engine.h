#ifndef _FURY_ENGINE_H_
#define _FURY_ENGINE_H_

#include <iostream>
#include <functional>
#include <string>

// #include <omp.h>
#include <plog/Log.h>

#include "Macros.h"

namespace fury
{
	class FURY_API Engine
	{
	private:

		static std::string m_AbsPath;

	public:

		static void InitPlog(plog::Severity severity, plog::IAppender* appender)
		{
			plog::init(severity, appender);
		}

		static bool InitGL();

		static void GetGLVersion(int &major, int &minor);
	};
}

#endif // _FURY_ENGINE_H_