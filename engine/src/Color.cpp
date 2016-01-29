#include "Color.h"

namespace fury
{
	Color Color::Black = Color(0, 0, 0);

	Color Color::Blue = Color(0, 0, 1);

	Color Color::Cyan = Color(0, 1, 1);

	Color Color::Gray = Color(0.5f, 0.5f, 0.5f);

	Color Color::Green = Color(0, 1, 0);

	Color Color::Magenta = Color(1, 0, 1);

	Color Color::Red = Color(1, 0, 0);

	Color Color::White = Color(1, 1, 1);

	Color Color::Yellow = Color(1, 1, 0);

	Color Color::Lerp(Color color, float t) const
	{
		float it = 1.0f - t;
		return Color(
			r * it + color.r * t,
			g * it + color.g * t,
			b * it + color.b * t
		);
	}

	Color Color::Invert() const
	{
		return Color(1.0f - r, 1.0f - g, 1.0f - b);
	}
}