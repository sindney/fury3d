#include <cmath>

#include "Fury/BoxBounds.h"
#include "Fury/Frustum.h"
#include "Fury/Matrix4.h"
#include "Fury/Plane.h"
#include "Fury/SphereBounds.h"
#include "Fury/Vector4.h"

namespace fury
{
	Frustum::Frustum(const Frustum &other)
	{
		m_BaseCorners = other.m_BaseCorners;
		m_CurrentCorners = other.m_CurrentCorners;
		m_Planes = other.m_Planes;
		m_ProjectionParams = other.m_ProjectionParams;
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

		m_ProjectionParams[0] = left;
		m_ProjectionParams[1] = right;
		m_ProjectionParams[2] = bottom;
		m_ProjectionParams[3] = top;
		m_ProjectionParams[4] = near;
		m_ProjectionParams[5] = far;

		float toFar = far / near;
		float farTop = top * toFar;
		float farRight = right * toFar;
		float farBottom = bottom * toFar;
		float farLeft = left * toFar;

		m_BaseCorners[0] = Vector4(left, top, near, 1.0f);
		m_BaseCorners[1] = Vector4(right, top, near, 1.0f);
		m_BaseCorners[2] = Vector4(left, bottom, near, 1.0f);
		m_BaseCorners[3] = Vector4(right, bottom, near, 1.0f);
		m_BaseCorners[4] = Vector4(farLeft, farTop, far, 1.0f);
		m_BaseCorners[5] = Vector4(farRight, farTop, far, 1.0f);
		m_BaseCorners[6] = Vector4(farLeft, farBottom, far, 1.0f);
		m_BaseCorners[7] = Vector4(farRight, farBottom, far, 1.0f);

		Transform(Matrix4());
	}

	void Frustum::Transform(const Matrix4 &matrix)
	{
		m_Transform = matrix;

		// transform corners to world space.
		for (int i = 0; i < 8; i++)
			m_CurrentCorners[i] = matrix.Multiply(m_BaseCorners[i]);

		// top : ftr, ntr, ntl
		m_Planes[0].Set3Points(m_CurrentCorners[5], m_CurrentCorners[1], m_CurrentCorners[0]);
		// bottom : nbl, nbr, fbr
		m_Planes[1].Set3Points(m_CurrentCorners[2], m_CurrentCorners[3], m_CurrentCorners[7]);
		// left : ntl, nbl, fbl
		m_Planes[2].Set3Points(m_CurrentCorners[0], m_CurrentCorners[2], m_CurrentCorners[6]);
		// right : fbr, nbr, ntr
		m_Planes[3].Set3Points(m_CurrentCorners[7], m_CurrentCorners[3], m_CurrentCorners[1]);
		// near : ntl, ntr, nbr
		m_Planes[4].Set3Points(m_CurrentCorners[0], m_CurrentCorners[1], m_CurrentCorners[3]);
		// far : ftr, ftl, fbl
		m_Planes[5].Set3Points(m_CurrentCorners[5], m_CurrentCorners[4], m_CurrentCorners[6]);
	}

	Side Frustum::IsInside(const SphereBounds &bsphere) const
	{
		if (bsphere.GetInfinite())
			return Side::IN;

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
		if (aabb.GetInfinite())
			return Side::IN;

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
		if (bsphere.GetInfinite())
			return true;

		for (int i = 0; i < 6; i++)
			if (!m_Planes[i].IsInsideFast(bsphere))
				return false;

		return true;
	}

	bool Frustum::IsInsideFast(const BoxBounds &aabb) const
	{
		if (aabb.GetInfinite())
			return true;

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

	std::array<Vector4, 8> Frustum::GetCurrentCorners() const
	{
		return m_CurrentCorners;
	}

	std::array<Vector4, 8> Frustum::GetBaseCorners() const
	{
		return m_BaseCorners;
	}

	Matrix4 Frustum::GetTransformMatrix() const
	{
		return m_Transform;
	}

	BoxBounds Frustum::GetBoxBounds() const
	{
		BoxBounds aabb;
		for (const auto &ptn : m_CurrentCorners)
			aabb.Encapsulate(ptn);
		return aabb;
	}
}