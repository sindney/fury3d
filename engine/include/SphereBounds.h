#ifndef _FURY_SPHEREBOUNDS_H_
#define _FURY_SPHEREBOUNDS_H_

#include "Collidable.h"
#include "Vector4.h"

namespace fury
{
	class BoxBounds;

	class FURY_API SphereBounds : public Collidable
	{
	protected:

		Vector4 m_Center;

		float m_Radius;

		bool m_Infinite = false;

		bool m_Dirty = false;

	public:

		SphereBounds();

		SphereBounds(Vector4 center, float radius);

		void SetCenterRadius(Vector4 center, float radius);

		void Zero();

		void SetInfinite(bool value);

		bool GetInfinite() const;

		virtual Side IsInside(Vector4 point) const;

		virtual Side IsInside(const BoxBounds &aabb) const;

		virtual Side IsInside(const SphereBounds &bsphere) const;

		virtual bool IsInsideFast(Vector4 point) const;

		virtual bool IsInsideFast(const BoxBounds &aabb) const;

		virtual bool IsInsideFast(const SphereBounds &bsphere) const;

		// grow to include another aabb.
		void Encapsulate(const BoxBounds &aabb);

		// grow to include another bsphere.
		void Encapsulate(const SphereBounds &bsphere);

		// grow to include a point.
		void Encapsulate(Vector4 point);

		// get the shortest distance between point to aabb.
		// if the point is inside the aabb, 0 will be returned.
		float GetDistance(Vector4 point) const;

		Vector4 GetCenter() const;

		float GetRadius() const;

		bool GetDirty() const;

		// this will change Encapsulate's first-time behaviour.
		void SetDirty(bool dirty);

		SphereBounds &operator = (const SphereBounds &data);

		bool operator == (const SphereBounds &data) const;

		bool operator != (const SphereBounds &data) const;

	};
}

#endif // _FURY_SPHEREBOUNDS_H_