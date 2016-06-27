#include <cmath>

#include "MathUtil.h"
#include "BoxBounds.h"
#include "Matrix4.h"
#include "Plane.h"
#include "Quaternion.h"

namespace fury
{
	std::string Matrix4::PROJECTION_MATRIX = "projection_matrix";

	std::string Matrix4::INVERT_VIEW_MATRIX = "invert_view_matrix";

	std::string Matrix4::WORLD_MATRIX = "world_matrix";
	
	Matrix4::Matrix4()
	{
		Identity();
	}

	Matrix4::Matrix4(const float raw[])
	{
		for(int i = 0; i < 16; i++) Raw[i] = raw[i];
	}

	Matrix4::Matrix4(std::initializer_list<float> raw)
	{
		ASSERT_MSG(raw.size() == 16, "Incorrect matrix data size!");
		std::copy(raw.begin(), raw.end(), Raw);
	}

	Matrix4::Matrix4(const Matrix4 &other) 
	{
		for(int i = 0; i < 16; i++) Raw[i] = other.Raw[i];
	}

	void Matrix4::Identity()
	{
		Raw[0] = 1.0f; Raw[4] = 0.0f; Raw[8] = 0.0f; Raw[12] = 0.0f;
		Raw[1] = 0.0f; Raw[5] = 1.0f; Raw[9] = 0.0f; Raw[13] = 0.0f;
		Raw[2] = 0.0f; Raw[6] = 0.0f; Raw[10] = 1.0f; Raw[14] = 0.0f;
		Raw[3] = 0.0f; Raw[7] = 0.0f; Raw[11] = 0.0f; Raw[15] = 1.0f;
	}
	
	void Matrix4::Translate(Vector4 position)
	{
		Identity();

		Raw[12] = position.x; 
		Raw[13] = position.y; 
		Raw[14] = position.z;
	}

	void Matrix4::AppendTranslation(Vector4 position)
	{
		Matrix4 matrix;
		matrix.Translate(position);
		*this = *this * matrix;
	}

	void Matrix4::PrependTranslation(Vector4 position)
	{
		Matrix4 matrix;
		matrix.Translate(position);
		*this = matrix * *this;
	}

	void Matrix4::Rotate(Quaternion rotation)
	{
		Identity();

		float ww = 2.0f * rotation.w;
		float xx = 2.0f * rotation.x;
		float yy = 2.0f * rotation.y;
		float zz = 2.0f * rotation.z;
		
		Raw[0] = 1.0f - yy * rotation.y - zz * rotation.z;
		Raw[1] = xx * rotation.y + ww * rotation.z;
		Raw[2] = xx * rotation.z - ww * rotation.y;
		Raw[4] = xx * rotation.y - ww * rotation.z;
		Raw[5] = 1.0f - xx * rotation.x - zz * rotation.z;
		Raw[6] = yy * rotation.z + ww * rotation.x;
		Raw[8] = xx * rotation.z + ww * rotation.y;
		Raw[9] = yy * rotation.z - ww * rotation.x;
		Raw[10] = 1.0f - xx * rotation.x - yy * rotation.y;
	}

	void Matrix4::AppendRotation(Quaternion rotation)
	{
		Matrix4 matrix;
		matrix.Rotate(rotation);
		*this = *this * matrix;
	}

	void Matrix4::PrependRotation(Quaternion rotation)
	{
		Matrix4 matrix;
		matrix.Rotate(rotation);
		*this = matrix * *this;
	}
	
	void Matrix4::Scale(Vector4 scale)
	{
		Identity();

		Raw[0] = scale.x;
		Raw[5] = scale.y;
		Raw[10] = scale.z;
	}

	void Matrix4::AppendScale(Vector4 scale)
	{
		Matrix4 matrix;
		matrix.Scale(scale);
		*this = *this * matrix;
	}

	void Matrix4::PrependScale(Vector4 scale)
	{
		Matrix4 matrix;
		matrix.Scale(scale);
		*this = matrix * *this;
	}

	void Matrix4::PerspectiveFov(float fov, float ratio, float near, float far)
	{
		float top = near * tan(fov / 2.0f);
		float right = top * ratio;
		PerspectiveOffCenter(-right, right, -top, top, near, far);
	}

	void Matrix4::PerspectiveOffCenter(float left, float right, float bottom, float top, float near, float far)
	{
		for(int i = 0; i < 16; i++) Raw[i] = 0.0f;

		Raw[0] = 2 * near / (right - left);
		Raw[5] = 2 * near / (top - bottom);
		Raw[8] = (right + left) / (right - left);
		Raw[9] = (top + bottom) / (top - bottom);
		Raw[10] = - (far + near) / (far - near);
		Raw[11] = -1;
		Raw[14] = - 2 * far * near / (far - near);
	}
	
	void Matrix4::OrthoOffCenter(float left, float right, float bottom, float top, float near, float far)
	{
		for(int i = 0; i < 16; i++) Raw[i] = 0.0f;

		Raw[0] = 2 / (right - left);
		Raw[5] = 2 / (top - bottom);
		Raw[10] = 2 / (far - near);
		Raw[12] = - (right + left) / (right - left);
		Raw[13] = - (top + bottom) / (top - bottom);
		Raw[14] = - (far + near) / (far - near);
		Raw[15] = 1;
	}

	void Matrix4::LookAt(Vector4 eye, Vector4 at, Vector4 up)
	{
		Vector4 zAxis = (eye - at).Normalized();
		Vector4 xAxis = up.CrossProduct(zAxis).Normalized();
		Vector4 yAxis = zAxis.CrossProduct(xAxis);

		Raw[0] = xAxis.x;	Raw[4] = xAxis.y;	Raw[8] = xAxis.z;	Raw[12] = -(xAxis * eye);
		Raw[1] = yAxis.x;	Raw[5] = yAxis.y;	Raw[9] = yAxis.z;	Raw[13] = -(yAxis * eye);
		Raw[2] = zAxis.x;	Raw[6] = zAxis.y;	Raw[10] = zAxis.z;	Raw[14] = -(zAxis * eye);
		Raw[3] = 0.0f;		Raw[7] = 0.0f;		Raw[11] = 0.0f;		Raw[15] = 1.0f;
	}

	Matrix4 Matrix4::Transpose() const
	{
		const float data[] = { 
			Raw[0], Raw[4], Raw[8], Raw[12], 
			Raw[1], Raw[5], Raw[9], Raw[13], 
			Raw[2], Raw[6], Raw[10], Raw[14], 
			Raw[3], Raw[7], Raw[11], Raw[15]
		};

		return Matrix4(data);
	}

	Vector4 Matrix4::Multiply(Vector4 data) const
	{
		return Vector4(
			data.x * Raw[0] + data.y * Raw[4] + data.z * Raw[8] + data.w * Raw[12],
			data.x * Raw[1] + data.y * Raw[5] + data.z * Raw[9] + data.w * Raw[13],
			data.x * Raw[2] + data.y * Raw[6] + data.z * Raw[10] + data.w * Raw[14],
			data.x * Raw[3] + data.y * Raw[7] + data.z * Raw[11] + data.w * Raw[15]
		);
	}

	Quaternion Matrix4::Multiply(Quaternion data) const
	{
		Vector4 axis = MathUtil::QuatToAxisRad(data);
		float radian = axis.w;
		axis.w = 0.0f;

		axis = Multiply(axis);

		return MathUtil::AxisRadToQuat(axis, radian);
	}

	BoxBounds Matrix4::Multiply(const BoxBounds &aabb) const
	{
		Vector4 max = aabb.GetMax();
		Vector4 min = aabb.GetMin();

		Vector4 vertices[] = {
			Multiply(Vector4(min, 1.0f)),
			Multiply(Vector4(max.x, min.y, min.z, 1.0f)),
			Multiply(Vector4(min.x, max.y, min.z, 1.0f)),
			Multiply(Vector4(max.x, max.y, min.z, 1.0f)),
			Multiply(Vector4(min.x, min.y, max.z, 1.0f)),
			Multiply(Vector4(max.x, min.y, max.z, 1.0f)),
			Multiply(Vector4(min.x, max.y, max.z, 1.0f)),
			Multiply(Vector4(max, 1.0f))
		};

		max = min = vertices[0];

		for (Vector4 &vt : vertices)
		{
			if (vt.x > max.x)
				max.x = vt.x;
			else if (vt.x < min.x)
				min.x = vt.x;
			if (vt.y > max.y)
				max.y = vt.y;
			else if (vt.y < min.y)
				min.y = vt.y;
			if (vt.z > max.z)
				max.z = vt.z;
			else if (vt.z < min.z)
				min.z = vt.z;
		}

		return BoxBounds(min, max);
	}

	Plane Matrix4::Multiply(const Plane &data) const
	{
		Vector4 point = Multiply(Vector4(data.GetNormal() * data.GetDistance(), 1.0f));
		Vector4 normal = Transpose().Inverse().Multiply(Vector4(data.GetNormal(), 0.0f));

		return Plane(normal, point);
	}

	Matrix4 Matrix4::Inverse() const
	{
		Matrix4 output;
		
		float det = Raw[0] * (Raw[5] * Raw[10] - Raw[6] * Raw[9])
					+ Raw[1] * (Raw[6] * Raw[8] - Raw[4] * Raw[10])
					+ Raw[2] * (Raw[4] * Raw[9] - Raw[5] * Raw[8]);
		if(det != 0)
		{
			float det2 = 1.0f / det;
			output.Raw[0] = (Raw[5] * Raw[10] - Raw[6] * Raw[9]) * det2;
			output.Raw[1] = (Raw[2] * Raw[9] - Raw[1] * Raw[10]) * det2;
			output.Raw[2] = (Raw[1] * Raw[6] - Raw[2] * Raw[5]) * det2;
			output.Raw[3] = 0.0f;
			output.Raw[4] = (Raw[6] * Raw[8] - Raw[4] * Raw[10]) * det2;
			output.Raw[5] = (Raw[0] * Raw[10] - Raw[2] * Raw[8]) * det2;
			output.Raw[6] = (Raw[2] * Raw[4] - Raw[0] * Raw[6]) * det2;
			output.Raw[7] = 0.0f;
			output.Raw[8] = (Raw[4] * Raw[9] - Raw[5] * Raw[8]) * det2;
			output.Raw[9] = (Raw[1] * Raw[8] - Raw[0] * Raw[9]) * det2;
			output.Raw[10] = (Raw[0] * Raw[5] - Raw[1] * Raw[4]) * det2;
			output.Raw[11] = 0.0f;
			output.Raw[12] = -(Raw[12] * output.Raw[0] + Raw[13] * output.Raw[4] + Raw[14] * output.Raw[8]);
			output.Raw[13] = -(Raw[12] * output.Raw[1] + Raw[13] * output.Raw[5] + Raw[14] * output.Raw[9]);
			output.Raw[14] = -(Raw[12] * output.Raw[2] + Raw[13] * output.Raw[6] + Raw[14] * output.Raw[10]);
			output.Raw[15] = 1.0f;
		}
		
		return output;
	}

	Matrix4 Matrix4::Clone() const
	{
		return Matrix4(this->Raw);
	}

	bool Matrix4::operator == (const Matrix4 &other) const
	{
		for(int i = 0; i < 16; i++)
		{
			if(Raw[i] != other.Raw[i]) 
				return false;
		}
		return true;
	}
	
	bool Matrix4::operator != (const Matrix4 &other) const
	{
		for(int i = 0; i < 16; i++)
		{
			if(Raw[i] != other.Raw[i]) 
				return true;
		}
		return false;
	}

	Matrix4 &Matrix4::operator = (const Matrix4 &other)
	{
		for(int i = 0; i < 16; i++)
			Raw[i] = other.Raw[i];
		return *this;
	}

	Matrix4 Matrix4::operator * (const Matrix4 &other) const
	{
		Matrix4 output;
		
		output.Raw[0] = Raw[0] * other.Raw[0] + Raw[4] * other.Raw[1] + Raw[8]	* other.Raw[2] + Raw[12] * other.Raw[3];
		output.Raw[1] = Raw[1] * other.Raw[0] + Raw[5] * other.Raw[1] + Raw[9]	* other.Raw[2] + Raw[13] * other.Raw[3];
		output.Raw[2] = Raw[2] * other.Raw[0] + Raw[6] * other.Raw[1] + Raw[10] * other.Raw[2] + Raw[14] * other.Raw[3];
		output.Raw[3] = Raw[3] * other.Raw[0] + Raw[7] * other.Raw[1] + Raw[11] * other.Raw[2] + Raw[15] * other.Raw[3];

		output.Raw[4] = Raw[0] * other.Raw[4] + Raw[4] * other.Raw[5] + Raw[8]	* other.Raw[6] + Raw[12] * other.Raw[7];
		output.Raw[5] = Raw[1] * other.Raw[4] + Raw[5] * other.Raw[5] + Raw[9]	* other.Raw[6] + Raw[13] * other.Raw[7];
		output.Raw[6] = Raw[2] * other.Raw[4] + Raw[6] * other.Raw[5] + Raw[10] * other.Raw[6] + Raw[14] * other.Raw[7];
		output.Raw[7] = Raw[3] * other.Raw[4] + Raw[7] * other.Raw[5] + Raw[11] * other.Raw[6] + Raw[15] * other.Raw[7];

		output.Raw[8] = Raw[0] * other.Raw[8] + Raw[4] * other.Raw[9] + Raw[8] * other.Raw[10] + Raw[12] * other.Raw[11];
		output.Raw[9] = Raw[1] * other.Raw[8] + Raw[5] * other.Raw[9] + Raw[9] * other.Raw[10] + Raw[13] * other.Raw[11];
		output.Raw[10] = Raw[2] * other.Raw[8] + Raw[6] * other.Raw[9] + Raw[10] * other.Raw[10] + Raw[14] * other.Raw[11];
		output.Raw[11] = Raw[3] * other.Raw[8] + Raw[7] * other.Raw[9] + Raw[11] * other.Raw[10] + Raw[15] * other.Raw[11];

		output.Raw[12] = Raw[0] * other.Raw[12] + Raw[4] * other.Raw[13] + Raw[8] * other.Raw[14] + Raw[12] * other.Raw[15];
		output.Raw[13] = Raw[1] * other.Raw[12] + Raw[5] * other.Raw[13] + Raw[9] * other.Raw[14] + Raw[13] * other.Raw[15];
		output.Raw[14] = Raw[2] * other.Raw[12] + Raw[6] * other.Raw[13] + Raw[10] * other.Raw[14] + Raw[14] * other.Raw[15];
		output.Raw[15] = Raw[3] * other.Raw[12] + Raw[7] * other.Raw[13] + Raw[11] * other.Raw[14] + Raw[15] * other.Raw[15];
		
		return output;
	}
}