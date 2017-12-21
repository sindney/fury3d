#include "Fury/Log.h"
#include "Fury/Transform.h"
#include "Fury/SceneNode.h"

namespace fury
{
	Transform::Ptr Transform::Create()
	{
		return std::make_shared<Transform>();
	}

	Transform::Ptr Transform::Create(Vector4 position, Quaternion rotation, Vector4 scale)
	{
		return std::make_shared<Transform>(position, rotation, scale);
	}

	Transform::Transform()
	{
		m_TypeIndex = typeid(Transform);
		m_PreScale = m_Scale = m_PostScale = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		SetDeltaTime(0.0f);
	}

	Transform::Transform(Vector4 position, Quaternion rotation, Vector4 scale)
	{
		m_TypeIndex = typeid(Transform);
		
		m_PrePosition = m_PostPosition = position;
		m_PreRotation = m_PostRotation = rotation;
		m_PreScale = m_PostScale = scale;

		SetDeltaTime(0.0f);
	}

	Component::Ptr Transform::Clone() const
	{
		auto ptr = Transform::Create(m_Position, m_Rotation, m_Scale);
		ptr->SetDeltaTime(m_Dt);
		return ptr;
	}

	void Transform::SetDeltaTime(float dt)
	{
		// TODO: 优化dt未改变 或 0 或 1 的情况
		if (!m_Dirty && m_Dt == dt)
			return;

		m_Dirty = false;

		m_Position = m_PrePosition + (m_PostPosition - m_PrePosition) * dt;
		m_Rotation = m_PreRotation.Slerp(m_PostRotation, dt);
		m_Scale = m_PreScale + (m_PostScale - m_PreScale) * dt;
		m_Dt = dt;

		m_Matrix.Identity();
		m_Matrix.AppendTranslation(m_Position);
		m_Matrix.AppendRotation(m_Rotation);
		m_Matrix.AppendScale(m_Scale);

		if (!m_Owner.expired())
		{
			auto node = m_Owner.lock();
			node->SetLocalPosition(m_Position);
			node->SetLocalRoattion(m_Rotation);
			node->SetLocalScale(m_Scale);
			node->Recompose(true);
		}
	}

	float Transform::GetDeltaTime() const
	{
		return m_Dt;
	}

	void Transform::SyncTransforms()
	{
		m_PrePosition = m_PostPosition;
		m_PreRotation = m_PostRotation;
		m_PreScale = m_PostScale;
		m_Dirty = true;
	}

	void Transform::SyncTransforms(const std::shared_ptr<SceneNode> &sceneNode)
	{
		m_PrePosition = m_PostPosition = sceneNode->GetLocalPosition();
		m_PreRotation = m_PostRotation = sceneNode->GetLocalRoattion();
		m_PreScale = m_PostScale = sceneNode->GetLocalScale();
		m_Dirty = true;
	}

	void Transform::SetPreTransforms(Vector4 position, Quaternion rotation, Vector4 scale)
	{
		m_PrePosition = position;
		m_PreRotation = rotation;
		m_PreScale = scale;
		m_Dirty = true;
	}

	void Transform::SetPostTransforms(Vector4 position, Quaternion rotation, Vector4 scale)
	{
		m_PostPosition = position;
		m_PostRotation = rotation;
		m_PostScale = scale;
		m_Dirty = true;
	}

	void Transform::SetPrePosition(Vector4 position)
	{
		m_PrePosition = position;
		m_Dirty = true;
	}

	Vector4 Transform::GetPrePosition() const
	{
		return m_PrePosition;
	}

	void Transform::SetPostPosition(Vector4 position)
	{
		m_PostPosition = position;
		m_Dirty = true;
	}

	Vector4 Transform::GetPostPosition() const
	{
		return m_PostPosition;
	}

	void Transform::SetPosition(Vector4 position)
	{
		m_PrePosition = m_PostPosition = position;
		m_Dirty = true;
	}

	Vector4 Transform::GetPosition() const
	{
		return m_Position;
	}

	void Transform::SetPreRotation(Quaternion rotation)
	{
		m_PreRotation = rotation;
		m_Dirty = true;
	}

	Quaternion Transform::GetPreRotation() const
	{
		return m_PreRotation;
	}

	void Transform::SetPostRotation(Quaternion rotation)
	{
		m_PostRotation = rotation;
		m_Dirty = true;
	}

	Quaternion Transform::GetPostRotation() const
	{
		return m_PostRotation;
	}

	void Transform::SetRotation(Quaternion rotation)
	{
		m_PreRotation = m_PostRotation = rotation;
		m_Dirty = true;
	}

	Quaternion Transform::GetRotation() const
	{
		return m_Rotation;
	}

	void Transform::SetPreScale(Vector4 scale)
	{
		m_PreScale = scale;
		m_Dirty = true;
	}

	Vector4 Transform::GetPreScale() const
	{
		return m_PreScale;
	}

	void Transform::SetPostScale(Vector4 scale)
	{
		m_PostScale = scale;
		m_Dirty = true;
	}

	Vector4 Transform::GetPostScale() const
	{
		return m_PostScale;
	}

	void Transform::SetScale(Vector4 scale)
	{
		m_PreScale = m_PostScale = scale;
		m_Dirty = true;
	}

	Vector4 Transform::GetScale() const
	{
		return m_Scale;
	}

	Matrix4 Transform::GetMatrix() const
	{
		return m_Matrix;
	}

	void Transform::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		SyncTransforms(node);
	}
}