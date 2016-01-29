#include <cmath>

#include "Angle.h"

namespace fury
{
	const float Angle::PI = 3.1415926535897932384626433832795028841971693993751f;

	const float Angle::HalfPI = 1.5707963267948966192313216916397514420985846996875f;

	// PI / 180
	const float Angle::DegToRad = 0.017453292519943295769236907684886127134428718885417f;

	// 180 / PI
	const float Angle::RadToDeg = 57.295779513082320876798154814105170332405472466565f;

	float Angle::DegreeToRadian(float deg)
	{
		return deg * DegToRad;
	}

	float Angle::RadianToDegree(float rad)
	{
		return rad * RadToDeg;
	}

	Quaternion Angle::AxisRadToQuat(Vector4 axis, float rad)
	{
		float t2 = rad * .5f;
		float st2 = std::sin(t2);
		return Quaternion(axis.x * st2, axis.y * st2,
			axis.z * st2, std::cos(t2));
	}

	Quaternion Angle::AxisRadToQuat(Vector4 axisRad)
	{
		return AxisRadToQuat(axisRad, axisRad.w);
	}

	Vector4 Angle::AxisRadToEulerRad(Vector4 axis, float rad)
	{
		Vector4 eulerRadian;

		float s = sin(rad);
		float c = cos(rad);
		float t = 1 - c;

		if ((axis.x * axis.y * t + axis.z * s) > 0.998f)
		{
			// north pole singularity detected
			eulerRadian.x = 2 * atan2(axis.x * sin(axis.w / 2), cos(axis.w / 2));
			eulerRadian.y = 0;
			eulerRadian.z = PI / 2;
		}
		else if ((axis.x * axis.y * t + axis.z * s) < -0.998f)
		{
			// south pole singularity detected
			eulerRadian.x = -2 * atan2(axis.x * sin(axis.w / 2), cos(axis.w / 2));
			eulerRadian.y = 0;
			eulerRadian.y = -PI / 2;
		}
		else
		{
			eulerRadian.x = atan2(
				axis.y * s - axis.x * axis.z * t,
				1 - (axis.y * axis.y + axis.z * axis.z) * t
				);
			eulerRadian.y = atan2(
				axis.x * s - axis.y * axis.z * t,
				1 - (axis.x * axis.x + axis.z * axis.z) * t
				);
			eulerRadian.z = asin(axis.x * axis.y * t + axis.z * s);
		}

		return eulerRadian;
	}

	Vector4 Angle::AxisRadToEulerRad(Vector4 axisRad)
	{
		return AxisRadToEulerRad(axisRad, axisRad.w);
	}

	Quaternion Angle::EulerRadToQuat(Vector4 eulerRad)
	{
		return EulerRadToQuat(eulerRad.x, eulerRad.y, eulerRad.z);
	}

	Quaternion Angle::EulerRadToQuat(float yaw, float pitch, float roll)
	{
		float cx = std::cos(yaw / 2);
		float sx = std::sin(yaw / 2);
		float cy = std::cos(pitch / 2);
		float sy = std::sin(pitch / 2);
		float cz = std::cos(roll / 2);
		float sz = std::sin(roll / 2);

		return Quaternion(
			sx * sz * cy + cx * cz * sy,
			sx * cz * cy + cx * sz * sy,
			cx * sz * cy - sx * cz * sy,
			cx * cz * cy - sx * sz * sy
		);
	}

	Vector4 Angle::QuatToAxisRad(Quaternion quat)
	{
		Vector4 axisRadian;
		axisRadian.w = std::acos(quat.w) * 2.0f;

		float a = 1.0f - quat.w * quat.w;
		if (a <= 0.0f)
		{
			axisRadian.x = 1.0f;
			axisRadian.y = axisRadian.z = 0.0f;
		}
		else
		{
			float b = 1.0f / std::sqrt(a);
			axisRadian.x = quat.x * b;
			axisRadian.y = quat.y * b;
			axisRadian.z = quat.z * b;
		}

		return axisRadian;
	}

	Vector4 Angle::QuatToEulerRad(Quaternion quat)
	{
		return AxisRadToEulerRad(QuatToAxisRad(quat));
	}
}