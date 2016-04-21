#include "InputManager.h"

namespace fury
{
	InputManager::InputManager(unsigned int width, unsigned int height)
		: m_WindowSize(width, height), m_MousePosition(0, 0)
	{
		for (int i = 0; i < sf::Mouse::Button::ButtonCount; i++)
			m_MouseDown[i] = false;

		for (int i = 0; i < sf::Keyboard::Key::KeyCount; i++)
			m_KeyDown[i] = false;
	}

	std::pair<unsigned int, unsigned int> InputManager::GetWindowSize()
	{
		return m_WindowSize;
	}

	std::pair<int, int> InputManager::GetMousePosition()
	{
		return m_MousePosition;
	}

	bool InputManager::GetWindowFocused()
	{
		return m_WindowFocused;
	}

	float InputManager::GetMouseWheel()
	{
		return m_MouseWheel;
	}

	bool InputManager::GetMouseDown()
	{
		for (int i = 0; i < sf::Mouse::Button::ButtonCount; i++)
		{
			if (m_MouseDown[i])
				return true;
		}
		return false;
	}

	bool InputManager::GetMouseDown(sf::Mouse::Button btn)
	{
		return m_MouseDown[btn];
	}

	bool InputManager::GetKeyDown(sf::Keyboard::Key key)
	{
		return m_KeyDown[key];
	}
}