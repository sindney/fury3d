#include <cmath>

#include "BoxBounds.h"
#include "Frustum.h"
#include "Matrix4.h"
#include "Plane.h"
#include "SphereBounds.h"
#include "Vector4.h"

namespace fury
{
	Frustum::Frustum(const Frustum &other) : Frustum() 
	{
		m_Corners = other.m_Corners;
		m_WorldSpaceCorners = other.m_WorldSpaceCorners;
		m_Planes = other.m_Planes;
	}

	void Frustum::Setup(float fov, float ratio, float near, float far)
	{
		float top = near * tan(fov / 2.0f);
		float right = top * ratio;
		Setup(-right, right, -top, top, near, far);
	}

	void Frustum::Setup(float left, float right, float bottom, float top, float near, float far)
	{
		// in opengl, negative z axis point to screen.
		// so we need to reverse near far values.
		near = -near;
		far = -far;

		float toFar = far / near;
		float farTop = top * toFar;
		float farRight = right * toFar;
		float farBottom = -farTop;
		float farLeft = -farRight;

		// ntl, ntr, nbl, nbr
		m_Corners[0] = Vector4(left, top, near, 1.0f);
		m_Corners[1] = Vector4(right, top, near, 1.0f);
		m_Corners[2] = Vector4(left, bottom, near, 1.0f);
		m_Corners[3] = Vector4(right, bottom, near, 1.0f);
		// ftl, ftr, fbl, fbr
		m_Corners[4] = Vector4(farLeft, farTop, far, 1.0f);
		m_Corners[5] = Vector4(farRight, farTop, far, 1.0f);
		m_Corners[6] = Vector4(farLeft, farBottom, far, 1.0f);
		m_Corners[7] = Vector4(farRight, farBottom, far, 1.0f);

		Transform(Matrix4());
	}

	void Frustum::Transform(const Matrix4 &matrix)
	{
		// transform corners to world space.
		for (int i = 0; i < 8; i++)
			m_WorldSpaceCorners[i] = matrix.Multiply(m_Corners[i]);

		// top : ftr, ntr, ntl
		m_Planes[0].Set3Points(m_WorldSpaceCorners[5], m_WorldSpaceCorners[1], m_WorldSpaceCorners[0]);
		// bottom : nbl, nbr, fbr
		m_Planes[1].Set3Points(m_WorldSpaceCorners[2], m_WorldSpaceCorners[3], m_WorldSpaceCorners[7]);
		// left : ntl, nbl, fbl
		m_Planes[2].Set3Points(m_WorldSpaceCorners[0], m_WorldSpaceCorners[2], m_WorldSpaceCorners[6]);
		// right : fbr, nbr, ntr
		m_Planes[3].Set3Points(m_WorldSpaceCorners[7], m_WorldSpaceCorners[3], m_WorldSpaceCorners[1]);
		// near : ntl, ntr, nbr
		m_Planes[4].Set3Points(m_WorldSpaceCorners[0], m_WorldSpaceCorners[1], m_WorldSpaceCorners[3]);
		// far : ftr, ftl, fbl
		m_Planes[5].Set3Points(m_WorldSpaceCorners[5], m_WorldSpaceCorners[4], m_WorldSpaceCorners[6]);
	}

	Side Frustum::IsInside(const SphereBounds &bsphere) const
	{
		bool straddle = false;

		for (int i = 0; i < 6; i++)
		{
			Side side = m_Planes[i].IsInside(bsphere);
			if (side == Side::OUT)
				return Side::OUT;
			else if (side == Side::STRADDLE)
				straddle = true;
		}

		return straddle ? Side::STRADDLE : Side::IN;
	}

	Side Frustum::IsInside(const BoxBounds &aabb) const
	{
		bool straddle = false;

		for (int i = 0; i < 6; i++)
		{
			Side side = m_Planes[i].IsInside(aabb);
			if (side == Side::OUT)
				return Side::OUT;
			else if (side == Side::STRADDLE)
				straddle = true;
		}

		return straddle ? Side::STRADDLE : Side::IN;
	}

	Side Frustum::IsInside(Vector4 point) const
	{
		bool straddle = false;

		for (int i = 0; i < 6; i++)
		{
			Side side = m_Planes[i].IsInside(point);
			if (side == Side::OUT)
				return Side::OUT;
			else if (side == Side::STRADDLE)
				straddle = true;
		}

		return straddle ? Side::STRADDLE : Side::IN;
	}

	bool Frustum::IsInsideFast(const SphereBounds &bsphere) const
	{
		for (int i = 0; i < 6; i++)
			if (!m_Planes[i].IsInsideFast(bsphere))
				return false;

		return true;
	}

	bool Frustum::IsInsideFast(const BoxBounds &aabb) const
	{
		for (int i = 0; i < 6; i++)
			if (!m_Planes[i].IsInsideFast(aabb))
				return false;

		return true;
	}

	bool Frustum::IsInsideFast(Vector4 point) const
	{
		for (int i = 0; i < 6; i++)
			if (!m_Planes[i].IsInsideFast(point))
				return false;

		return true;
	}

	std::vector<Vector4> Frustum::GetWorldSpaceCorners() const
	{
		return m_WorldSpaceCorners;
	}
}