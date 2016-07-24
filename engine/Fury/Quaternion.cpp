#include <cmath>

#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"

namespace fury
{
	void Quaternion::Identity()
	{
		x = y = z = 0.0f;
		w = 1.0f;
	}

	float Quaternion::DotProduct(Quaternion other) const
	{
		return w * other.w + x * other.x + y * other.y + z * other.z;
	}

	Quaternion Quaternion::Slerp(Quaternion other, float dt) const
	{
		float cosom = DotProduct(other);
		Quaternion end = other;

		if (cosom < 0.0f) 
		{
			cosom = -cosom;
			end.x = -end.x;
			end.y = -end.y;
			end.z = -end.z;
			end.w = -end.w;
		}
		
		float k0, k1;
		if(cosom > 0.9999f)
		{
			k0 = 1.0f - dt;
			k1 = dt;
		}
		else
		{
			float omega = std::acos(cosom);
			float sinom = std::sin(omega);
			k0 = std::sin((1.0f - dt) * omega) / sinom;
			k1 = std::sin(dt * omega) / sinom;
		}
		
		end.x = k0 * x + k1 * end.x;
		end.y = k0 * y + k1 * end.y;
		end.z = k0 * z + k1 * end.z;
		end.w = k0 * w + k1 * end.w;
		
		return end;
	}

	Quaternion Quaternion::Conjugate() const
	{
		Quaternion other;

		other.x = -x;
		other.y = -y;
		other.z = -z;
		other.w = w;

		return other;
	}

	Quaternion Quaternion::Pow(float exp) const
	{
		if(std::abs(w) > .9999f) return *this;
		
		float alpha = std::acos(w);
		float alpha2 = alpha * exp;
		float mul = std::sin(alpha2) / std::sin(alpha);
		
		Quaternion other;

		other.w = std::cos(alpha2);
		other.x = x * mul;
		other.y = y * mul;
		other.z = z * mul;
		
		return other;
	}

	void Quaternion::Normalize()
	{
		float mag = std::sqrt(x * x + y * y + z * z + w * w);
		if(mag > 0.0f)
		{
			mag = 1 / mag;
			w *= mag;
			x *= mag;
			y *= mag;
			z *= mag;
		} 
		else 
		{
			Identity();
		}
	}

	Quaternion Quaternion::Clone() const
	{
		return Quaternion(*this);
	}

	bool Quaternion::operator == (Quaternion other) const
	{
		return x == other.x && y == other.y && z == other.z && w == other.w;
	}
	
	bool Quaternion::operator != (Quaternion other) const
	{
		return x != other.x || y != other.y || z != other.z || w != other.w;
	}

	Quaternion &Quaternion::operator = (Quaternion other)
	{
		x = other.x; y = other.y; z = other.z; w = other.w;
		return *this;
	}
	
	Quaternion Quaternion::operator * (Quaternion other) const
	{
		return Quaternion(
			w * other.x + x * other.w + y * other.z - z * other.y, 
			w * other.y - x * other.z + y * other.w + z * other.x, 
			w * other.z + x * other.y - y * other.x + z * other.w, 
			w * other.w - x * other.x - y * other.y - z * other.z
		);
	}
}
