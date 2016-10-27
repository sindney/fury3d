#include <SFML/Window.hpp>

#include "Fury/BufferManager.h"
#include "Fury/Engine.h"
#include "Fury/FbxParser.h"
#include "Fury/GLLoader.h"
#include "Fury/Gui.h"
#include "Fury/InputUtil.h"
#include "Fury/Log.h"
#include "Fury/MeshUtil.h"
#include "Fury/RenderUtil.h"
#include "Fury/ThreadUtil.h"
#include "Fury/Vector4.h"

namespace fury
{
	bool Engine::Initialize(sf::Window &window, int numThreads, LogLevel level, const char* logfile,
		bool console, const LogFormatter &formatter, bool append)
	{
		Log<0>::Initialize(std::move(level), std::move(logfile), std::move(console), formatter, std::move(append));

		ThreadUtil::Initialize(std::move(numThreads));
		ThreadUtil::Instance()->SetMainThread();

		FURYD << ThreadUtil::Instance()->GetWorkerCount() << " thread launched!";

		MeshUtil::m_UnitQuad = MeshUtil::CreateQuad("quad_mesh", Vector4(-1.0f, -1.0f, 0.0f), Vector4(1.0f, 1.0f, 0.0f));
		MeshUtil::m_UnitCube = MeshUtil::CreateCube("cube_mesh", Vector4(-1.0f), Vector4(1.0f));
		MeshUtil::m_UnitIcoSphere = MeshUtil::CreateIcoSphere("ico_sphere_mesh", 1.0f, 2);
		MeshUtil::m_UnitSphere = MeshUtil::CreateSphere("sphere_mesh", 1.0f, 20, 20);
		MeshUtil::m_UnitCylinder = MeshUtil::CreateCylinder("cylinder_mesh", 1.0f, 1.0f, 1.0f, 4, 10);
		MeshUtil::m_UnitCone = MeshUtil::CreateCylinder("cone_mesh", 0.0f, 1.0f, 1.0f, 4, 10);

		InputUtil::Initialize(window.getSize().x, window.getSize().y);

#ifdef _FURY_FBXPARSER_IMP_
		FbxParser::Initialize();
#endif

		int flag = gl::LoadGLFunctions();

		RenderUtil::Initialize();

		BufferManager::Initialize();

#ifdef _FURY_GUI_IMP_
		Gui::Initialize(&window);
#endif

		if (flag == 1)
			return true;

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

	void Engine::HandleEvent(sf::Event &event)
	{
		auto &inputMgr = InputUtil::Instance();
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

	std::pair<int, int> Engine::GetGLVersion()
	{
		return std::make_pair<int, int>(gl::GetMajorVersion(), gl::GetMinorVersion());
	}
}