#ifndef _FURY_CAMERA_H_
#define _FURY_CAMERA_H_

#include <array>

#include "Fury/BoxBounds.h"
#include "Fury/Component.h"
#include "Fury/Matrix4.h"
#include "Fury/Frustum.h"

namespace fury
{
	class SceneNode;

	// Coordinate system & units (engine-wide convention -- see
	// docs/ARCHITECTURE.md sec Coordinate System & Units):
	//   * Handedness: right-handed.
	//   * Up axis: +Y.
	//   * Forward: -Z (camera looks down -Z; glTF convention).
	//   * Unit: 1 world unit = 1 centimeter. glTF is unitless but the
	//     Khronos sample assets (e.g. Fox) are authored in cm, so the
	//     engine adopts cm to match. Camera near/far, light radii,
	//     shadow bounds, and move speeds are all in cm. When importing
	//     assets authored in metres (1 unit = 1 m), apply a x100 scale
	//     at import; FBX2glTF's cm->m models already carry a 100x node
	//     scale.
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

		// Persists projection params + shadow settings.
		bool Load(const void* wrapper, bool object = true) override;

		void Save(void* wrapper, bool object = true) override;

		void PerspectiveFov(float fov, float ratio, float near, float far);

		// Update the aspect ratio of a perspective projection, keeping the
		// frustum's world transform (PerspectiveFov resets it). No-op for ortho.
		void SetAspect(float ratio);

		void PerspectiveOffCenter(float left, float right, float bottom, float top, float near, float far);

		void OrthoOffCenter(float left, float right, float bottom, float top, float near, float far);

		Matrix4 GetProjectionMatrix() const;
		
		Matrix4 GetProjectionMatrix(float near, float far) const;

		Frustum GetFrustum() const;

		Frustum GetFrustum(float near, float far) const;

		float GetNear() const;

		float GetFar() const;

		// Vertical FOV in radians for a perspective camera. Returns 0 for
		// orthographic. Used by the editor to rebuild the projection with
		// a new aspect ratio (viewport resize) while preserving FOV.
		float GetFov() const;

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
