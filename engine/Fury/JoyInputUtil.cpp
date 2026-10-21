#include "Fury/JoyInputUtil.h"

#include <SFML/Window/Joystick.hpp>

namespace fury
{
	JoyInputUtil::JoyInputUtil()
	{
		for (unsigned int i = 0; i < sf::Joystick::Count; i++)
		{
			m_Connected[i] = false;
			for (unsigned int b = 0; b < sf::Joystick::ButtonCount; b++)
				m_Buttons[i][b] = m_ButtonsPrev[i][b] = false;
			for (unsigned int a = 0; a < sf::Joystick::AxisCount; a++)
				m_Axes[i][a] = 0.0f;
		}
	}

	bool JoyInputUtil::IsConnected(unsigned int id) const
	{
		if (id >= sf::Joystick::Count) return false;
		return m_Connected[id];
	}

	unsigned int JoyInputUtil::GetButtonCount(unsigned int id) const
	{
		if (id >= sf::Joystick::Count || !m_Connected[id]) return 0;
		return sf::Joystick::getButtonCount(id);
	}

	bool JoyInputUtil::IsButtonDown(unsigned int id, unsigned int button) const
	{
		if (id >= sf::Joystick::Count) return false;
		if (button >= sf::Joystick::ButtonCount) return false;
		return m_Buttons[id][button];
	}

	// Rising edge: down this frame, up the previous frame.
	bool JoyInputUtil::WasButtonPressed(unsigned int id, unsigned int button) const
	{
		if (id >= sf::Joystick::Count) return false;
		if (button >= sf::Joystick::ButtonCount) return false;
		return m_Buttons[id][button] && !m_ButtonsPrev[id][button];
	}

	// SFML axis position in [-100, 100]; 0 if unsupported / disconnected.
	float JoyInputUtil::GetAxis(unsigned int id, unsigned int axis) const
	{
		if (id >= sf::Joystick::Count) return 0.0f;
		if (axis >= sf::Joystick::AxisCount) return 0.0f;
		return m_Axes[id][axis];
	}

	// SFML auto-updates joystick state when an sf::Window is bound and
	// events are being polled, but call update() defensively so a script
	// that polls outside the event loop still sees fresh values.
	void JoyInputUtil::PollState()
	{
		sf::Joystick::update();
		for (unsigned int i = 0; i < sf::Joystick::Count; i++) {
			const bool connected = sf::Joystick::isConnected(i);
			if (connected != m_Connected[i]) {
				m_Connected[i] = connected;
				if (connected)
					OnConnected->Emit(std::move(i));
				else
					OnDisconnected->Emit(std::move(i));
			}
			if (!connected) {
				for (unsigned int b = 0; b < sf::Joystick::ButtonCount; b++)
					m_Buttons[i][b] = m_ButtonsPrev[i][b] = false;
				for (unsigned int a = 0; a < sf::Joystick::AxisCount; a++)
					m_Axes[i][a] = 0.0f;
				continue;
			}
			for (unsigned int b = 0; b < sf::Joystick::ButtonCount; b++) {
				m_ButtonsPrev[i][b] = m_Buttons[i][b];
				m_Buttons[i][b] = sf::Joystick::isButtonPressed(i, b);
			}
			for (unsigned int a = 0; a < sf::Joystick::AxisCount; a++) {
				const auto axis = static_cast<sf::Joystick::Axis>(a);
				m_Axes[i][a] = sf::Joystick::hasAxis(i, axis)
					? sf::Joystick::getAxisPosition(i, axis)
					: 0.0f;
			}
		}
	}
}
