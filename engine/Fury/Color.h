#ifndef _FURY_COLOR_H_
#define _FURY_COLOR_H_

#include "Macros.h"

namespace fury
{
	class FURY_API Color
	{
	public:

		static Color Black;

		static Color Blue;

		static Color Cyan;

		static Color Gray;

		static Color Green;

		static Color Magenta;

		static Color Red;

		static Color White;

		static Color Yellow;

		float r, g, b, a;

		Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

		Color Lerp(Color color, float t) const;

		Color Invert() const;

		Color &operator = (Color other);

		bool operator == (Color other) const;

		bool operator != (Color other) const;

	};
}

#endif // _FURY_COLOR_H_