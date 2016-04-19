#include "EngineManager.h"
#include "EntityManager.h"
#include "FbxParser.h"
#include "Log.h"
#include "GLLoader.h"
#include "MeshUtil.h"
#include "ThreadManager.h"
#include "Vector4.h"

namespace fury
{
	bool EngineManager::Initialize(int numThreads, LogLevel level, const LogFormatter &formatter,
		bool console, const char* logfile, bool append)
	{
		Log<0>::Initialize(std::move(level), formatter, std::move(console),
			std::move(logfile), std::move(append));

		ThreadManager::Initialize(std::move(numThreads));
		ThreadManager::Instance()->SetMainThread();

		FURYD << ThreadManager::Instance()->GetWorkerCount() << " thread launched!";

		MeshUtil::m_UnitQuad = MeshUtil::CreateQuad("quad_mesh", Vector4(-1.0f, -1.0f, 0.0f), Vector4(1.0f, 1.0f, 0.0f));
		MeshUtil::m_UnitCube = MeshUtil::CreateCube("cube_mesh", Vector4(-1.0f), Vector4(1.0f));
		MeshUtil::m_UnitIcoSphere = MeshUtil::CreateIcoSphere("ico_sphere_mesh", 1.0f, 2);
		MeshUtil::m_UnitSphere = MeshUtil::CreateSphere("sphere_mesh", 1.0f, 20, 20);
		MeshUtil::m_UnitCylinder = MeshUtil::CreateCylinder("cylinder_mesh", 1.0f, 1.0f, 1.0f, 4, 10);
		MeshUtil::m_UnitCone = MeshUtil::CreateCylinder("cone_mesh", 0.0f, 1.0f, 1.0f, 4, 10);

		EntityManager::Initialize();
		FbxParser::Initialize();

		return SetupGL();
	}

	bool EngineManager::SetupGL()
	{
		int flag = gl::LoadGLFunctions();
		if (flag == 1)
		{
			glEnable(GL_FRAMEBUFFER_SRGB);
			return true;
		}

		if (flag < 1)
		{
			FURYE << "Failed to load gl functions.";
		}
		else
		{
			FURYE << "Failed to load " << flag - 1 << " gl functions.";
		}

		return false;
	}

	std::pair<int, int> EngineManager::GetGLVersion()
	{
		return std::make_pair<int, int>(gl::GetMajorVersion(), gl::GetMinorVersion());
	}
}