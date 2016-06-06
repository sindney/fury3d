#ifndef _FURY_VECTOR4_H_
#define _FURY_VECTOR4_H_

#include "Macros.h"

namespace fury
{
	/**
	 *	When you need a Vector4 with special w.
	 *	Call Vector4(yourVector, yourW) to create one to make sure it's w is correct.
	 */
	class FURY_API Vector4
	{
	public:

		// 0,1,0,0
		static const Vector4 YAxis; 
		
		// 0,-1,0,0
		static const Vector4 NegYAxis;

		// 1,0,0,0
		static const Vector4 XAxis;

		// -1,0,0,0
		static const Vector4 NegXAxis;

		// 0,0,1,0
		static const Vector4 ZAxis;

		// 0,0,-1,0
		static const Vector4 NegZAxis;

		float x, y, z, w;

		Vector4() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
		
		Vector4(const Vector4 &other) : x(other.x), y(other.y), z(other.z), w(other.w) {}

		Vector4(Vector4 other, const float w) : x(other.x), y(other.y), z(other.z), w(w) {}
		
		Vector4(float value) : x(value), y(value), z(value), w(1) {}

		Vector4(float value, float w) : x(value), y(value), z(value), w(w) {}

		Vector4(float x, float y, float z) : x(x), y(y), z(z), w(1) {}

		Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		
		void Absolute();

		void Zero();
		
		void Normalize();

		Vector4 Normalized() const;

		float Length() const;

		float SquareLength() const;

		float Distance(Vector4 other) const;

		Vector4 CrossProduct(Vector4 other) const;

		Vector4 Project(Vector4 other) const;

		Vector4 Clone() const;
		
		// all operator ignores w component.
		// or will simply set w to 1.0

		bool operator == (Vector4 other) const;
		
		bool operator != (Vector4 other) const;

		bool operator < (Vector4 other) const;

		bool operator <= (Vector4 other) const;

		bool operator > (Vector4 other) const;

		bool operator >= (Vector4 other) const;

		Vector4 &operator = (Vector4 other);
		
		Vector4 operator - () const;
		
		Vector4 operator + (Vector4 other) const;
		
		Vector4 operator - (Vector4 other) const;

		float operator * (Vector4 other) const;

		Vector4 operator * (const float other) const;
		
		Vector4 operator / (const float other) const;
		
	};
}

#endif // _FURY_VECTOR4_H_