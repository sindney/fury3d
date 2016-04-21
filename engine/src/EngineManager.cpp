#include <SFML/Window.hpp>

#include "EngineManager.h"
#include "EntityManager.h"
#include "FbxParser.h"
#include "GLLoader.h"
#include "InputManager.h"
#include "Log.h"
#include "MeshUtil.h"
#include "ThreadManager.h"
#include "Vector4.h"

namespace fury
{
	bool EngineManager::Initialize(sf::Window &window, int numThreads, LogLevel level, const char* logfile,
		bool console, const LogFormatter &formatter, bool append)
	{
		Log<0>::Initialize(std::move(level), std::move(logfile), std::move(console), formatter, std::move(append));

		ThreadManager::Initialize(std::move(numThreads));
		ThreadManager::Instance()->SetMainThread();

		FURYD << ThreadManager::Instance()->GetWorkerCount() << " thread launched!";

		MeshUtil::m_UnitQuad = MeshUtil::CreateQuad("quad_mesh", Vector4(-1.0f, -1.0f, 0.0f), Vector4(1.0f, 1.0f, 0.0f));
		MeshUtil::m_UnitCube = MeshUtil::CreateCube("cube_mesh", Vector4(-1.0f), Vector4(1.0f));
		MeshUtil::m_UnitIcoSphere = MeshUtil::CreateIcoSphere("ico_sphere_mesh", 1.0f, 2);
		MeshUtil::m_UnitSphere = MeshUtil::CreateSphere("sphere_mesh", 1.0f, 20, 20);
		MeshUtil::m_UnitCylinder = MeshUtil::CreateCylinder("cylinder_mesh", 1.0f, 1.0f, 1.0f, 4, 10);
		MeshUtil::m_UnitCone = MeshUtil::CreateCylinder("cone_mesh", 0.0f, 1.0f, 1.0f, 4, 10);

		InputManager::Initialize(window.getSize().x, window.getSize().y);
		EntityManager::Initialize();
		FbxParser::Initialize();

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

	void EngineManager::HandleEvent(sf::Event &event)
	{
		auto &inputMgr = InputManager::Instance();
		switch (event.type)
		{
		case sf::Event::Closed:
			inputMgr->OnWindowClosed.Emit();
			break;
		case sf::Event::Resized:
			inputMgr->m_WindowSize.first = event.size.width;
			inputMgr->m_WindowSize.second = event.size.height;
			inputMgr->OnWindowResized.Emit(std::move(event.size.width), std::move(event.size.height));
			break;
		case sf::Event::LostFocus:
			inputMgr->m_WindowFocused = false;
			inputMgr->OnWindowFocus.Emit(false);
			break;
		case sf::Event::GainedFocus:
			inputMgr->m_WindowFocused = true;
			inputMgr->OnWindowFocus.Emit(true);
			break;
		case sf::Event::TextEntered:
			inputMgr->OnTextEntered.Emit(std::move(event.text.unicode));
			break;
		case sf::Event::KeyPressed:
			inputMgr->m_KeyDown[event.key.code] = true;
			inputMgr->OnKeyDown.Emit(std::move(event.key.code));
			break;
		case sf::Event::KeyReleased:
			inputMgr->m_KeyDown[event.key.code] = false;
			inputMgr->OnKeyUp.Emit(std::move(event.key.code));
			break;
		case sf::Event::MouseWheelScrolled:
			inputMgr->m_MouseWheel = event.mouseWheelScroll.delta;
			inputMgr->OnMouseWheel.Emit(std::move(event.mouseWheelScroll.delta),
				std::move(event.mouseWheelScroll.x), std::move(event.mouseWheelScroll.y));
			break;
		case sf::Event::MouseButtonPressed:
			inputMgr->m_MouseDown[event.mouseButton.button] = true;
			inputMgr->OnMouseDown.Emit(std::move(event.mouseButton.button), std::move(event.mouseButton.x),
				std::move(event.mouseButton.y));
			break;
		case sf::Event::MouseButtonReleased:
			inputMgr->m_MouseDown[event.mouseButton.button] = false;
			inputMgr->OnMouseUp.Emit(std::move(event.mouseButton.button), std::move(event.mouseButton.x),
				std::move(event.mouseButton.y));
			break;
		case sf::Event::MouseMoved:
			inputMgr->m_MousePosition.first = event.mouseMove.x;
			inputMgr->m_MousePosition.second = event.mouseMove.y;
			inputMgr->OnMouseMove.Emit(std::move(event.mouseMove.x), std::move(event.mouseMove.y));
			break;
		case sf::Event::MouseEntered:
			inputMgr->m_MouseInWindow = true;
			inputMgr->OnMouseEnter.Emit(true);
			break;
		case sf::Event::MouseLeft:
			inputMgr->m_MouseInWindow = false;
			inputMgr->OnMouseEnter.Emit(false);
			break;
		default:
			break;
		}
	}

	std::pair<int, int> EngineManager::GetGLVersion()
	{
		return std::make_pair<int, int>(gl::GetMajorVersion(), gl::GetMinorVersion());
	}
}