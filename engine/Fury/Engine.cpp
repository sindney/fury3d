#include <SFML/Window.hpp>

#include "Fury/BufferManager.h"
#include "Fury/Engine.h"
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
	Signal<float>::Ptr Engine::OnUpdate = Signal<float>::Create();

	Signal<>::Ptr Engine::OnFixedUpdate = Signal<>::Create();

	bool Engine::Initialize(sf::Window &window, float guiScale, int numThreads, LogLevel level, const char* logfile,
		bool console, const LogFormatter &formatter, bool append)
	{
		Log<0>::Initialize(std::move(level), std::move(logfile), std::move(console), formatter, std::move(append));

		ThreadUtil::Initialize(std::move(numThreads));
		ThreadUtil::Instance()->SetMainThread();

		FURYD << ThreadUtil::Instance()->GetWorkerCount() << " thread launched!";
		FURYD << "Window width: " << window.getSize().x << ", height: " << window.getSize().y;

		MeshUtil::m_UnitQuad = MeshUtil::CreateQuad("quad_mesh", Vector4(-1.0f, -1.0f, 0.0f), Vector4(1.0f, 1.0f, 0.0f));
		MeshUtil::m_UnitCube = MeshUtil::CreateCube("cube_mesh", Vector4(-1.0f), Vector4(1.0f));
		MeshUtil::m_UnitIcoSphere = MeshUtil::CreateIcoSphere("ico_sphere_mesh", 1.0f, 2);
		MeshUtil::m_UnitSphere = MeshUtil::CreateSphere("sphere_mesh", 1.0f, 20, 20);
		MeshUtil::m_UnitCylinder = MeshUtil::CreateCylinder("cylinder_mesh", 1.0f, 1.0f, 1.0f, 4, 10);
		MeshUtil::m_UnitCone = MeshUtil::CreateCylinder("cone_mesh", 0.0f, 1.0f, 1.0f, 4, 10);

		InputUtil::Initialize(window.getSize().x, window.getSize().y);

		int flag = gl::LoadGLFunctions();

		RenderUtil::Initialize();

		BufferManager::Initialize();

#ifdef _FURY_GUI_IMP_
		Gui::Initialize(&window, guiScale);
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

		if (event.is<sf::Event::Closed>())
		{
			inputMgr->OnWindowClosed->Emit();
		}
		else if (const auto* resized = event.getIf<sf::Event::Resized>())
		{
			int w = static_cast<int>(resized->size.x);
			int h = static_cast<int>(resized->size.y);
			inputMgr->m_WindowSize.first = w;
			inputMgr->m_WindowSize.second = h;
			inputMgr->OnWindowResized->Emit(std::move(w), std::move(h));
		}
		else if (event.is<sf::Event::FocusLost>())
		{
			inputMgr->m_WindowFocused = false;
			inputMgr->OnWindowFocus->Emit(false);
		}
		else if (event.is<sf::Event::FocusGained>())
		{
			inputMgr->m_WindowFocused = true;
			inputMgr->OnWindowFocus->Emit(true);
		}
		else if (const auto* text = event.getIf<sf::Event::TextEntered>())
		{
			size_t unicode = static_cast<size_t>(text->unicode);
			inputMgr->OnTextEntered->Emit(std::move(unicode));
		}
		else if (const auto* key = event.getIf<sf::Event::KeyPressed>())
		{
			inputMgr->m_KeyDown[static_cast<unsigned int>(key->code)] = true;
			sf::Keyboard::Key code = key->code;
			inputMgr->OnKeyDown->Emit(std::move(code));
		}
		else if (const auto* key = event.getIf<sf::Event::KeyReleased>())
		{
			inputMgr->m_KeyDown[static_cast<unsigned int>(key->code)] = false;
			sf::Keyboard::Key code = key->code;
			inputMgr->OnKeyUp->Emit(std::move(code));
		}
		else if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>())
		{
			inputMgr->m_MouseWheel = wheel->delta;
			float delta = wheel->delta;
			int wx = wheel->position.x;
			int wy = wheel->position.y;
			inputMgr->OnMouseWheel->Emit(std::move(delta), std::move(wx), std::move(wy));
		}
		else if (const auto* btn = event.getIf<sf::Event::MouseButtonPressed>())
		{
			inputMgr->m_MouseDown[static_cast<unsigned int>(btn->button)] = true;
			sf::Mouse::Button b = btn->button;
			int bx = btn->position.x;
			int by = btn->position.y;
			inputMgr->OnMouseDown->Emit(std::move(b), std::move(bx), std::move(by));
		}
		else if (const auto* btn = event.getIf<sf::Event::MouseButtonReleased>())
		{
			inputMgr->m_MouseDown[static_cast<unsigned int>(btn->button)] = false;
			sf::Mouse::Button b = btn->button;
			int bx = btn->position.x;
			int by = btn->position.y;
			inputMgr->OnMouseUp->Emit(std::move(b), std::move(bx), std::move(by));
		}
		else if (const auto* move = event.getIf<sf::Event::MouseMoved>())
		{
			inputMgr->m_MousePosition.first = move->position.x;
			inputMgr->m_MousePosition.second = move->position.y;
			int mx = move->position.x;
			int my = move->position.y;
			inputMgr->OnMouseMove->Emit(std::move(mx), std::move(my));
		}
		else if (event.is<sf::Event::MouseEntered>())
		{
			inputMgr->m_MouseInWindow = true;
			inputMgr->OnMouseEnter->Emit(true);
		}
		else if (event.is<sf::Event::MouseLeft>())
		{
			inputMgr->m_MouseInWindow = false;
			inputMgr->OnMouseEnter->Emit(false);
		}

		Gui::HandleEvent(event);
	}

	void Engine::Update(float dt)
	{
		ThreadUtil::Instance()->Update();
		OnUpdate->Emit(std::move(dt));
	}

	void Engine::FixedUpdate()
	{
		OnFixedUpdate->Emit();
	}

	void Engine::Shutdown()
	{
		// TODO: reset singleton's pointer.
		Gui::Shutdown();
	}

	std::pair<int, int> Engine::GetGLVersion()
	{
		return std::make_pair<int, int>(gl::GetMajorVersion(), gl::GetMinorVersion());
	}
}