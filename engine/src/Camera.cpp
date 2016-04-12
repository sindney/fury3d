#include "Camera.h"
#include "Plane.h"
#include "SceneNode.h"

namespace fury
{
	Camera::Ptr Camera::Create()
	{
		return std::make_shared<Camera>();
	}

	Camera::Camera() : m_Perspective(false)
	{
		m_TypeIndex = typeid(Camera);
	}

	Component::Ptr Camera::Clone() const
	{
		auto ptr = Camera::Create();
		ptr->m_Far = m_Far;
		ptr->m_Near = m_Near;
		ptr->m_Perspective = m_Perspective;
		ptr->m_ProjectionMatrix = m_ProjectionMatrix;
		ptr->m_Frustum = m_Frustum;
		return ptr;
	}

	void Camera::PerspectiveFov(float fov, float ratio, float near, float far)
	{
		m_Far = far;
		m_Near = near;
		m_Perspective = true;
		
		m_ProjectionMatrix.PerspectiveFov(fov, ratio, near, far);
		m_Frustum.Setup(fov, ratio, near, far);
	}

	void Camera::PerspectiveOffCenter(float left, float right, float bottom, float top, float near, float far)
	{
		m_Far = far;
		m_Near = near;
		m_Perspective = true;

		m_ProjectionMatrix.PerspectiveOffCenter(left, right, bottom, top, near, far);
		m_Frustum.Setup(left, right, bottom, top, near, far);
	}

	void Camera::OrthoOffCenter(float left, float right, float bottom, float top, float near, float far)
	{
		m_Far = far;
		m_Near = near;
		m_Perspective = true;

		m_ProjectionMatrix.OrthoOffCenter(left, right, bottom, top, near, far);
		m_Frustum.Setup(left, right, bottom, top, near, far);
	}

	Matrix4 Camera::GetProjectionMatrix() const
	{
		return m_ProjectionMatrix;
	}

	Frustum Camera::GetFrustum() const
	{
		return m_Frustum;
	}

	float Camera::GetNear() const
	{
		return m_Near;
	}

	float Camera::GetFar() const
	{
		return m_Far;
	}

	bool Camera::IsPerspective() const
	{
		return m_Perspective;
	}

	void Camera::Transform(const Matrix4 &matrix)
	{
		m_Frustum.Transform(matrix);
	}

	bool Camera::IsVisible(const BoxBounds &aabb) const
	{
		return m_Frustum.IsInsideFast(aabb);
	}

	bool Camera::IsVisible(const SphereBounds &bsphere) const
	{
		return m_Frustum.IsInsideFast(bsphere);
	}

	bool Camera::IsVisible(Vector4 point) const
	{
		return m_Frustum.IsInsideFast(point);
	}

	void Camera::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		Camera::Ptr selfPtr = shared_from_this();
		m_SignalKey = node->OnTransformChange->Connect(selfPtr, &Camera::OnSceneNodeTransformChange);
	}

	void Camera::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnDetaching(node);
		node->OnTransformChange->Disconnect(m_SignalKey);
		m_SignalKey = 0;
	}

	void Camera::OnSceneNodeTransformChange(const std::shared_ptr<SceneNode> &sender)
	{
		Transform(sender->GetWorldMatrix());
	}

}