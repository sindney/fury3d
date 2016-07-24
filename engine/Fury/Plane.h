#ifndef _FURY_PLANE_H_
#define _FURY_PLANE_H_

#include "Fury/EnumUtil.h"
#include "Fury/Vector4.h"

namespace fury
{
	class BoxBounds;

	class SphereBounds;

	class FURY_API Plane
	{
	protected:

		Vector4 m_Normal;

		float m_Distance;

	public:

		Plane();

		Plane(float a, float b, float c, float d);

		Plane(const Plane &other);

		Plane(Vector4 p1, Vector4 p2, Vector4 p3);

		Plane(Vector4 normal, Vector4 position);

		// CCW
		void Set3Points(Vector4 p1, Vector4 p2, Vector4 p3);

		void SetNormalAndPosition(Vector4 normal, Vector4 position);

		Side IsInside(Vector4 point) const;

		Side IsInside(const BoxBounds &aabb) const;

		Side IsInside(const SphereBounds &bsphere) const;

		bool IsInsideFast(Vector4 point) const;

		bool IsInsideFast(const BoxBounds &aabb) const;

		bool IsInsideFast(const SphereBounds &bsphere) const;

		float GetDistance(Vector4 point) const;

		Vector4 GetNormal() const;
		
		float GetDistance() const;

		Plane &operator = (Plane other);

		bool operator == (Plane other) const;

		bool operator != (Plane other) const;

	};
}

#endif // _FURY_PLANE_H_