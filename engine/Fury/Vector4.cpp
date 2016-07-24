#include <cmath>

#include "Fury/Vector4.h"

namespace fury
{
	const Vector4 Vector4::YAxis(0.0f, 1.0f, 0.0f, 0.0f);

	const Vector4 Vector4::NegYAxis(0.0f, -1.0f, 0.0f, 0.0f);

	const Vector4 Vector4::XAxis(1.0f, 0.0f, 0.0f, 0.0f);

	const Vector4 Vector4::NegXAxis(-1.0f, 0.0f, 0.0f, 0.0f);

	const Vector4 Vector4::ZAxis(0.0f, 0.0f, 1.0f, 0.0f);

	const Vector4 Vector4::NegZAxis(0.0f, 0.0f, -1.0f, 0.0f);

	void Vector4::Absolute()
	{
		x = std::abs(x);
		y = std::abs(y);
		z = std::abs(z);
		w = 1.0f;
	}

	void Vector4::Zero()
	{
		x = y = z = 0.0f;
		w = 1.0f;
	}
	
	void Vector4::Normalize()
	{
		float mag = x * x + y * y + z * z;
		if(mag > 0.0f)
		{
			float a = 1.0f / sqrt(mag);
			x *= a; y *= a; z *= a;
		}
		w = 1.0f;
	}

	Vector4 Vector4::Normalized() const
	{
		Vector4 vector(*this);
		vector.Normalize();
		return vector;
	}

	float Vector4::Length() const
	{
		return sqrt(x * x + y * y + z * z);
	}

	float Vector4::SquareLength() const
	{
		return x * x + y * y + z * z;
	}

	float Vector4::Distance(Vector4 other) const
	{
		float dx = x - other.x;
		float dy = y - other.y;
		float dz = z - other.z;
		return sqrt(dx * dx + dy * dy + dz * dz);
	}

	Vector4 Vector4::CrossProduct(Vector4 other) const
	{
		return Vector4(
			y * other.z - z * other.y, 
			z * other.x - x * other.z, 
			x * other.y - y * other.x, 
			1.0f
		);
	}

	Vector4 Vector4::Project(Vector4 other) const
	{
		return other * (*this * other / other.SquareLength());
	}

	Vector4 Vector4::Clone() const
	{
		return Vector4(*this);
	}
	
	bool Vector4::operator == (Vector4 other) const 
	{
		return x == other.x && y == other.y && z == other.z;
	}
	
	bool Vector4::operator != (Vector4 other) const 
	{
		return x != other.x || y != other.y || z != other.z;
	}

	bool Vector4::operator < (Vector4 other) const
	{
		return x < other.x && y < other.y && z < other.z;
	}

	bool Vector4::operator <= (Vector4 other) const
	{
		return x <= other.x && y <= other.y && z <= other.z;
	}

	bool Vector4::operator > (Vector4 other) const
	{
		return x > other.x && y > other.y && z > other.z;
	}

	bool Vector4::operator >= (Vector4 other) const
	{
		return x >= other.x && y >= other.y && z >= other.z;
	}

	Vector4 &Vector4::operator = (Vector4 other) 
	{
		x = other.x; y = other.y; z = other.z;
		return *this;
	}
	
	Vector4 Vector4::operator - () const 
	{
		return Vector4(-x, -y, -z, 1.0f);
	}
	
	Vector4 Vector4::operator + (Vector4 other) const 
	{
		return Vector4(x + other.x, y + other.y, z + other.z, 1.0f);
	}
	
	Vector4 Vector4::operator - (Vector4 other) const 
	{
		return Vector4(x - other.x, y - other.y, z - other.z, 1.0f);
	}

	float Vector4::operator * (Vector4 other) const 
	{
		return x * other.x + y * other.y + z * other.z;
	}
	
	Vector4 Vector4::operator * (const float other) const
	{
		return Vector4(x * other, y * other, z * other, 1.0f);
	}

	Vector4 Vector4::operator / (const float other) const 
	{
		float i = 1.0f / other;
		return Vector4(x * i, y * i, z * i, 1.0f);
	}

	std::ostream &operator << (std::ostream &os, const Vector4 &data)
	{
		return os << "Vector4(" << data.x << ", " << data.y << ", " << data.z << ", " << data.w << ")";
	}
}