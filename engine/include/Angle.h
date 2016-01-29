#ifndef _FURY_ANGLE_H_
#define _FURY_ANGLE_H_

#include "Quaternion.h"
#include "Vector4.h"

namespace fury
{
	// Euler in YXZ order.
	class FURY_API Angle
	{
	public:

		static const float PI;

		static const float HalfPI;

		static const float DegToRad;

		static const float RadToDeg;

		static float DegreeToRadian(float deg);

		static float RadianToDegree(float rad);

		static Quaternion AxisRadToQuat(Vector4 axis, float rad);

		static Quaternion AxisRadToQuat(Vector4 axisRad);

		static Vector4 AxisRadToEulerRad(Vector4 axis, float rad);

		static Vector4 AxisRadToEulerRad(Vector4 axisRad);

		static Quaternion EulerRadToQuat(Vector4 eulerRad);

		static Quaternion EulerRadToQuat(float yaw, float pitch, float roll);

		static Vector4 QuatToAxisRad(Quaternion quat);

		static Vector4 QuatToEulerRad(Quaternion quat);

	};
}

#endif // _FURY_ANGLE_H_