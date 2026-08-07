#include "Fury/Camera.h"
#include "Fury/Log.h"
#include "Fury/Plane.h"
#include "Fury/SceneNode.h"

#include <cmath>

namespace fury
{
	Camera::Ptr Camera::Create()
	{
		return std::make_shared<Camera>();
	}

	Camera::Camera() : m_Perspective(false)
	{
		m_TypeIndex = typeid(Camera);
		m_ShadowAABB = BoxBounds(Vector4(0), Vector4(0));
	}

	Component::Ptr Camera::Clone() const
	{
		auto ptr = Camera::Create();
		ptr->m_ProjectionParams = m_ProjectionParams;
		ptr->m_Perspective = m_Perspective;
		ptr->m_ProjectionMatrix = m_ProjectionMatrix;
		ptr->m_Frustum = m_Frustum;
		ptr->m_ShadowAABB = m_ShadowAABB;
		ptr->m_ShadowFar = m_ShadowFar;
		return ptr;
	}

	bool Camera::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Camera: json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "Camera")
		{
			FURYE << "Camera: invalid type " << str << "!";
			return false;
		}

		LoadMemberValue(wrapper, "perspective", m_Perspective);

		// left, right, bottom, top, near, far - then rebuild the
		// projection through the OffCenter setters (keeps matrix +
		// frustum consistent).
		float p[6] = { 0, 0, 0, 0, 0, 0 };
		bool haveParams = false;
		const char *keys[6] = { "left", "right", "bottom", "top", "near", "far" };
		for (int i = 0; i < 6; ++i)
			haveParams = LoadMemberValue(wrapper, keys[i], p[i]) || haveParams;

		if (haveParams)
		{
			if (m_Perspective)
				PerspectiveOffCenter(p[0], p[1], p[2], p[3], p[4], p[5]);
			else
				OrthoOffCenter(p[0], p[1], p[2], p[3], p[4], p[5]);
		}

		LoadMemberValue(wrapper, "shadow_far", m_ShadowFar);

		Vector4 bmin, bmax;
		if (LoadMemberValue(wrapper, "shadow_min", bmin) && LoadMemberValue(wrapper, "shadow_max", bmax))
			SetShadowBounds(bmin, bmax);

		return true;
	}

	void Camera::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		SaveKey(wrapper, "type");
		SaveValue(wrapper, "Camera");

		SaveKey(wrapper, "perspective");
		SaveValue(wrapper, m_Perspective);

		const char *keys[6] = { "left", "right", "bottom", "top", "near", "far" };
		for (int i = 0; i < 6; ++i)
		{
			SaveKey(wrapper, keys[i]);
			SaveValue(wrapper, m_ProjectionParams[i]);
		}

		SaveKey(wrapper, "shadow_far");
		SaveValue(wrapper, m_ShadowFar);

		const BoxBounds bounds = GetShadowBounds(false);
		SaveKey(wrapper, "shadow_min");
		SaveValue(wrapper, bounds.GetMin());
		SaveKey(wrapper, "shadow_max");
		SaveValue(wrapper, bounds.GetMax());

		if (object)
			EndObject(wrapper);
	}

	void Camera::PerspectiveFov(float fov, float ratio, float near, float far)
	{
		m_Perspective = true;

		float top = near * tan(fov / 2.0f);
		float right = top * ratio;

		m_ProjectionParams[0] = -right;
		m_ProjectionParams[1] = right;
		m_ProjectionParams[2] = -top;
		m_ProjectionParams[3] = top;
		m_ProjectionParams[4] = near;
		m_ProjectionParams[5] = far;

		m_ProjectionMatrix.PerspectiveOffCenter(-right, right, -top, top, near, far);
		m_Frustum.Setup(-right, right, -top, top, near, far);
	}

	void Camera::SetAspect(float ratio)
	{
		if (!m_Perspective) return;

		// Preserve top (= tan(fov/2) * near), recompute right from the new
		// ratio. near / far / bottom are unchanged.
		const float top   = m_ProjectionParams[3];
		const float bot   = m_ProjectionParams[2];
		const float near  = m_ProjectionParams[4];
		const float far   = m_ProjectionParams[5];
		const float right = top * ratio;
		const float left  = -right;

		m_ProjectionParams[0] = left;
		m_ProjectionParams[1] = right;

		m_ProjectionMatrix.PerspectiveOffCenter(left, right, bot, top, near, far);

		// Frustum::Setup resets the world transform to identity — save it
		// here and re-apply so the camera's view transform survives the
		// aspect change. (PerspectiveFov doesn't do this because it's a
		// full re-init, typically called before the camera is attached to
		// a moving node.)
		Matrix4 savedTransform = m_Frustum.GetTransformMatrix();
		m_Frustum.Setup(left, right, bot, top, near, far);
		m_Frustum.Transform(savedTransform);
	}

	void Camera::PerspectiveOffCenter(float left, float right, float bottom, float top, float near, float far)
	{
		m_ProjectionParams[0] = left;
		m_ProjectionParams[1] = right;
		m_ProjectionParams[2] = bottom;
		m_ProjectionParams[3] = top;
		m_ProjectionParams[4] = near;
		m_ProjectionParams[5] = far;
		m_Perspective = true;

		m_ProjectionMatrix.PerspectiveOffCenter(left, right, bottom, top, near, far);
		m_Frustum.Setup(left, right, bottom, top, near, far);
	}

	void Camera::OrthoOffCenter(float left, float right, float bottom, float top, float near, float far)
	{
		m_Perspective = true;

		m_ProjectionParams[0] = left;
		m_ProjectionParams[1] = right;
		m_ProjectionParams[2] = bottom;
		m_ProjectionParams[3] = top;
		m_ProjectionParams[4] = near;
		m_ProjectionParams[5] = far;

		m_ProjectionMatrix.OrthoOffCenter(left, right, bottom, top, near, far);
		m_Frustum.Setup(left, right, bottom, top, near, far);
	}

	Matrix4 Camera::GetProjectionMatrix() const
	{
		return m_ProjectionMatrix;
	}

	Matrix4 Camera::GetProjectionMatrix(float near, float far) const
	{
		Matrix4 pm;
		if (m_Perspective)
			pm.PerspectiveOffCenter(m_ProjectionParams[0], m_ProjectionParams[1], m_ProjectionParams[2], m_ProjectionParams[3], near, far);
		else
			pm.OrthoOffCenter(m_ProjectionParams[0], m_ProjectionParams[1], m_ProjectionParams[2], m_ProjectionParams[3], near, far);
		return pm;
	}

	Frustum Camera::GetFrustum() const
	{
		return m_Frustum;
	}

	Frustum Camera::GetFrustum(float near, float far) const
	{
		Frustum clone;
		float curNear = GetNear();
		float curLeft = m_ProjectionParams[0];
		float curRight = m_ProjectionParams[1];
		float curBottom = m_ProjectionParams[2];
		float curTop = m_ProjectionParams[3];

		float toNear = near / curNear;
		float top = curTop * toNear;
		float right = curRight * toNear;
		float bottom = curBottom * toNear;
		float left = curLeft * toNear;

		clone.Setup(left, right, bottom, top, near, far);
		clone.Transform(m_Frustum.GetTransformMatrix());
		return clone;
	}

	float Camera::GetNear() const
	{
		return m_ProjectionParams[4];
	}

	float Camera::GetFar() const
	{
		return m_ProjectionParams[5];
	}

	float Camera::GetFov() const
	{
		// For a perspective camera, top = tan(fov/2) * near, so
		// fov = 2 * atan(top / near). Meaningless for orthographic.
		if (!m_Perspective || m_ProjectionParams[4] == 0.0f)
			return 0.0f;
		return 2.0f * std::atan(m_ProjectionParams[3] / m_ProjectionParams[4]);
	}

	float Camera::GetShadowFar() const
	{
		return m_ShadowFar;
	}

	void Camera::SetShadowFar(float far)
	{
		m_ShadowFar = far;
	}

	BoxBounds Camera::GetShadowBounds(bool worldSpace) const
	{
		if (worldSpace)
			return m_Frustum.GetTransformMatrix().Multiply(m_ShadowAABB);
		else
			return m_ShadowAABB;
	}

	void Camera::SetShadowBounds(Vector4 min, Vector4 max)
	{
		m_ShadowAABB.SetMinMax(min, max);
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
