#include <math.h>

#include "Fury/BoxBounds.h"
#include "Fury/Plane.h"
#include "Fury/SphereBounds.h"

namespace fury
{
	Plane::Plane() : m_Distance(0), m_Normal() {}

	Plane::Plane(float a, float b, float c, float d) :
		m_Normal(a, b, c, 0.0f), m_Distance(d) {}

	Plane::Plane(const Plane &other)
		: m_Normal(other.GetNormal()), m_Distance(other.GetDistance()) {}

	Plane::Plane(Vector4 p1, Vector4 p2, Vector4 p3)
	{
		Set3Points(p1, p2, p3);
	}

	Plane::Plane(Vector4 normal, Vector4 position)
	{
		SetNormalAndPosition(normal, position);
	}

	void Plane::Set3Points(Vector4 p1, Vector4 p2, Vector4 p3)
	{
		m_Normal = (p2 - p1).CrossProduct((p3 - p2));
		m_Normal.Normalize();
		m_Normal.w = 0.0f;
		m_Distance = -(p1 * m_Normal);
	}

	void Plane::SetNormalAndPosition(Vector4 normal, Vector4 position)
	{
		m_Normal = normal.Normalized();
		m_Normal.w = 0.0f;
		m_Distance = -(normal * position);
	}

	Side Plane::IsInside(Vector4 point) const
	{
		float d = GetDistance(point);
		if (d > 0.0f) return Side::IN;
		if (d < 0.0f) return Side::OUT;
		return Side::STRADDLE;
	}

	Side Plane::IsInside(const BoxBounds &aabb) const
	{
		if (GetDistance(aabb.GetVertexP(m_Normal)) < 0.0f)
			return Side::OUT;
		if (GetDistance(aabb.GetVertexN(m_Normal)) < 0.0f)
			return Side::STRADDLE;
		return Side::IN;
	}

	Side Plane::IsInside(const SphereBounds &bsphere) const
	{
		float d = GetDistance(bsphere.GetCenter());
		if (fabs(d) <= bsphere.GetRadius())
			return Side::STRADDLE;

		return d > 0.0f ? Side::IN : Side::OUT;
	}

	bool Plane::IsInsideFast(Vector4 point) const
	{
		return GetDistance(point) >= 0.0f;
	}

	bool Plane::IsInsideFast(const BoxBounds &aabb) const
	{
		return GetDistance(aabb.GetVertexP(m_Normal)) >= 0.0f;
	}

	bool Plane::IsInsideFast(const SphereBounds &bsphere) const
	{
		return GetDistance(bsphere.GetCenter()) >= bsphere.GetRadius();
	}

	float Plane::GetDistance(Vector4 point) const
	{
		return m_Normal.x * point.x + m_Normal.y * point.y + m_Normal.z * point.z + m_Distance;
	}

	Vector4 Plane::GetNormal() const
	{
		return m_Normal;
	}

	float Plane::GetDistance() const
	{
		return m_Distance;
	}

	Plane &Plane::operator = (Plane other)
	{
		m_Normal = other.GetNormal();
		m_Distance = other.GetDistance();
		return *this;
	}

	bool Plane::operator == (Plane other) const
	{
		return m_Normal == other.GetNormal() && m_Distance == other.GetDistance();
	}

	bool Plane::operator != (Plane other) const
	{
		return m_Normal != other.GetNormal() || m_Distance != other.GetDistance();
	}
}