#ifndef _FURY_FRUSTUM_H_
#define _FURY_FRUSTUM_H_

#include <vector>

#include "Collidable.h"
#include "Plane.h"

namespace fury
{
	class BoxBounds;

	class SphereBounds;

	class Matrix4;

	class FURY_API Frustum : public Collidable
	{
	private:

		std::vector<Vector4> m_Corners;

		std::vector<Vector4> m_WorldSpaceCorners;

		std::vector<Plane> m_Planes;

	public:

		Frustum() : m_Corners(8), m_WorldSpaceCorners(8), m_Planes(6) {}
		
		Frustum(const Frustum &other);

		void Setup(float fov, float ratio, float near, float far);

		void Setup(float left, float right, float bottom, float top, float near, float far);

		void Transform(const Matrix4 &matrix);

		virtual Side IsInside(const SphereBounds &bsphere) const;

		virtual Side IsInside(const BoxBounds &aabb) const;

		virtual Side IsInside(Vector4 point) const;

		virtual bool IsInsideFast(const SphereBounds &bsphere) const;

		virtual bool IsInsideFast(const BoxBounds &aabb) const;

		virtual bool IsInsideFast(Vector4 point) const;

		// ntl, ntr, nbl, nbr, ftl, ftr, fbl, fbr
		std::vector<Vector4> GetWorldSpaceCorners() const;
	};
}

#endif // _FURY_FRUSTUM_H_