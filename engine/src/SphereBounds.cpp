#include <limits>

#include "BoxBounds.h"
#include "SphereBounds.h"

namespace fury
{
	SphereBounds::SphereBounds() 
	{
		Zero();
	}

	SphereBounds::SphereBounds(Vector4 center, float radius) : m_Dirty(false)
	{
		SetCenterRadius(center, radius);
	}

	void SphereBounds::SetCenterRadius(Vector4 center, float radius)
	{
		m_Center = center;
		m_Radius = radius;
	}

	void SphereBounds::Zero()
	{
		m_Center.Zero();
		m_Radius = 0.0f;
	}

	void SphereBounds::SetInfinite(bool value)
	{
		m_Infinite = value;
		if (m_Infinite)
			m_Radius = std::numeric_limits<float>::max();
	}

	bool SphereBounds::GetInfinite() const
	{
		return m_Infinite;
	}

	Side SphereBounds::IsInside(Vector4 point) const
	{
		if (m_Infinite) return Side::IN;

		float distance = (m_Center - point).Length();
		if (distance < m_Radius)
			return Side::IN;
		else if (distance > m_Radius)
			return Side::OUT;
		else
			return Side::STRADDLE;
	}
	
	Side SphereBounds::IsInside(const BoxBounds &aabb) const
	{
		if (m_Infinite || aabb.GetInfinite()) return Side::IN;

		float radius2 = m_Radius * m_Radius;
		if ((m_Center - aabb.FarestPoint(m_Center)).SquareLength() < radius2)
			return Side::IN;
		else if ((m_Center - aabb.ClosestPoint(m_Center)).SquareLength() <= radius2)
			return Side::STRADDLE;
		else
			return Side::OUT;
	}

	Side SphereBounds::IsInside(const SphereBounds &bsphere) const
	{
		if (m_Infinite || bsphere.GetInfinite()) return Side::IN;

		float distance = (m_Center - bsphere.GetCenter()).Length();
		if (distance >= m_Radius + bsphere.GetRadius())
			return Side::OUT;
		else if (distance + bsphere.GetRadius() < m_Radius)
			return Side::IN;
		else
			return Side::STRADDLE;
	}

	bool SphereBounds::IsInsideFast(Vector4 point) const
	{
		if (m_Infinite) return true;
		return (m_Center - point).SquareLength() <= m_Radius * m_Radius;
	}

	bool SphereBounds::IsInsideFast(const BoxBounds &aabb) const
	{
		if (m_Infinite || aabb.GetInfinite()) return true;
		return (m_Center - aabb.ClosestPoint(m_Center)).SquareLength() <= m_Radius * m_Radius;
	}

	bool SphereBounds::IsInsideFast(const SphereBounds &bsphere) const
	{
		if (m_Infinite || bsphere.GetInfinite()) return true;
		const float radius = m_Radius + bsphere.m_Radius;
		return (m_Center - bsphere.GetCenter()).SquareLength() <= radius * radius;
	}

	void SphereBounds::Encapsulate(const BoxBounds &aabb)
	{
		if (m_Infinite || aabb.GetInfinite())
		{
			SetInfinite(true);
			return;
		}

		float radius = aabb.GetExtents().Length();

		if (m_Dirty)
		{
			m_Dirty = false;
			SetCenterRadius(aabb.GetCenter(), radius);
		}
		else
		{
			m_Radius = (m_Center - aabb.GetCenter()).Length() + radius;
		}
	}

	void SphereBounds::Encapsulate(const SphereBounds &bsphere)
	{
		if (m_Infinite || bsphere.GetInfinite())
		{
			SetInfinite(true);
			return;
		}

		if (m_Dirty)
		{
			m_Dirty = false;
			SetCenterRadius(bsphere.GetCenter(), bsphere.GetRadius());
		}
		else
		{
			m_Radius = (m_Center - bsphere.GetCenter()).Length() + bsphere.GetRadius();
		}
	}

	void SphereBounds::Encapsulate(Vector4 point)
	{
		if (m_Infinite) return;

		if (m_Dirty)
		{
			m_Dirty = false;
			SetCenterRadius(point, 0.0f);
		}
		else
		{
			m_Radius = (m_Center - point).Length();
		}
	}

	float SphereBounds::GetDistance(Vector4 point) const
	{
		float distance = (m_Center - point).Length();
		return distance > m_Radius ? 0.0f : distance - m_Radius;
	}

	Vector4 SphereBounds::GetCenter() const
	{
		return m_Center;
	}

	float SphereBounds::GetRadius() const
	{
		return m_Radius;
	}

	bool SphereBounds::GetDirty() const
	{
		return m_Dirty;
	}

	void SphereBounds::SetDirty(bool dirty)
	{
		m_Dirty = dirty;
	}

	SphereBounds &SphereBounds::operator = (const SphereBounds &data)
	{
		if (data.GetInfinite())
			SetInfinite(true);
		else 
			SetCenterRadius(data.GetCenter(), data.GetRadius());

		return *this;
	}

	bool SphereBounds::operator == (const SphereBounds &data) const
	{
		return m_Radius == data.GetRadius() && m_Center == data.GetCenter();
	}

	bool SphereBounds::operator != (const SphereBounds &data) const
	{
		return m_Radius != data.GetRadius() || m_Center != data.GetCenter();
	}
}