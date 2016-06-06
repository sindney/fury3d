#ifndef _FURY_FRUSTUM_H_
#define _FURY_FRUSTUM_H_

#include <array>

#include "Collidable.h"
#include "Plane.h"
#include "Matrix4.h"

namespace fury
{
	class BoxBounds;

	class SphereBounds;

	class FURY_API Frustum : public Collidable
	{
	private:

		std::array<Vector4, 8> m_BaseCorners;

		std::array<Vector4, 8> m_CurrentCorners;

		std::array<Plane, 6> m_Planes;

		// left, right, bottom, top, near, far
		std::array<float, 6> m_ProjectionParams;

		Matrix4 m_Transform;

	public:

		Frustum() {}

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
		std::array<Vector4, 8> GetCurrentCorners() const;

		std::array<Vector4, 8> GetBaseCorners() const;

		Matrix4 GetTransformMatrix() const;

		BoxBounds GetBoxBounds() const;
	};
}

#endif // _FURY_FRUSTUM_H_