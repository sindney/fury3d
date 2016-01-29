#ifndef _FURY_QUATERNION_H_
#define _FURY_QUATERNION_H_

#include "Macros.h"

namespace fury
{
	class Vector4;

	class FURY_API Quaternion
	{
	public:
		
		float x, y, z, w;
		
		Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
		
		Quaternion(const Quaternion &other) : x(other.x), y(other.y), z(other.z), w(other.w) {}
		
		Quaternion(float nx, float ny, float nz, float nw) : x(nx), y(ny), z(nz), w(nw) {}
		
		void Identity();

		float DotProduct(Quaternion other) const;

		Quaternion Slerp(Quaternion other, float dt) const;

		Quaternion Conjugate() const;

		Quaternion Pow(float exp) const;

		void Normalize();

		Quaternion Clone() const;

		bool operator == (Quaternion other) const;
		
		bool operator != (Quaternion other) const;

		Quaternion &operator = (Quaternion other);
		
		Quaternion operator * (Quaternion other) const;
	};
}

#endif // _FURY_QUATERNION_H_