#ifndef _FURY_TRANSFORM_H_
#define _FURY_TRANSFORM_H_

#include "Fury/Quaternion.h"
#include "Fury/Matrix4.h"
#include "Fury/Vector4.h"
#include "Fury/Component.h"

namespace fury
{	
	class FURY_API Transform : public Component
	{
	public:

		typedef std::shared_ptr<Transform> Ptr;

		static Ptr Create();

		static Ptr Create(Vector4 position, Quaternion rotation, Vector4 scale);

	protected:

		Vector4 m_PrePosition, m_Position, m_PostPosition, m_WorldPosition;

		Quaternion m_PreRotation, m_Rotation, m_PostRotation, m_WorldRotation;

		Vector4 m_PreScale, m_Scale, m_PostScale, m_WorldScale;

		Matrix4 m_Matrix;

		float m_Dt = 0.0f;
		
		bool m_Dirty = true;

	public:

		Transform();

		Transform(Vector4 position, Quaternion rotation, Vector4 scale);

		Component::Ptr Clone() const override;

		float GetDeltaTime() const;

		// this'll recalculate output matrices.
		void SetDeltaTime(float dt);

		// sync pre and post transforms.
		void SyncTransforms();

		void SyncTransforms(const std::shared_ptr<SceneNode> &sceneNode);

		void SetPreTransforms(Vector4 position, Quaternion rotation, Vector4 scale);

		void SetPostTransforms(Vector4 position, Quaternion rotation, Vector4 scale);

		// set 'initial' position
		void SetPrePosition(Vector4 position);

		Vector4 GetPrePosition() const;

		// set 'target' position
		void SetPostPosition(Vector4 position);

		Vector4 GetPostPosition() const;

		// set both 'initial' and 'target' position.
		void SetPosition(Vector4 position);

		// returns current position
		Vector4 GetPosition() const;

		// set 'initial' rotation
		void SetPreRotation(Quaternion rotation);

		Quaternion GetPreRotation() const;

		// set 'target' rotation
		void SetPostRotation(Quaternion rotation);

		Quaternion GetPostRotation() const;

		// set both 'initial' and 'target' rotation.
		void SetRotation(Quaternion rotation);

		// get current rotation
		Quaternion GetRotation() const;

		// set 'initial' scale
		void SetPreScale(Vector4 scale);

		Vector4 GetPreScale() const;

		// set 'target' scale
		void SetPostScale(Vector4 scale);

		Vector4 GetPostScale() const;

		// set both 'initial' and 'target' scale.
		void SetScale(Vector4 scale);

		// get current scale
		Vector4 GetScale() const;

		Matrix4 GetMatrix() const;

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;
	};

}

#endif // _FURY_TRANSFORM_H_