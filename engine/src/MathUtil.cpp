#include <cmath>

#include "MathUtil.h"
#include "SceneNode.h"

namespace fury
{
	const float MathUtil::PI = 3.1415926535897932384626433832795028841971693993751f;

	const float MathUtil::HalfPI = 1.5707963267948966192313216916397514420985846996875f;

	// PI / 180
	const float MathUtil::DegToRad = 0.017453292519943295769236907684886127134428718885417f;

	// 180 / PI
	const float MathUtil::RadToDeg = 57.295779513082320876798154814105170332405472466565f;

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

	Matrix4 MathUtil::GetCropMatrix(Matrix4 lightMatrix, Frustum frustum, std::vector<std::shared_ptr<SceneNode>> &casters)
	{
		// limit z
		auto corners = frustum.GetCurrentCorners();
		auto pos = lightMatrix.Multiply(corners[0]);
		float minZ = pos.z, maxZ = pos.z;
		for (auto corner : corners)
		{
			pos = lightMatrix.Multiply(corner);
			if (pos.z > maxZ) maxZ = pos.z;
			if (pos.z < minZ) minZ = pos.z;
		}

		for (auto caster : casters)
		{
			auto corners = caster->GetWorldAABB().GetCorners();
			for (auto corner : corners)
			{
				pos = lightMatrix.Multiply(corner);
				if (pos.z > maxZ) maxZ = pos.z;
			}
		}

		Matrix4 projMatrix;
		projMatrix.OrthoOffCenter(-1.0f, 1.0f, -1.0f, 1.0f, maxZ, minZ);

		// limit xy
		float maxX = 0.0f, maxY = 0.0f;
		float minX = 0.0f, minY = 0.0f;

		auto mvp = projMatrix * lightMatrix;

		pos = mvp.Multiply(corners[0]);
		maxX = minX = pos.x / pos.w;
		maxY = minY = pos.y / pos.w;

		for (auto corner : corners)
		{
			pos = mvp.Multiply(corner);

			pos.x /= pos.w;
			pos.y /= pos.w;

			if (pos.x > maxX) maxX = pos.x;
			if (pos.x < minX) minX = pos.x;
			if (pos.y > maxY) maxY = pos.y;
			if (pos.y < minY) minY = pos.y;
		}

		// build crop matrix
		float scaleX = 2.0f / (maxX - minX);
		float scaleY = 2.0f / (maxY - minY);
		float offsetX = -0.5f * (maxX + minX) * scaleX;
		float offsetY = -0.5f * (maxY + minY) * scaleY;

		Matrix4 cropMatrix(
		{
			scaleX, 0.0f, 0.0f, 0.0f, 
			0.0f, scaleY, 0.0f, 0.0f, 
			0.0f, 0.0f, 1.0f, 0.0f, 
			offsetX, offsetY, 0.0f, 1.0f
		});

		return projMatrix * cropMatrix;
	}

	void MathUtil::FilterNodes(const Collidable &collider, std::vector<std::shared_ptr<SceneNode>> &possibles, std::vector<std::shared_ptr<SceneNode>> &collisions)
	{
		collisions.erase(collisions.begin(), collisions.end());

		for (auto possible : possibles)
		{
			if (collider.IsInsideFast(possible->GetWorldAABB()))
				collisions.push_back(possible);
		}
	}
}