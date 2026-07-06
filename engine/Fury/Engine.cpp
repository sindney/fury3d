#include <SFML/Window.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "Fury/BufferManager.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Engine.h"
#include "Fury/GLLoader.h"
#include "Fury/Gui.h"
#include "Fury/InputUtil.h"
#include "Fury/Log.h"
#include "Fury/MeshUtil.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/ThreadUtil.h"
#include "Fury/Vector4.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fury
{
	namespace
	{
		// Read the SFML window's back-buffer and write it to `path` as an
		// 8-bit RGBA PNG. Used by the --screenshot debug capture path.
		// Returns true on success.
		bool WriteBackBufferAsPng(const std::string &path, sf::Window &window)
		{
			const sf::Vector2u sz = window.getSize();
			const unsigned int w = sz.x;
			const unsigned int h = sz.y;
			if (w == 0 || h == 0)
			{
				FURYE << "WriteBackBufferAsPng: window has zero size";
				return false;
			}
			const std::size_t row_bytes = static_cast<std::size_t>(w) * 4;
			std::vector<unsigned char> pixels(row_bytes * h);
			glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
				GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

			// OpenGL's read origin is bottom-left, PNG's is top-left.
			std::vector<unsigned char> flipped(pixels.size());
			for (unsigned int y = 0; y < h; ++y)
			{
				std::memcpy(&flipped[y * row_bytes],
					&pixels[(h - 1 - y) * row_bytes],
					row_bytes);
			}

			const int rc = stbi_write_png(path.c_str(),
				static_cast<int>(w), static_cast<int>(h),
				4, flipped.data(), static_cast<int>(row_bytes));
			if (rc == 0)
			{
				FURYE << "stbi_write_png failed for path '" << path << "'";
				return false;
			}
			return true;
		}
	}
	Signal<float>::Ptr Engine::OnUpdate = Signal<float>::Create();

	Signal<>::Ptr Engine::OnFixedUpdate = Signal<>::Create();

	bool Engine::Initialize(sf::Window &window, int numThreads, LogLevel level, const char* logfile,
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
		// Clear Scene::Active / Pipeline::Active / MeshUtil's primitive
		// caches NOW, while the GL context is still alive. These are
		// static shared_ptrs; sol2's state destructor doesn't release
		// them (the Lua bindings assign to them by value, transferring
		// ownership out of Lua's hands). Without this, they survive
		// until the C runtime's static-cleanup phase, which runs AFTER
		// `sf::Window`'s destructor has already torn down the GL
		// context. The Scene's destructor then walks the entity map
		// and each Mesh's destructor calls glDeleteVertexArrays on a
		// dead context — UB, typically a segfault, plus spurious
		// "Tangent/Normal data dirty" warnings from a render that runs
		// after the meshes are already destroyed.
		Scene::Active.reset();
		Pipeline::Active.reset();
		MeshUtil::Reset();

		Editor::Shutdown();
		Gui::Shutdown();
	}

	std::pair<int, int> Engine::GetGLVersion()
	{
		return std::make_pair<int, int>(gl::GetMajorVersion(), gl::GetMinorVersion());
	}

	void Engine::Run(sf::Window &window, const EngineCallbacks &cb)
	{
		Run(window, cb, EngineOptions{});
	}

	void Engine::Run(sf::Window &window, const EngineCallbacks &cb, const EngineOptions &opts)
	{
		if (cb.OnInit) cb.OnInit();

#ifdef _FURY_GUI_IMP_
		Gui::Initialize(&window, opts.gui_scale, opts.gui_font_scale);
		Editor::Initialize();
#endif

		window.setFramerateLimit(opts.max_fps < 0 ? 0u : static_cast<unsigned int>(opts.max_fps));
		FURYD << "framerate cap: " << opts.max_fps;
		FURYD << "gui_scale: " << opts.gui_scale;
		FURYD << "gui_font_scale: " << opts.gui_font_scale;

		const std::int32_t SKIP_TICKS = 1000 / 25;       // 25 Hz fixed
		const int MAX_FRAMESKIP = 5;

		sf::Clock clock;
		std::int32_t next_game_tick = clock.getElapsedTime().asMilliseconds();
		bool running = true;
		int frame_index = 0;

		while (window.isOpen() && running)
		{
			RenderUtil::Instance()->BeginFrame();

			while (const std::optional event = window.pollEvent())
			{
				if (event->is<sf::Event::Closed>())
				{
					running = false;
					break;
				}
				sf::Event ev = *event;
				HandleEvent(ev);
			}

			int numLoops = 0;
			while (clock.getElapsedTime().asMilliseconds() > next_game_tick && numLoops < MAX_FRAMESKIP && running)
			{
				if (cb.OnFixedUpdate) cb.OnFixedUpdate();
				FixedUpdate();
				next_game_tick += SKIP_TICKS;
				numLoops++;
			}

			std::int32_t elapsed = clock.getElapsedTime().asMilliseconds();
			next_game_tick -= elapsed;

			// Wall-clock dt (real seconds since last frame). The fixed-tick
			// loop above uses `clock` directly; we restart it here AFTER
			// reading the elapsed-ms slice so the next iteration's
			// `clock.getElapsedTime()` measures from this point. Scripts
			// can sum dt across frames to get true elapsed seconds.
			float dt = clock.restart().asSeconds();

		Gui::NewFrame(dt);
		// Editor::Tick must run AFTER Gui::NewFrame and BEFORE the user
		// on_update callback. Editor::Tick builds the editor's ImGui
		// windows (menu bar, dockspace, built-in windows, gizmo) into the
		// current frame's draw lists.
		Editor::Tick();
		if (cb.OnUpdate) cb.OnUpdate(dt);
		Update(dt);

		// Editor post-render hook: drives the viewport-picking state
		// machine and the selection-visualization overlay after the user
		// pipeline has rendered the 3D scene and before Gui::Render
		// flushes ImGui draws. With WITH_EDITOR=OFF this resolves to an
		// inline no-op.
		Editor::TickPostRender();

		// Flush ImGui draw lists to the default framebuffer LAST so the
		// editor (and any script-side floating windows) composite on top
		// of the 3D scene — including the Viewport window's ImGui::Image,
		// which samples the scene's offscreen render target. This runs
		// after TickPostRender so the selection overlay (drawn into the
		// render target) is visible inside the Viewport window this frame.
		Gui::Render();

		window.display();

			RenderUtil::Instance()->EndFrame();

			++frame_index;
			if (!opts.screenshot_path.empty() && frame_index == opts.screenshot_frame)
			{
				const bool ok = WriteBackBufferAsPng(opts.screenshot_path, window);
				if (ok)
				{
					FURYI << "captured screenshot to " << opts.screenshot_path
						<< " at frame " << frame_index;
					if (opts.exit_code_out) *opts.exit_code_out = 0;
				}
				else
				{
					FURYE << "screenshot capture failed for path '"
						<< opts.screenshot_path << "'";
					if (opts.exit_code_out) *opts.exit_code_out = 1;
				}
				running = false;
			}
		}

		if (cb.OnShutdown) cb.OnShutdown();
	}
}