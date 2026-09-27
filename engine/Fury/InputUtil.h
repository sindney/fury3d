#ifndef _FURY_INPUT_UTIL_H_
#define _FURY_INPUT_UTIL_H_

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/Window/Window.hpp>

#include "Fury/Signal.h"
#include "Fury/Singleton.h"
#include "Fury/EnumUtil.h"

namespace fury
{
	class FURY_API InputUtil final : public Singleton<InputUtil, int, int>
	{
		friend class Engine;

	public:

		typedef std::shared_ptr<InputUtil> Ptr;

	private:

		std::pair<int, int> m_WindowSize;

		std::pair<int, int> m_MousePosition;

		bool m_WindowFocused = false;

		bool m_MouseInWindow = true;

		float m_MouseWheel = 0.0f;

		bool m_MouseDown[sf::Mouse::ButtonCount];

		bool m_KeyDown[sf::Keyboard::KeyCount];

		// Bound by Engine::Run; needed for cursor grab/visibility and the
		// grabbed-mode recentering in Engine::HandleEvent.
		sf::Window *m_Window = nullptr;

		bool m_CursorGrabbed = false;

		bool m_CursorVisible = true;

		// Accumulated relative mouse motion while the cursor is grabbed
		// (fps look). Consumed (read + cleared) by the character controller.
		std::pair<int, int> m_MouseDeltaAccum {0, 0};

	public:

		Signal<sf::Keyboard::Key>::Ptr OnKeyDown = Signal<sf::Keyboard::Key>::Create();

		Signal<sf::Keyboard::Key>::Ptr OnKeyUp = Signal<sf::Keyboard::Key>::Create();

		Signal<>::Ptr OnWindowClosed = Signal<>::Create();

		Signal<int, int>::Ptr OnWindowResized = Signal<int, int>::Create();

		// true for focused, false for losing.
		Signal<bool>::Ptr OnWindowFocus = Signal<bool>::Create();

		// unicode
		Signal<size_t>::Ptr OnTextEntered = Signal<size_t>::Create();

		// true for entering window, false for lefting.
		Signal<bool>::Ptr OnMouseEnter = Signal<bool>::Create();

		// offset (ppositive up/left), x & y relative to window's top & left owner.
		Signal<float, int, int>::Ptr OnMouseWheel = Signal<float, int, int>::Create();

		// x & y relative to window's top & left owner.
		Signal<int, int>::Ptr OnMouseMove = Signal<int, int>::Create();

		// x & y relative to window's top & left owner.
		Signal<sf::Mouse::Button, int, int>::Ptr OnMouseDown = Signal<sf::Mouse::Button, int, int>::Create();

		Signal<sf::Mouse::Button, int, int>::Ptr OnMouseUp = Signal<sf::Mouse::Button, int, int>::Create();

		InputUtil(int winWidth, int winHeight);

		void GetWindowSize(int &width, int &height);

		std::pair<int, int> GetMousePosition();

		// Window-bound cursor control (game UI / fps look).
		void BindWindow(sf::Window *window);

		void SetCursorGrabbed(bool grabbed);

		bool GetCursorGrabbed() const;

		void SetCursorVisible(bool visible);

		// Re-applies (grabbed, visible) to the OS; called on focus
		// transitions so an unfocused window never holds the cursor.
		void SyncCursorOS();

		// Accumulated relative mouse motion since the last call (clears on
		// read). Only meaningful while the cursor is grabbed.
		std::pair<int, int> ConsumeMouseDelta();

		bool GetWindowFocused();

		float GetMouseWheel();

		bool GetMouseDown();

		bool GetMouseDown(sf::Mouse::Button btn);

		bool GetKeyDown(sf::Keyboard::Key key);

		// Zero held keys, buttons, wheel, and position.
		void ResetTransientInputState();
	};
}

#endif // _FURY_INPUT_UTIL_H_
