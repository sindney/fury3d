#ifndef _FURY_BOXBOUNDS_H_
#define _FURY_BOXBOUNDS_H_

#include "Collidable.h"
#include "Vector4.h"

namespace fury
{
	class SphereBounds;

	// axis aligned bounding box
	class FURY_API BoxBounds : public Collidable
	{
	protected:

		Vector4 m_Max, m_Min;

		Vector4 m_Extents, m_Size;

		Vector4 m_Center;

		bool m_Infinite = false;

		bool m_Dirty = false;

	public:

		BoxBounds();

		BoxBounds(Vector4 min, Vector4 max);

		void SetMinMax(Vector4 min, Vector4 max);

		void Zero();

		void SetInfinite(bool value);

		bool GetInfinite() const;

		virtual Side IsInside(Vector4 point) const;

		virtual Side IsInside(const SphereBounds &bsphere) const;

		virtual Side IsInside(const BoxBounds &aabb) const;

		virtual bool IsInsideFast(Vector4 point) const;

		virtual bool IsInsideFast(const BoxBounds &aabb) const;

		virtual bool IsInsideFast(const SphereBounds &bsphere) const;

		// grow to include another aabb.
		void Encapsulate(const BoxBounds &aabb);

		// grow to include another bsphere.
		void Encapsulate(const SphereBounds &bsphere);

		// grow to include a point.
		void Encapsulate(Vector4 point);

		// the closest point on the aabb.
		// if the point is inside the aabb, unmodified point will be returned.
		Vector4 ClosestPoint(Vector4 point) const;

		// the farest point on the aabb.
		// if the point is inside the aabb, unmodified point will be returned.
		Vector4 FarestPoint(Vector4 point) const;

		// get the shortest distance between point to aabb.
		// if the point is inside the aabb, 0 will be returned.
		float GetDistance(Vector4 point) const;

		// get the positive vertex to a plane.
		Vector4 GetVertexP(Vector4 normal) const;

		// get the negative vertex to a plane.
		Vector4 GetVertexN(Vector4 normal) const;

		Vector4 GetCenter() const;

		Vector4 GetExtents() const;

		Vector4 GetMax() const;

		Vector4 GetMin() const;

		Vector4 GetSize() const;

		bool Valid() const;

		bool GetDirty() const;

		// this will change Encapsulate's first-time behaviour.
		void SetDirty(bool dirty);

		BoxBounds &operator = (const BoxBounds &data);

		bool operator == (const BoxBounds &data) const;

		bool operator != (const BoxBounds &data) const;

	};
}

#endif // _FURY_BOXBOUNDS_H_