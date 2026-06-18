#include "Fury/InputUtil.h"

namespace fury
{
	InputUtil::InputUtil(int winWidth, int winHeight)
		: m_WindowSize(winWidth, winHeight), m_MousePosition(0, 0)
	{
		for (unsigned int i = 0; i < sf::Mouse::ButtonCount; i++)
			m_MouseDown[i] = false;

		for (unsigned int i = 0; i < sf::Keyboard::KeyCount; i++)
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
		for (unsigned int i = 0; i < sf::Mouse::ButtonCount; i++)
		{
			if (m_MouseDown[i])
				return true;
		}
		return false;
	}

	bool InputUtil::GetMouseDown(sf::Mouse::Button btn)
	{
		return m_MouseDown[static_cast<unsigned int>(btn)];
	}

	bool InputUtil::GetKeyDown(sf::Keyboard::Key key)
	{
		return m_KeyDown[static_cast<unsigned int>(key)];
	}
}