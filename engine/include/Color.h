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

		float r, g, b;

		Color(float r, float g, float b) : r(r), g(g), b(b) {}

		Color Lerp(Color color, float t) const;

		Color Invert() const;

	};
}

#endif // _FURY_COLOR_H_