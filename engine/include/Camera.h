#ifndef _FURY_CAMERA_H_
#define _FURY_CAMERA_H_

#include <array>

#include "BoxBounds.h"
#include "Component.h"
#include "Matrix4.h"
#include "Frustum.h"

namespace fury
{
	class SceneNode;

	class FURY_API Camera : public Component, public std::enable_shared_from_this<Camera>
	{
	protected:

		Matrix4 m_ProjectionMatrix;

		Frustum m_Frustum;

		BoxBounds m_ShadowAABB;

		bool m_Perspective = true;

		// left, right, bottom, top, near, far
		std::array<float, 6> m_ProjectionParams;

		float m_ShadowFar = 0.0f;

		size_t m_SignalKey = 0;

	public:

		typedef std::shared_ptr<Camera> Ptr;

		static Ptr Create();

		Camera();

		Component::Ptr Clone() const override;

		void PerspectiveFov(float fov, float ratio, float near, float far);

		void PerspectiveOffCenter(float left, float right, float bottom, float top, float near, float far);

		void OrthoOffCenter(float left, float right, float bottom, float top, float near, float far);

		Matrix4 GetProjectionMatrix() const;
		
		Matrix4 GetProjectionMatrix(float near, float far) const;

		Frustum GetFrustum() const;

		Frustum GetFrustum(float near, float far) const;

		float GetNear() const;

		float GetFar() const;

		float GetShadowFar() const;

		void SetShadowFar(float far);

		BoxBounds GetShadowBounds(bool worldSpace = true) const;

		void SetShadowBounds(Vector4 min, Vector4 max);

		bool IsPerspective() const;

		// transform camera's frustum to match camera's current matrix.
		void Transform(const Matrix4 &matrix);

		// test the visiablity of an aabb.
		bool IsVisible(const BoxBounds &aabb) const;

		// test the visiablity of an bsphere.
		bool IsVisible(const SphereBounds &bsphere) const;

		// test the visiablity of a point.
		bool IsVisible(Vector4 point) const;

		// SceneNode::OnTransformChange callback.
		void OnSceneNodeTransformChange(const std::shared_ptr<SceneNode> &sender);

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;
	};
}

#endif // _FURY_CAMERA_H_
