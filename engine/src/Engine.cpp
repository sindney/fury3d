#include "Debug.h"
#include "Engine.h"
#include "GLLoader.h"

namespace fury
{
	bool Engine::InitGL()
	{
		int flag = gl::LoadGLFunctions();
		if (flag == 1)
		{
			return true;
		}

		if (flag < 1)
		{
			LOGE << "Failed to load gl functions.";
		}
		else
		{
			LOGE << "Failed to load " << flag - 1 << " gl functions.";
		}

		return false;
	}

	void Engine::GetGLVersion(int &major, int &minor)
	{
		major = gl::GetMajorVersion();
		minor = gl::GetMinorVersion();
	}
}