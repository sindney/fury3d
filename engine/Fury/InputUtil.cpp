#include "Fury/InputUtil.h"

namespace fury
{
	InputUtil::InputUtil(int winWidth, int winHeight)
		: m_WindowSize(winWidth, winHeight), m_MousePosition(0, 0)
	{
		for (int i = 0; i < sf::Mouse::Button::ButtonCount; i++)
			m_MouseDown[i] = false;

		for (int i = 0; i < sf::Keyboard::Key::KeyCount; i++)
			m_KeyDown[i] = false;
	}

	void InputUtil::GetWindowSize(int &width, int &height)
	{
		width = m_WindowSize.first;
		height = m_WindowSize.second;
	}

	std::pair<int, int> InputUtil::GetMousePosition()
	{
		return m_MousePosition;
	}

	bool InputUtil::GetWindowFocused()
	{
		return m_WindowFocused;
	}

	float InputUtil::GetMouseWheel()
	{
		return m_MouseWheel;
	}

	bool InputUtil::GetMouseDown()
	{
		for (int i = 0; i < sf::Mouse::Button::ButtonCount; i++)
		{
			if (m_MouseDown[i])
				return true;
		}
		return false;
	}

	bool InputUtil::GetMouseDown(sf::Mouse::Button btn)
	{
		return m_MouseDown[btn];
	}

	bool InputUtil::GetKeyDown(sf::Keyboard::Key key)
	{
		return m_KeyDown[key];
	}
}