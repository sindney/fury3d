#include "Fury/GameUI.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Window.hpp>

#include "Fury/AssetBackend.h"
#include "Fury/Engine.h"
#include "Fury/GameUIRenderer.h"
#include "Fury/Log.h"
#include "Fury/Scene.h"

namespace
{
	// RmlUi reads files through an Open/Read/Seek/Tell pull API; we serve
	// each open file from a fully-read byte buffer (UI-scale files) sourced
	// from the engine VFS.
	struct GameUIFileBuffer
	{
		std::vector<unsigned char> data;
		size_t pos = 0;
	};

	class GameUIFileInterface final : public Rml::FileInterface
	{
	public:

		Rml::FileHandle Open(const Rml::String &path) override
		{
			std::vector<unsigned char> bytes;
			// Content-relative first (scene working dir / Engine prefix),
			// then the raw path so absolute and cwd-relative paths work.
			if (!fury::AssetBackend::ReadAssetBytes(fury::Scene::ResolveAsset(path), bytes) &&
				!fury::AssetBackend::ReadAssetBytes(path, bytes))
			{
				return 0;
			}
			auto *buffer = new GameUIFileBuffer();
			buffer->data = std::move(bytes);
			return reinterpret_cast<Rml::FileHandle>(buffer);
		}

		void Close(Rml::FileHandle file) override
		{
			delete reinterpret_cast<GameUIFileBuffer *>(file);
		}

		size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
		{
			auto *fb = reinterpret_cast<GameUIFileBuffer *>(file);
			const size_t remaining = fb->data.size() - fb->pos;
			const size_t count = size < remaining ? size : remaining;
			if (count > 0)
			{
				std::memcpy(buffer, fb->data.data() + fb->pos, count);
				fb->pos += count;
			}
			return count;
		}

		bool Seek(Rml::FileHandle file, long offset, int origin) override
		{
			auto *fb = reinterpret_cast<GameUIFileBuffer *>(file);
			long base = 0;
			if (origin == SEEK_CUR) base = static_cast<long>(fb->pos);
			else if (origin == SEEK_END) base = static_cast<long>(fb->data.size());
			const long next = base + offset;
			if (next < 0 || static_cast<size_t>(next) > fb->data.size()) return false;
			fb->pos = static_cast<size_t>(next);
			return true;
		}

		size_t Tell(Rml::FileHandle file) override
		{
			return reinterpret_cast<GameUIFileBuffer *>(file)->pos;
		}
	};

	class GameUISystemInterface final : public Rml::SystemInterface
	{
	public:

		double GetElapsedTime() override
		{
			return static_cast<double>(fury::Engine::GetTime());
		}
	};

	std::unique_ptr<GameUISystemInterface> s_SystemInterface;
	std::unique_ptr<GameUIFileInterface> s_FileInterface;
	std::unique_ptr<fury::GameUIRenderer> s_Renderer;
	Rml::Context *s_Context = nullptr;
	sf::Window *s_Window = nullptr;
	float s_DpRatio = 1.0f;
	bool s_RmlInitialized = false;
	bool s_DebuggerInitialized = false;

	// SFML 3 -> RmlUi key map (upstream RmlUi_Platform_SFML table, SFML 2
	// compat stripped - fury3d is SFML 3.1 only).
	Rml::Input::KeyIdentifier ConvertKey(sf::Keyboard::Key key)
	{
		using SK = sf::Keyboard::Key;
		using KI = Rml::Input::KeyIdentifier;
		switch (key)
		{
		case SK::A: return KI::KI_A;
		case SK::B: return KI::KI_B;
		case SK::C: return KI::KI_C;
		case SK::D: return KI::KI_D;
		case SK::E: return KI::KI_E;
		case SK::F: return KI::KI_F;
		case SK::G: return KI::KI_G;
		case SK::H: return KI::KI_H;
		case SK::I: return KI::KI_I;
		case SK::J: return KI::KI_J;
		case SK::K: return KI::KI_K;
		case SK::L: return KI::KI_L;
		case SK::M: return KI::KI_M;
		case SK::N: return KI::KI_N;
		case SK::O: return KI::KI_O;
		case SK::P: return KI::KI_P;
		case SK::Q: return KI::KI_Q;
		case SK::R: return KI::KI_R;
		case SK::S: return KI::KI_S;
		case SK::T: return KI::KI_T;
		case SK::U: return KI::KI_U;
		case SK::V: return KI::KI_V;
		case SK::W: return KI::KI_W;
		case SK::X: return KI::KI_X;
		case SK::Y: return KI::KI_Y;
		case SK::Z: return KI::KI_Z;
		case SK::Num0: return KI::KI_0;
		case SK::Num1: return KI::KI_1;
		case SK::Num2: return KI::KI_2;
		case SK::Num3: return KI::KI_3;
		case SK::Num4: return KI::KI_4;
		case SK::Num5: return KI::KI_5;
		case SK::Num6: return KI::KI_6;
		case SK::Num7: return KI::KI_7;
		case SK::Num8: return KI::KI_8;
		case SK::Num9: return KI::KI_9;
		case SK::Numpad0: return KI::KI_NUMPAD0;
		case SK::Numpad1: return KI::KI_NUMPAD1;
		case SK::Numpad2: return KI::KI_NUMPAD2;
		case SK::Numpad3: return KI::KI_NUMPAD3;
		case SK::Numpad4: return KI::KI_NUMPAD4;
		case SK::Numpad5: return KI::KI_NUMPAD5;
		case SK::Numpad6: return KI::KI_NUMPAD6;
		case SK::Numpad7: return KI::KI_NUMPAD7;
		case SK::Numpad8: return KI::KI_NUMPAD8;
		case SK::Numpad9: return KI::KI_NUMPAD9;
		case SK::Left: return KI::KI_LEFT;
		case SK::Right: return KI::KI_RIGHT;
		case SK::Up: return KI::KI_UP;
		case SK::Down: return KI::KI_DOWN;
		case SK::Add: return KI::KI_ADD;
		case SK::Backspace: return KI::KI_BACK;
		case SK::Delete: return KI::KI_DELETE;
		case SK::Divide: return KI::KI_DIVIDE;
		case SK::End: return KI::KI_END;
		case SK::Enter: return KI::KI_RETURN;
		case SK::Escape: return KI::KI_ESCAPE;
		case SK::F1: return KI::KI_F1;
		case SK::F2: return KI::KI_F2;
		case SK::F3: return KI::KI_F3;
		case SK::F4: return KI::KI_F4;
		case SK::F5: return KI::KI_F5;
		case SK::F6: return KI::KI_F6;
		case SK::F7: return KI::KI_F7;
		case SK::F8: return KI::KI_F8;
		case SK::F9: return KI::KI_F9;
		case SK::F10: return KI::KI_F10;
		case SK::F11: return KI::KI_F11;
		case SK::F12: return KI::KI_F12;
		case SK::F13: return KI::KI_F13;
		case SK::F14: return KI::KI_F14;
		case SK::F15: return KI::KI_F15;
		case SK::Home: return KI::KI_HOME;
		case SK::Insert: return KI::KI_INSERT;
		case SK::LControl: return KI::KI_LCONTROL;
		case SK::LShift: return KI::KI_LSHIFT;
		case SK::Multiply: return KI::KI_MULTIPLY;
		case SK::Pause: return KI::KI_PAUSE;
		case SK::RControl: return KI::KI_RCONTROL;
		case SK::RShift: return KI::KI_RSHIFT;
		case SK::Space: return KI::KI_SPACE;
		case SK::Subtract: return KI::KI_SUBTRACT;
		case SK::Tab: return KI::KI_TAB;
		default: break;
		}
		return KI::KI_UNKNOWN;
	}

	int GetKeyModifierState()
	{
		int modifiers = 0;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
			modifiers |= Rml::Input::KM_SHIFT;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))
			modifiers |= Rml::Input::KM_CTRL;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt))
			modifiers |= Rml::Input::KM_ALT;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LSystem) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RSystem))
			modifiers |= Rml::Input::KM_META;
		return modifiers;
	}

	// Hovering/focusing the document background (root or body) is not a
	// capture; any other element is.
	bool IsBackgroundElement(Rml::Element *element)
	{
		if (element == nullptr) return true;
		Rml::ElementDocument *doc = element->GetOwnerDocument();
		if (doc == nullptr) return true;
		return element == doc;
	}
}

namespace fury
{
	namespace GameUI
	{
		bool EnsureInitialized()
		{
			if (s_RmlInitialized) return true;
			s_SystemInterface = std::make_unique<GameUISystemInterface>();
			s_FileInterface = std::make_unique<GameUIFileInterface>();
			s_Renderer = std::make_unique<fury::GameUIRenderer>();
			Rml::SetSystemInterface(s_SystemInterface.get());
			Rml::SetFileInterface(s_FileInterface.get());
			Rml::SetRenderInterface(s_Renderer.get());
			if (!Rml::Initialise())
			{
				FURYW << "GameUI: Rml::Initialise failed";
				s_Renderer.reset();
				s_SystemInterface.reset();
				s_FileInterface.reset();
				return false;
			}
			s_RmlInitialized = true;

			// Lua plugin initialisation lives in LuaBindings::Register (it has
			// the live lua_State). Calling here would only get Rml::Lua's
			// internal state, which the rest of the engine doesn't use.
			(void)0;

			// Windowless path (fury exec / cli): a nominal-size context so
			// layout, queries, and tests work without a window or GL.
			if (s_Window == nullptr && s_Context == nullptr)
				s_Context = Rml::CreateContext("main", Rml::Vector2i(1280, 720));
			return true;
		}

		bool Initialize(sf::Window *window, float dpRatio)
		{
			if (window == nullptr) return false;
			s_Window = window;
			s_DpRatio = dpRatio > 0.0f ? dpRatio : 1.0f;
			if (!EnsureInitialized()) return false;
			if (s_Context == nullptr)
			{
				const sf::Vector2u size = window->getSize();
				s_Context = Rml::CreateContext("main", Rml::Vector2i(
					static_cast<int>(size.x), static_cast<int>(size.y)));
			}
			if (s_Context == nullptr)
			{
				FURYW << "GameUI: failed to create RmlUi context";
				return false;
			}
			s_Context->SetDensityIndependentPixelRatio(s_DpRatio);
			if (std::getenv("FURY_UI_DEBUG") != nullptr)
				SetDebuggerVisible(true);
			return true;
		}

		void Shutdown()
		{
			if (!s_RmlInitialized) return;
			// Force our context off the registry so it is destroyed here,
			// while the lua_State is still alive. The dtor's listener
			// callbacks fire during this removal; after this returns,
			// Rml::Shutdown has nothing left of ours to destroy.
			if (s_Context != nullptr)
			{
				while (s_Context->GetNumDocuments() > 0)
					s_Context->UnloadDocument(s_Context->GetDocument(0));
				s_Context->Update();
				Rml::RemoveContext("main");
				s_Context = nullptr;
			}
			Rml::Shutdown();
			s_Window = nullptr;
			s_RmlInitialized = false;
			s_Renderer.reset();
			s_SystemInterface.reset();
			s_FileInterface.reset();
		}

		void Update(float)
		{
			if (s_Context != nullptr)
				s_Context->Update();
		}

		void HandleEvent(sf::Event &event)
		{
			if (s_Context == nullptr) return;
			if (const auto *resized = event.getIf<sf::Event::Resized>())
			{
				s_Context->SetDimensions(Rml::Vector2i(
					static_cast<int>(resized->size.x), static_cast<int>(resized->size.y)));
			}
			else if (const auto *moved = event.getIf<sf::Event::MouseMoved>())
			{
				s_Context->ProcessMouseMove(moved->position.x, moved->position.y, GetKeyModifierState());
			}
			else if (const auto *pressed = event.getIf<sf::Event::MouseButtonPressed>())
			{
				s_Context->ProcessMouseButtonDown(static_cast<int>(pressed->button), GetKeyModifierState());
			}
			else if (const auto *released = event.getIf<sf::Event::MouseButtonReleased>())
			{
				s_Context->ProcessMouseButtonUp(static_cast<int>(released->button), GetKeyModifierState());
			}
			else if (const auto *wheel = event.getIf<sf::Event::MouseWheelScrolled>())
			{
				const Rml::Vector2f delta = {
					wheel->wheel == sf::Mouse::Wheel::Horizontal ? -wheel->delta : 0.0f,
					wheel->wheel == sf::Mouse::Wheel::Vertical ? -wheel->delta : 0.0f,
				};
				s_Context->ProcessMouseWheel(delta, GetKeyModifierState());
			}
			else if (event.is<sf::Event::MouseLeft>())
			{
				s_Context->ProcessMouseLeave();
			}
			else if (const auto *text = event.getIf<sf::Event::TextEntered>())
			{
				Rml::Character character = Rml::Character(text->unicode);
				if (character == Rml::Character('\r'))
					character = Rml::Character('\n');
				if (text->unicode >= 32 || character == Rml::Character('\n'))
					s_Context->ProcessTextInput(character);
			}
			else if (const auto *keyDown = event.getIf<sf::Event::KeyPressed>())
			{
				s_Context->ProcessKeyDown(ConvertKey(keyDown->code), GetKeyModifierState());
			}
			else if (const auto *keyUp = event.getIf<sf::Event::KeyReleased>())
			{
				s_Context->ProcessKeyUp(ConvertKey(keyUp->code), GetKeyModifierState());
			}
		}

		Rml::Context *GetContext()
		{
			return s_Context;
		}

		Rml::ElementDocument *LoadDocument(const std::string &path)
		{
			if (!EnsureInitialized() || s_Context == nullptr) return nullptr;
			Rml::ElementDocument *doc = s_Context->LoadDocument(path);
			if (doc == nullptr)
				FURYW << "GameUI: failed to load document '" << path << "'";
			return doc;
		}

		Rml::ElementDocument *GetDocument(const std::string &path)
		{
			if (!EnsureInitialized() || s_Context == nullptr) return nullptr;
			// Our convention is the load path: Context::GetDocument matches
			// the document element's id, so walk by source URL first.
			for (int i = 0; i < s_Context->GetNumDocuments(); ++i)
			{
				Rml::ElementDocument *doc = s_Context->GetDocument(i);
				if (doc != nullptr && doc->GetSourceURL() == path)
					return doc;
			}
			return s_Context->GetDocument(path); // explicit-id documents
		}

		bool ShowDocument(const std::string &path)
		{
			Rml::ElementDocument *doc = GetDocument(path);
			if (doc == nullptr) return false;
			doc->Show();
			return true;
		}

		bool HideDocument(const std::string &path)
		{
			Rml::ElementDocument *doc = GetDocument(path);
			if (doc == nullptr) return false;
			doc->Hide();
			return true;
		}

		bool ToggleDocument(const std::string &path)
		{
			Rml::ElementDocument *doc = GetDocument(path);
			if (doc == nullptr) return false;
			if (doc->IsVisible()) doc->Hide();
			else doc->Show();
			return true;
		}

		bool CloseDocument(const std::string &path)
		{
			Rml::ElementDocument *doc = GetDocument(path);
			if (doc == nullptr) return false;
			doc->Close();
			return true;
		}

		bool WantCaptureMouse()
		{
			if (s_Context == nullptr) return false;
			return !IsBackgroundElement(s_Context->GetHoverElement());
		}

		bool WantCaptureKeyboard()
		{
			if (s_Context == nullptr) return false;
			return !IsBackgroundElement(s_Context->GetFocusElement());
		}

		void SetDebuggerVisible(bool visible)
		{
			if (!EnsureInitialized() || s_Context == nullptr) return;
			if (!s_DebuggerInitialized)
			{
				if (!Rml::Debugger::Initialise(s_Context))
				{
					FURYW << "GameUI: debugger initialise failed";
					return;
				}
				s_DebuggerInitialized = true;
			}
			Rml::Debugger::SetVisible(visible);
		}

		std::shared_ptr<void> BuildDrawSnapshot()
		{
			if (s_Context == nullptr || s_Renderer == nullptr) return nullptr;
			s_Renderer->BeginBuild(s_Context->GetDimensions());
			s_Context->Render();
			return s_Renderer->EndBuild();
		}

		void RenderSnapshot(const std::shared_ptr<void> &frame)
		{
			if (s_Renderer == nullptr || frame == nullptr) return;
			s_Renderer->Replay(std::static_pointer_cast<GameUIFrameData>(frame));
		}
	}
}
