#ifndef _FURY_COLLIDABLE_H_
#define _FURY_COLLIDABLE_H_

#include "Fury/Plane.h"

namespace fury
{
	class BoxBounds;

	class SphereBounds;

	class Vector4;

	class FURY_API Collidable
	{
	public:

		virtual Side IsInside(Vector4 point) const = 0;

		virtual Side IsInside(const BoxBounds &aabb) const = 0;

		virtual Side IsInside(const SphereBounds &bsphere) const = 0;

		virtual bool IsInsideFast(const SphereBounds &bsphere) const = 0;

		virtual bool IsInsideFast(const BoxBounds &aabb) const = 0;

		virtual bool IsInsideFast(Vector4 point) const = 0;
	};
}

#endif // _FURY_COLLIDABLE_H_