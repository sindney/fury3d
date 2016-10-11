#ifndef _FURY_MATHUTIL_H_
#define _FURY_MATHUTIL_H_

#include <vector>
#include <memory>

#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"
#include "Fury/Matrix4.h"

namespace fury
{
	class SceneNode;

	// Euler in YXZ order.
	class FURY_API MathUtil
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

		static bool PointInCone(Vector4 coneCenter, Vector4 coneDir, float height, float theta, Vector4 point);

	};
}

#endif // _FURY_MATHUTIL_H_
