#include <cmath>

#include "Fury/MathUtil.h"
#include "Fury/SceneNode.h"

namespace fury
{
	const float MathUtil::PI		= 3.1415926536f;

	const float MathUtil::HalfPI	= 1.5707963268f;

	// PI / 180
	const float MathUtil::DegToRad	= 0.0174532925f;

	// 180 / PI
	const float MathUtil::RadToDeg	= 57.295779513f;

	float MathUtil::DegreeToRadian(float deg)
	{
		return deg * DegToRad;
	}

	float MathUtil::RadianToDegree(float rad)
	{
		return rad * RadToDeg;
	}

	Quaternion MathUtil::AxisRadToQuat(Vector4 axis, float rad)
	{
		float t2 = rad * .5f;
		float st2 = std::sin(t2);
		return Quaternion(axis.x * st2, axis.y * st2,
			axis.z * st2, std::cos(t2));
	}

	Quaternion MathUtil::AxisRadToQuat(Vector4 axisRad)
	{
		return AxisRadToQuat(axisRad, axisRad.w);
	}

	Vector4 MathUtil::AxisRadToEulerRad(Vector4 axis, float rad)
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

	Vector4 MathUtil::AxisRadToEulerRad(Vector4 axisRad)
	{
		return AxisRadToEulerRad(axisRad, axisRad.w);
	}

	Quaternion MathUtil::EulerRadToQuat(Vector4 eulerRad)
	{
		return EulerRadToQuat(eulerRad.x, eulerRad.y, eulerRad.z);
	}

	Quaternion MathUtil::EulerRadToQuat(float yaw, float pitch, float roll)
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

	Vector4 MathUtil::QuatToAxisRad(Quaternion quat)
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

	Vector4 MathUtil::QuatToEulerRad(Quaternion quat)
	{
		return AxisRadToEulerRad(QuatToAxisRad(quat));
	}

	bool MathUtil::PointInCone(Vector4 coneCenter, Vector4 coneDir, float height, float theta, Vector4 point)
	{
		float cosTheta = std::cos(theta);
		float cosTheta2 = cosTheta * cosTheta;

		Vector4 dir = point - coneCenter;
		float dot = coneDir * dir;

		if (dot >= 0 && dot * dot >= cosTheta2 * (dir * dir) &&
			(dir.Project(coneDir).SquareLength() <= height * height))
			return true;

		return false;
	}

	bool MathUtil::Decompose(const Matrix4 &m, Vector4 &translation, Quaternion &rotation, Vector4 &scale)
	{
		// Matrix4 is column-major (OpenGL). The translation lives in
		// column 3 (Raw[12..14]); columns 0..2 hold the rotation*scale
		// basis. Decomposition is the standard "lengths give scale,
		// normalized columns give rotation" recipe; works for pure TRS
		// matrices, which is all the gizmo round-trip needs to handle.
		translation = Vector4(m.Raw[12], m.Raw[13], m.Raw[14], 1.0f);

		Vector4 col0(m.Raw[0], m.Raw[1], m.Raw[2], 0.0f);
		Vector4 col1(m.Raw[4], m.Raw[5], m.Raw[6], 0.0f);
		Vector4 col2(m.Raw[8], m.Raw[9], m.Raw[10], 0.0f);

		float sx = col0.Length();
		float sy = col1.Length();
		float sz = col2.Length();

		if (sx < 1e-6f || sy < 1e-6f || sz < 1e-6f)
			return false;

		// A negative determinant means the basis is mirrored -- fold the
		// sign onto the X axis (consistent with glm's convention) so the
		// remaining basis is a proper rotation.
		Vector4 cross_yz(
			col1.y * col2.z - col1.z * col2.y,
			col1.z * col2.x - col1.x * col2.z,
			col1.x * col2.y - col1.y * col2.x,
			0.0f);
		float det = col0.x * cross_yz.x + col0.y * cross_yz.y + col0.z * cross_yz.z;
		if (det < 0.0f)
		{
			sx = -sx;
			col0 = Vector4(-col0.x, -col0.y, -col0.z, 0.0f);
		}

		scale = Vector4(sx, sy, sz, 0.0f);

		Vector4 r0 = col0 * (1.0f / sx);
		Vector4 r1 = col1 * (1.0f / sy);
		Vector4 r2 = col2 * (1.0f / sz);

		// Build a quaternion from a 3x3 rotation matrix expressed
		// column-by-column. Standard branch-on-trace recipe (Shoemake).
		float trace = r0.x + r1.y + r2.z;
		if (trace > 0.0f)
		{
			float s = std::sqrt(trace + 1.0f) * 2.0f;
			rotation.w = 0.25f * s;
			rotation.x = (r1.z - r2.y) / s;
			rotation.y = (r2.x - r0.z) / s;
			rotation.z = (r0.y - r1.x) / s;
		}
		else if (r0.x > r1.y && r0.x > r2.z)
		{
			float s = std::sqrt(1.0f + r0.x - r1.y - r2.z) * 2.0f;
			rotation.w = (r1.z - r2.y) / s;
			rotation.x = 0.25f * s;
			rotation.y = (r1.x + r0.y) / s;
			rotation.z = (r2.x + r0.z) / s;
		}
		else if (r1.y > r2.z)
		{
			float s = std::sqrt(1.0f + r1.y - r0.x - r2.z) * 2.0f;
			rotation.w = (r2.x - r0.z) / s;
			rotation.x = (r1.x + r0.y) / s;
			rotation.y = 0.25f * s;
			rotation.z = (r2.y + r1.z) / s;
		}
		else
		{
			float s = std::sqrt(1.0f + r2.z - r0.x - r1.y) * 2.0f;
			rotation.w = (r0.y - r1.x) / s;
			rotation.x = (r2.x + r0.z) / s;
			rotation.y = (r2.y + r1.z) / s;
			rotation.z = 0.25f * s;
		}

		rotation.Normalize();
		return true;
	}
}