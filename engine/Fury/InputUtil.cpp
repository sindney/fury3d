#include "Fury/InputUtil.h"

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Mouse.hpp>

#if PLATFORM_MACOS
namespace fury
{
	// Defined in Engine_dpi_mac.mm: CG-level cursor confinement + HID
	// delta polling (SFML's macOS grab/warp paths are unusable there).
	void furyMacOSSyncCursor(bool grabbed, bool hidden);
	void furyMacOSPollMouseDelta(float &dx, float &dy);
}
#endif

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

	void InputUtil::BindWindow(sf::Window *window)
	{
		m_Window = window;
	}

	void InputUtil::SetCursorGrabbed(bool grabbed)
	{
		m_CursorGrabbed = grabbed;
#if PLATFORM_MACOS
		SyncCursorOS();
#else
		if (m_Window != nullptr)
			m_Window->setMouseCursorGrabbed(grabbed);
#endif
	}

	// Applies (grabbed, visible) to the OS. m_WindowFocused feeds in so
	// focus loss releases the cursor without changing the logical state.
	void InputUtil::SyncCursorOS()
	{
#if PLATFORM_MACOS
		furyMacOSSyncCursor(m_CursorGrabbed && m_WindowFocused, m_CursorGrabbed && m_WindowFocused && !m_CursorVisible);
#endif
	}

	bool InputUtil::GetCursorGrabbed() const
	{
		return m_CursorGrabbed;
	}

	void InputUtil::SetCursorVisible(bool visible)
	{
		m_CursorVisible = visible;
#if PLATFORM_MACOS
		SyncCursorOS();
#else
		if (m_Window != nullptr)
			m_Window->setMouseCursorVisible(visible);
#endif
	}

	std::pair<int, int> InputUtil::ConsumeMouseDelta()
	{
		const auto out = m_MouseDeltaAccum;
		m_MouseDeltaAccum = {0, 0};
		return out;
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

	void InputUtil::ResetTransientInputState()
	{
		for (unsigned int i = 0; i < sf::Keyboard::KeyCount; ++i)
			m_KeyDown[i] = false;
		for (unsigned int i = 0; i < sf::Mouse::ButtonCount; ++i)
			m_MouseDown[i] = false;
		m_MouseWheel = 0.0f;
		m_MousePosition = std::make_pair(0, 0);
	}
}