#include <limits>

#include "BoxBounds.h"
#include "SphereBounds.h"

namespace fury
{
	BoxBounds::BoxBounds(bool dirty)
	{
		Zero(dirty);
	}

	BoxBounds::BoxBounds(Vector4 min, Vector4 max) : m_Dirty(false)
	{
		SetMinMax(min, max);
	}

	void BoxBounds::SetMinMax(Vector4 min, Vector4 max)
	{
		m_Min = min;
		m_Max = max;
		m_Size = max - min;
		m_Extents = m_Size * 0.5f;
		m_Center = min + m_Extents;
		m_Dirty = false;
	}

	void BoxBounds::Zero(bool dirty)
	{
		SetMinMax(Vector4(), Vector4());
		if (dirty)
			SetDirty(true);
	}

	void BoxBounds::SetInfinite(bool value)
	{
		m_Infinite = value;
		if (m_Infinite)
			SetMinMax(Vector4(std::numeric_limits<float>::min()),
			Vector4(std::numeric_limits<float>::max()));
	}

	bool BoxBounds::GetInfinite() const
	{
		return m_Infinite;
	}

	Side BoxBounds::IsInside(Vector4 point) const
	{
		if (m_Infinite) return Side::IN;
		
		if (point > m_Min && point < m_Max)
			return Side::IN;
		if (point == m_Min && point == m_Max)
			return Side::STRADDLE;
		
		return Side::OUT;
	}

	Side BoxBounds::IsInside(const SphereBounds &bsphere) const
	{
		if (m_Infinite || bsphere.GetInfinite()) return Side::IN;

		float radius = bsphere.GetRadius();
		Vector4 extents(radius, radius, radius, 1);
		Vector4 center = bsphere.GetCenter();
		Vector4 max = center + extents;
		Vector4 min = center - extents;

		if (max < m_Max && min > m_Min)
			return Side::IN;
		else if ((center - ClosestPoint(center)).Length() <= radius)
			return Side::STRADDLE;
		else
			return Side::OUT;
	}

	Side BoxBounds::IsInside(const BoxBounds &aabb) const
	{
		if (m_Infinite || aabb.GetInfinite()) return Side::IN;

		Vector4 max = aabb.GetMax();
		Vector4 min = aabb.GetMin();

		if (max < m_Max && min > m_Min)
			return Side::IN;
		else if (max >= m_Min && min <= m_Max)
			return Side::STRADDLE;
		else
			return Side::OUT;
	}

	bool BoxBounds::IsInsideFast(Vector4 point) const
	{
		if (m_Infinite) return true;
		return point >= m_Min && point <= m_Max;
	}

	bool BoxBounds::IsInsideFast(const BoxBounds &aabb) const
	{
		if (m_Infinite || aabb.GetInfinite()) return true;
		return aabb.GetMax() >= m_Min && aabb.GetMin() <= m_Max;
	}

	bool BoxBounds::IsInsideFast(const SphereBounds &bsphere) const
	{
		if (m_Infinite || bsphere.GetInfinite()) return true;

		Vector4 center = bsphere.GetCenter();
		float radius2 = bsphere.GetRadius() * bsphere.GetRadius();
		return (center - ClosestPoint(center)).SquareLength() <= radius2;
	}

	void BoxBounds::Encapsulate(const BoxBounds &aabb)
	{
		if (m_Infinite) return;

		if (aabb.GetInfinite())
		{
			SetInfinite(true);
			return;
		}

		Vector4 vMax = aabb.GetMax();
		Vector4 vMin = aabb.GetMin();

		if (m_Dirty)
		{
			SetMinMax(vMin, vMax);
		}
		else
		{
			vMax.x = vMax.x > m_Max.x ? vMax.x : m_Max.x;
			vMax.y = vMax.y > m_Max.y ? vMax.y : m_Max.y;
			vMax.z = vMax.z > m_Max.z ? vMax.z : m_Max.z;

			vMin.x = vMin.x < m_Min.x ? vMin.x : m_Min.x;
			vMin.y = vMin.y < m_Min.y ? vMin.y : m_Min.y;
			vMin.z = vMin.z < m_Min.z ? vMin.z : m_Min.z;

			SetMinMax(vMin, vMax);
		}
	}

	void BoxBounds::Encapsulate(const SphereBounds &bsphere)
	{
		if (m_Infinite) return;

		if (bsphere.GetInfinite())
		{
			SetInfinite(true);
			return;
		}

		float radius = bsphere.GetRadius();
		Vector4 extents(radius, radius, radius, 1);
		Vector4 center = bsphere.GetCenter();

		Encapsulate(BoxBounds(center - extents, center + extents));
	}

	void BoxBounds::Encapsulate(Vector4 point)
	{
		if (m_Infinite) return;

		if (m_Dirty)
		{
			SetMinMax(point, point);
		}
		else
		{
			Vector4 vMax = point;
			Vector4 vMin = point;

			vMax.x = vMax.x > m_Max.x ? vMax.x : m_Max.x;
			vMax.y = vMax.y > m_Max.y ? vMax.y : m_Max.y;
			vMax.z = vMax.z > m_Max.z ? vMax.z : m_Max.z;

			vMin.x = vMin.x < m_Min.x ? vMin.x : m_Min.x;
			vMin.y = vMin.y < m_Min.y ? vMin.y : m_Min.y;
			vMin.z = vMin.z < m_Min.z ? vMin.z : m_Min.z;

			SetMinMax(vMin, vMax);
		}
	}

	Vector4 BoxBounds::ClosestPoint(Vector4 point) const
	{
		return m_Infinite ? point : Vector4
		(
			point.x > m_Min.x ? (point.x > m_Max.x ? m_Max.x : point.x) : m_Min.x,
			point.y > m_Min.y ? (point.y > m_Max.y ? m_Max.y : point.y) : m_Min.y,
			point.z > m_Min.z ? (point.z > m_Max.z ? m_Max.z : point.z) : m_Min.z,
			1.0f
		);
	}

	Vector4 BoxBounds::FarestPoint(Vector4 point) const
	{
		return m_Infinite ? point : Vector4
		(
			point.x > m_Max.x ? m_Min.x : (point.x < m_Min.x ? m_Max.x : point.x),
			point.y > m_Max.y ? m_Min.y : (point.y < m_Min.y ? m_Max.y : point.y),
			point.z > m_Max.z ? m_Min.z : (point.z < m_Min.z ? m_Max.z : point.z),
			1.0f
		);
	}

	float BoxBounds::GetDistance(Vector4 point) const
	{
		return IsInside(point) == Side::IN ? 0.0f : ClosestPoint(point).Distance(point);
	}

	Vector4 BoxBounds::GetVertexP(Vector4 normal) const
	{
		return Vector4
		(
			normal.x < 0.0f ? m_Min.x : m_Max.x,
			normal.y < 0.0f ? m_Min.y : m_Max.y,
			normal.z < 0.0f ? m_Min.z : m_Max.z,
			1.0f
		);
	}

	Vector4 BoxBounds::GetVertexN(Vector4 normal) const
	{
		return Vector4
		(
			normal.x < 0.0f ? m_Max.x : m_Min.x,
			normal.y < 0.0f ? m_Max.y : m_Min.y,
			normal.z < 0.0f ? m_Max.z : m_Min.z, 
			1.0f
		);
	}

	Vector4 BoxBounds::GetCenter() const
	{
		return m_Center;
	}

	Vector4 BoxBounds::GetExtents() const
	{
		return m_Extents;
	}

	Vector4 BoxBounds::GetMax() const
	{
		return m_Max;
	}

	Vector4 BoxBounds::GetMin() const
	{
		return m_Min;
	}

	Vector4 BoxBounds::GetSize() const
	{
		return m_Size;
	}

	std::array<Vector4, 8> BoxBounds::GetCorners() const
	{
		std::array<Vector4, 8> corners = 
		{
			Vector4(m_Min, 1.0f),
			Vector4(m_Max.x, m_Min.y, m_Min.z, 1.0f),
			Vector4(m_Min.x, m_Max.y, m_Min.z, 1.0f),
			Vector4(m_Max.x, m_Max.y, m_Min.z, 1.0f),
			Vector4(m_Min.x, m_Min.y, m_Max.z, 1.0f),
			Vector4(m_Max.x, m_Min.y, m_Max.z, 1.0f),
			Vector4(m_Min.x, m_Max.y, m_Max.z, 1.0f),
			Vector4(m_Max, 1.0f)
		};
		return corners;
	}

	bool BoxBounds::Valid() const
	{
		return m_Max.x >= m_Min.x && m_Max.y >= m_Min.y && m_Max.z >= m_Min.z;
	}

	bool BoxBounds::GetDirty() const
	{
		return m_Dirty;
	}

	void BoxBounds::SetDirty(bool dirty)
	{
		m_Dirty = dirty;
	}

	BoxBounds &BoxBounds::operator = (const BoxBounds &data)
	{
		if (data.GetInfinite())
			SetInfinite(true);
		else 
			SetMinMax(data.GetMin(), data.GetMax());

		return *this;
	}

	bool BoxBounds::operator == (const BoxBounds &data) const
	{
		return m_Max == data.GetMax() && m_Min == data.GetMin();
	}

	bool BoxBounds::operator != (const BoxBounds &data) const
	{
		return m_Max != data.GetMax() || m_Min != data.GetMin();
	}
}