#ifndef _FURY_JOY_INPUT_UTIL_H_
#define _FURY_JOY_INPUT_UTIL_H_

#include <SFML/Window/Joystick.hpp>

#include "Fury/Signal.h"
#include "Fury/Singleton.h"

namespace fury
{
	// Polls sf::Joystick once per frame so on_update scripts see fresh
	// button / axis state even when no input event arrives.
	class FURY_API JoyInputUtil final : public Singleton<JoyInputUtil>
	{
		friend class Engine;

	public:

		typedef std::shared_ptr<JoyInputUtil> Ptr;

		// Mirror sf::Joystick::Count / ButtonCount / AxisCount for callers
		// who don't want to reach into SFML.
		static constexpr unsigned int MaxJoysticks = sf::Joystick::Count;
		static constexpr unsigned int MaxButtons = sf::Joystick::ButtonCount;
		static constexpr unsigned int MaxAxes = sf::Joystick::AxisCount;

		// XInput-compatible button names. Most controllers shipped as
		// Xbox-compatible (the vast majority) map these to the matching
		// SFML button index; non-XInput pads may need a remap table.
		enum class FURY_API Button : unsigned int {
			A = 0,
			B = 1,
			X = 2,
			Y = 3,
			LB = 4,
			RB = 5,
			BACK = 6,
			START = 7,
			L3 = 8,
			R3 = 9,
		};

		Signal<unsigned int>::Ptr OnConnected = Signal<unsigned int>::Create();
		Signal<unsigned int>::Ptr OnDisconnected = Signal<unsigned int>::Create();

		JoyInputUtil();

		bool IsConnected(unsigned int id) const;
		unsigned int GetButtonCount(unsigned int id) const;
		bool IsButtonDown(unsigned int id, unsigned int button) const;

		// Rising edge this frame (was up, now down). Edge state is cleared
		// by PollState.
		bool WasButtonPressed(unsigned int id, unsigned int button) const;

		// SFML axis position in [-100, 100]; 0 if unsupported / disconnected.
		float GetAxis(unsigned int id, unsigned int axis) const;

		// Refreshes connection / button / axis state from sf::Joystick and
		// rolls the previous-frame button buffer for edge queries.
		void PollState();

	private:

		bool m_Connected[sf::Joystick::Count];
		bool m_Buttons[sf::Joystick::Count][sf::Joystick::ButtonCount];
		bool m_ButtonsPrev[sf::Joystick::Count][sf::Joystick::ButtonCount];
		float m_Axes[sf::Joystick::Count][sf::Joystick::AxisCount];
	};
}

#endif // _FURY_JOY_INPUT_UTIL_H_
