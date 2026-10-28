#ifndef _FURY_MATRIX4_H_
#define _FURY_MATRIX4_H_

#include <string>
#include <initializer_list>

#include "Macros.h"

namespace fury
{
	class BoxBounds;

	class Quaternion;

	class Vector4;

	class Plane;

	/**
	 *	Matrix stores position info in Raw[12, 13, 14] like opengl.
	 *	0	4	8	12
	 *	1	5	9	13
	 *	2	6	10	14
	 *	3	7	11	15
	 */
	class FURY_API Matrix4
	{
	public:

		static std::string PROJECTION_MATRIX;

		static std::string INVERT_VIEW_MATRIX;

		static std::string WORLD_MATRIX;

		float Raw[16];

		Matrix4();

		Matrix4(const float raw[]);

		Matrix4(const float raw[], const int offset);

		Matrix4(std::initializer_list<float> raw);

		Matrix4(const Matrix4 &other);

		void Identity();
		
		void Translate(Vector4 position);

		void AppendTranslation(Vector4 position);

		void PrependTranslation(Vector4 position);

		void Rotate(Quaternion rotation);

		void AppendRotation(Quaternion rotation);

		void PrependRotation(Quaternion rotation);
		
		void Scale(Vector4 scale);

		void AppendScale(Vector4 scale);

		void PrependScale(Vector4 scale);

		void PerspectiveFov(float fov, float ratio, float near, float far);

		void PerspectiveOffCenter(float left, float right, float bottom, float top, float near, float far);

		void OrthoOffCenter(float left, float right, float bottom, float top, float near, float far);

		// NDC depth convention for the projection builders above: GL maps z to
		// [-1,1], Metal/D3D/Vulkan clip z to [0,1] so they need the [0,1] form.
		// Set once from the active RHI device's capabilities at InitGraphics.
		static void SetDepthZeroToOne(bool enabled);

		static bool GetDepthZeroToOne();

		// Copy remapped from GL's [-1,1] NDC z to [0,1] (clip'.z = (clip.z + clip.w)/2).
		Matrix4 DepthZeroToOneRemap() const;

		// Projection in the active backend's NDC depth convention. GL keeps
		// [-1,1]; Metal/D3D/Vulkan clip z to [0,1] and need the remapped form.
		static Matrix4 DepthConvention(const Matrix4 &m);

		void LookAt(Vector4 eye, Vector4 at, Vector4 up);

		Matrix4 Transpose() const;

		Vector4 Multiply(Vector4 data) const;

		Quaternion Multiply(Quaternion data) const;

		BoxBounds Multiply(const BoxBounds &data) const;

		Plane Multiply(const Plane &data) const;

		// No projection term
		Matrix4 Inverse() const;

		Matrix4 Clone() const;

		bool operator == (const Matrix4 &other) const;
		
		bool operator != (const Matrix4 &other) const;

		Matrix4 &operator = (const Matrix4 &other);

		Matrix4 operator * (const Matrix4 &other) const;
	};

}

#endif // _FURY_MATRIX4_H_