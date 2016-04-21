#ifndef _FURY_INPUT_UTIL_H_
#define _FURY_INPUT_UTIL_H_

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include "Signal.h"
#include "Singleton.h"
#include "EnumUtil.h"

namespace fury
{
	class FURY_API InputManager final : public Singleton<InputManager, int, int>
	{
		friend class EngineManager;

	private:

		std::pair<unsigned int, unsigned int> m_WindowSize;

		std::pair<int, int> m_MousePosition;

		bool m_WindowFocused = false;

		bool m_MouseInWindow = true;

		float m_MouseWheel = 0.0f;

		bool m_MouseDown[sf::Mouse::Button::ButtonCount];

		bool m_KeyDown[sf::Keyboard::Key::KeyCount];

	public:
		
		Signal<sf::Keyboard::Key> OnKeyDown;

		Signal<sf::Keyboard::Key> OnKeyUp;

		Signal<> OnWindowClosed;

		Signal<unsigned int, unsigned int> OnWindowResized;

		// true for focused, false for losing.
		Signal<bool> OnWindowFocus;

		// unicode
		Signal<size_t> OnTextEntered;

		// true for entering window, false for lefting.
		Signal<bool> OnMouseEnter;

		// offset (ppositive up/left), x & y relative to window's top & left owner.
		Signal<float, int, int> OnMouseWheel;

		// x & y relative to window's top & left owner.
		Signal<int, int> OnMouseMove;

		// x & y relative to window's top & left owner.
		Signal<sf::Mouse::Button, int, int> OnMouseDown;

		Signal<sf::Mouse::Button, int, int> OnMouseUp;

		InputManager(unsigned int width, unsigned int height);

		std::pair<unsigned int, unsigned int> GetWindowSize();

		std::pair<int, int> GetMousePosition();

		bool GetWindowFocused();

		float GetMouseWheel();

		bool GetMouseDown();

		bool GetMouseDown(sf::Mouse::Button btn);

		bool GetKeyDown(sf::Keyboard::Key key);
	};
}

#endif // _FURY_INPUT_UTIL_H_