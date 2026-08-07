#include <SFML/Window.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "Fury/BufferManager.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Engine.h"
#include "Fury/Macros.h"
#include "Fury/GLLoader.h"
#include "Fury/Gui.h"
#include "Fury/InputUtil.h"
#include "Fury/Log.h"
#include "Fury/MeshUtil.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/ThreadUtil.h"
#include "Fury/Vector4.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if PLATFORM_WINDOWS
#include <commctrl.h>
#include <windowsx.h>
#include "ImGui/imgui.h"
namespace
{
	HWND s_FurySubclassHwnd = nullptr;

	LRESULT CALLBACK FurySubclassProc(
		HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
		UINT_PTR /*subclassId*/, DWORD_PTR /*refData*/)
	{
		if (msg == WM_SETCURSOR)
		{
			const WORD hit = LOWORD(lParam);
			if (hit != HTCLIENT)
			{
				if (ImGui::GetCurrentContext() != nullptr)
					ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
				DefWindowProcW(hwnd, WM_SETCURSOR, wParam, lParam);
				return TRUE;
			}
			if (ImGui::GetCurrentContext() != nullptr)
				ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	void InstallResizeCursorHook(sf::Window &window)
	{
		if (s_FurySubclassHwnd != nullptr) return;
		const sf::WindowHandle hwnd = window.getNativeHandle();
		if (hwnd == nullptr) return;
		SetWindowSubclass(hwnd, FurySubclassProc, 1, 0);
		s_FurySubclassHwnd = hwnd;
	}

	void RemoveResizeCursorHook()
	{
		if (s_FurySubclassHwnd == nullptr) return;
		RemoveWindowSubclass(s_FurySubclassHwnd, FurySubclassProc, 1);
		s_FurySubclassHwnd = nullptr;
	}
}
#else // !PLATFORM_WINDOWS
namespace
{
	void InstallResizeCursorHook(sf::Window &) {}
	void RemoveResizeCursorHook() {}
}
#endif // PLATFORM_WINDOWS

namespace fury
{
#if PLATFORM_MACOS
	// Defined in Engine_dpi_mac.mm (compiled only on macOS).
	float furyGetMacOSBackingScale();
#endif

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

	namespace { float s_FixedTickAlpha = 0.0f; }

	namespace
	{
		bool s_NeedsFocusSeed = true;
		int s_DroppedUnknownKeys = 0;
	}

	float Engine::GetSystemDPI()
	{
#if PLATFORM_WINDOWS
		return static_cast<float>(GetDpiForSystem()) / 96.0f;
#else
		// 1.0 on macOS/Linux: the OS already scales for Retina; applying
		// the backing factor here double-sizes the UI.
		return 1.0f;
#endif
	}

	bool Engine::Initialize(sf::Window &window, int numThreads, LogLevel level, const char* logfile,
		bool console, const LogFormatter &formatter, bool append)
	{
		Log<0>::Initialize(std::move(level), std::move(logfile), std::move(console), formatter, std::move(append));

		ThreadUtil::Initialize(static_cast<size_t>(numThreads));
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

		Editor::SetWindowForPersistence(&window);

		// Bootstrap focus state. Windows does not auto-fire WM_SETFOCUS
		// for a window shown via CreateWindowW(WS_VISIBLE), and SFML
		// only maps WM_SETFOCUS / WM_KILLFOCUS -- so without this seed
		// m_WindowFocused stays false on first launch and the editor's
		// drag gate silently fails. Cross-platform: window.hasFocus()
		// is a generic SFML call.
		if (window.hasFocus())
		{
			InputUtil::Instance()->m_WindowFocused = true;
			const sf::Vector2i pos = sf::Mouse::getPosition(window);
			InputUtil::Instance()->m_MousePosition = std::make_pair(static_cast<int>(pos.x), static_cast<int>(pos.y));
			InputUtil::Instance()->OnWindowFocus->Emit(true);
		}

		int flag = gl::LoadGLFunctions();

		RenderUtil::Initialize();

		BufferManager::Initialize();

		PhysicsWorld::Initialize();
		PhysicsWorld::Instance()->Subscribe();

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

	void Engine::HandleEvent(sf::Event &event, sf::Window &window)
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
			// Drop all held keys / mouse buttons. While the window is
			// unfocused (e.g. a native file dialog is open) SFML won't
			// deliver release events, so any key held when focus was
			// lost would read as "down" forever after -- the
			// "camera slides backwards after File -> Open" symptom.
			// ResetTransientInputState also flags the focus-gain seed
			// (see s_NeedsFocusSeed below) so the next FocusGained
			// re-seeds the cursor position from the live window.
			inputMgr->ResetTransientInputState();
			inputMgr->OnWindowFocus->Emit(false);
			s_NeedsFocusSeed = true;
		}
		else if (event.is<sf::Event::FocusGained>())
		{
			// One-shot cursor seed on every focus-gain transition:
			// re-read the live mouse position so the editor's hit-tests
			// see a real cursor after a focus round trip.
			if (s_NeedsFocusSeed)
			{
				const sf::Vector2i pos = sf::Mouse::getPosition(window);
				int mx = static_cast<int>(pos.x);
				int my = static_cast<int>(pos.y);
				inputMgr->m_MousePosition = std::make_pair(mx, my);
				// ResetTransientInputState re-zeros m_MousePosition;
				// re-write it after the reset so the seed survives.
				inputMgr->ResetTransientInputState();
				inputMgr->m_MousePosition = std::make_pair(mx, my);
				inputMgr->OnMouseMove->Emit(std::move(mx), std::move(my));
				s_NeedsFocusSeed = false;
			}
			inputMgr->m_WindowFocused = true;
			inputMgr->OnWindowFocus->Emit(true);
		}
		else if (const auto* text = event.getIf<sf::Event::TextEntered>())
		{
			size_t unicode = static_cast<size_t>(text->unicode);
			inputMgr->OnTextEntered->Emit(std::move(unicode));
		}
		else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>())
		{
			auto* key = keyPressed;
#if PLATFORM_WINDOWS
			// Drop unknown / out-of-range key codes to prevent an OOB
			// write into m_KeyDown[]. SFML's Win32 backend returns
			// Key::Unknown (-1) for IME virtual keys (e.g. VK_PROCESSKEY)
			// and the cast to unsigned would index 0xFFFFFFFF.
			if (key->code == sf::Keyboard::Key::Unknown
				|| static_cast<unsigned int>(key->code) >= sf::Keyboard::KeyCount)
			{
				++s_DroppedUnknownKeys;
				return;
			}
#endif
			inputMgr->m_KeyDown[static_cast<unsigned int>(key->code)] = true;
			sf::Keyboard::Key code = key->code;
			inputMgr->OnKeyDown->Emit(std::move(code));
		}
		else if (const auto* keyReleased = event.getIf<sf::Event::KeyReleased>())
		{
			auto* key = keyReleased;
#if PLATFORM_WINDOWS
			if (key->code == sf::Keyboard::Key::Unknown
				|| static_cast<unsigned int>(key->code) >= sf::Keyboard::KeyCount)
			{
				++s_DroppedUnknownKeys;
				return;
			}
#endif
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
		else if (const auto* btnPressed = event.getIf<sf::Event::MouseButtonPressed>())
		{
			auto* btn = btnPressed;
			inputMgr->m_MouseDown[static_cast<unsigned int>(btn->button)] = true;
			sf::Mouse::Button b = btn->button;
			int bx = btn->position.x;
			int by = btn->position.y;
			inputMgr->OnMouseDown->Emit(std::move(b), std::move(bx), std::move(by));
		}
		else if (const auto* btnReleased = event.getIf<sf::Event::MouseButtonReleased>())
		{
			auto* btn = btnReleased;
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
#if PLATFORM_WINDOWS
		RemoveResizeCursorHook();
#endif

		// Release GL resources while the context is still alive; statics
		// and singletons would otherwise destruct after it is gone.
		Scene::Active.reset();
		Pipeline::Active.reset();
		MeshUtil::Reset();
		RenderUtil::Instance().reset();

		// After Scene::Active: component detach destroys Jolt bodies,
		// which needs the physics world still alive.
		if (PhysicsWorld::Exists())
			PhysicsWorld::Instance().reset();

		Editor::Shutdown();
		Gui::Shutdown();
		Editor::SetWindowForPersistence(nullptr);
	}

	std::pair<int, int> Engine::GetGLVersion()
	{
		return std::make_pair<int, int>(gl::GetMajorVersion(), gl::GetMinorVersion());
	}

	float Engine::GetFixedTickAlpha()
	{
		return s_FixedTickAlpha;
	}

	float Engine::GetFixedDt()
	{
		return 1.0f / 25.0f;
	}

	void Engine::Run(sf::Window &window, const EngineCallbacks &cb)
	{
		Run(window, cb, EngineOptions{});
	}

	void Engine::Run(sf::Window &window, const EngineCallbacks &cb, const EngineOptions &opts)
	{
		if (cb.OnInit) cb.OnInit();

		// Compute the effective gui_scale from the system DPI. The caller
		// can opt into system-DPI scaling by passing gui_scale = 0.0f
		// (sentinel for "use system DPI") or by passing
		// dpi_aware_override = true to compose the system DPI with an
		// explicit gui_scale. The effective gui_font_scale follows the
		// same sentinel so the font density tracks the widget layout
		// on HiDPI displays -- without this, widgets are 2x but text
		// is still 1x and the editor looks "small". See
		// platform-window-dpi spec.
		const float systemDpi = GetSystemDPI();
		float effectiveScale = opts.gui_scale;
		const char *scaleSource = "explicit";
		if (opts.gui_scale == 0.0f)
		{
			effectiveScale = systemDpi;
			scaleSource = "system DPI";
		}
		else if (opts.dpi_aware_override)
		{
			effectiveScale = opts.gui_scale * systemDpi;
			scaleSource = "explicit * system DPI";
		}

		float effectiveFontScale = opts.gui_font_scale;
		if (opts.gui_font_scale == 0.0f) effectiveFontScale = effectiveScale;

#ifdef _FURY_GUI_IMP_
		Gui::Initialize(&window, effectiveScale, effectiveFontScale);
		Editor::Initialize();
#endif

#if PLATFORM_WINDOWS
		InstallResizeCursorHook(window);
#endif

		window.setFramerateLimit(opts.max_fps < 0 ? 0u : static_cast<unsigned int>(opts.max_fps));
		FURYD << "framerate cap: " << opts.max_fps;
		FURYD << "gui_scale: " << opts.gui_scale << " (" << scaleSource << ")"
		      << ", effective: " << effectiveScale;
		FURYD << "gui_font_scale: " << opts.gui_font_scale
		      << ", effective: " << effectiveFontScale;
		FURYD << "system DPI: " << systemDpi;
		FURYD << "dpi_aware_override: " << opts.dpi_aware_override;

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
				HandleEvent(ev, window);
			}

			int numLoops = 0;
			while (clock.getElapsedTime().asMilliseconds() > next_game_tick && numLoops < MAX_FRAMESKIP && running)
			{
				if (cb.OnFixedUpdate) cb.OnFixedUpdate();
				FixedUpdate();
				next_game_tick += SKIP_TICKS;
				numLoops++;
			}

		// Render interpolation alpha between the last and next fixed
		// tick (the FixedUpdate loop above). Clamped to [0,1) so a
		// long stall doesn't overshoot.
		{
				std::int32_t now_ms = clock.getElapsedTime().asMilliseconds();
				std::int32_t last_tick_ms = next_game_tick - SKIP_TICKS;
				float a = static_cast<float>(now_ms - last_tick_ms) / static_cast<float>(SKIP_TICKS);
				if (a < 0.0f) a = 0.0f;
				else if (a >= 1.0f) a = 0.999999f;
				s_FixedTickAlpha = a;
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
		// of the 3D scene -- including the Viewport window's ImGui::Image,
		// which samples the scene's offscreen render target. This runs
		// after TickPostRender so the selection overlay (drawn into the
		// render target) is visible inside the Viewport window this frame.
		Gui::Render();

		window.display();

			RenderUtil::Instance()->EndFrame();

			// End-of-frame summary of dropped key events. One FURYW
			// line per overflow frame; an active IME can produce
			// hundreds per frame so per-event logging is too chatty.
			if (s_DroppedUnknownKeys > 0)
			{
				FURYW << "rejected " << s_DroppedUnknownKeys
					<< " invalid KeyPressed/KeyReleased events this frame";
				s_DroppedUnknownKeys = 0;
			}

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