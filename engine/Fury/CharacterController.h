#ifndef _FURY_CHARACTER_CONTROLLER_H_
#define _FURY_CHARACTER_CONTROLLER_H_

#include <memory>
#include <string>

#include "Fury/PlayerController.h"
#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"

namespace fury
{
	// Jolt CharacterVirtual-driven capsule player (design D10). Walk/run/jump
	// on the fixed tick; mouse-drag orbits a third-person boom camera; the
	// owning node follows the capsule and yaws toward its velocity; an
	// Animator (on the node or a child) crossfades idle/walk/run clips.
	//
	// All dimensions/speeds are in engine world units (cm). The Jolt handle
	// is pimpl'd so this header stays Jolt-free.
	class FURY_API CharacterController : public PlayerController
	{
	public:

		typedef std::shared_ptr<CharacterController> Ptr;

		static Ptr Create();

		CharacterController();

		virtual ~CharacterController();

		virtual Component::Ptr Clone() const override;

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		virtual void Activate() override;

		virtual void Deactivate() override;

		// Capsule: m_Height is the total height including caps (cm).
		float GetHeight() const { return m_Height; }
		void SetHeight(float height) { m_Height = height; }

		float GetRadius() const { return m_Radius; }
		void SetRadius(float radius) { m_Radius = radius; }

		float GetWalkSpeed() const { return m_WalkSpeed; }
		void SetWalkSpeed(float speed) { m_WalkSpeed = speed; }

		float GetRunSpeed() const { return m_RunSpeed; }
		void SetRunSpeed(float speed) { m_RunSpeed = speed; }

		float GetJumpSpeed() const { return m_JumpSpeed; }
		void SetJumpSpeed(float speed) { m_JumpSpeed = speed; }

		// Third-person boom: distance behind / height above the node origin.
		float GetCameraDistance() const { return m_CameraDistance; }
		void SetCameraDistance(float distance) { m_CameraDistance = distance; }

		float GetCameraHeight() const { return m_CameraHeight; }
		void SetCameraHeight(float height) { m_CameraHeight = height; }

		// Constant local yaw applied to the model when its authored forward
		// axis isn't -Z (degrees).
		float GetModelYawOffset() const { return m_ModelYawOffset; }
		void SetModelYawOffset(float degrees) { m_ModelYawOffset = degrees; }

		const std::string &GetIdleClip() const { return m_IdleClip; }
		void SetIdleClip(const std::string &name) { m_IdleClip = name; }

		const std::string &GetWalkClip() const { return m_WalkClip; }
		void SetWalkClip(const std::string &name) { m_WalkClip = name; }

		const std::string &GetRunClip() const { return m_RunClip; }
		void SetRunClip(const std::string &name) { m_RunClip = name; }

		// Optional: played while airborne (>0.2s off ground). Empty = off.
		const std::string &GetJumpClip() const { return m_JumpClip; }
		void SetJumpClip(const std::string &name) { m_JumpClip = name; }

		// Fits capsule height/radius from the owning node's world AABB.
		void AutoFitFromNode();

		// PhysicsWorld hooks (simulation-enabled only).
		void CreateCharacter();
		void DestroyCharacter();
		void PhysicsTick(float fixedDt);
		void SyncFromPhysics(float alpha, float dt);

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnOwnerDestructing(SceneNode &node) override;

		float m_Height = 180.0f;

		float m_Radius = 40.0f;

		float m_WalkSpeed = 200.0f;

		float m_RunSpeed = 500.0f;

		float m_JumpSpeed = 450.0f;

		float m_CameraDistance = 500.0f;

		float m_CameraHeight = 180.0f;

		float m_ModelYawOffset = 0.0f;

		std::string m_IdleClip = "Survey";

		std::string m_WalkClip = "Walk";

		std::string m_RunClip = "Run";

		std::string m_JumpClip;

		// Ground state for the jump clip + future movement logic.
		bool m_Grounded = true;

		float m_AirTime = 0.0f;

		// Orbit state (mouse-drag driven).
		float m_Yaw = 0.0f;

		float m_Pitch = -0.35f;

		bool m_Dragging = false;

		int m_LastMouseX = 0;

		int m_LastMouseY = 0;

		// Interpolation state (capsule base position, world space).
		Vector4 m_PrevPos;

		Vector4 m_CurrPos;

		// Smoothed facing: approaches the movement yaw at a capped turn
		// rate instead of snapping to it (A/D no longer spin the model
		// instantly).
		float m_CurrentYaw = 0.0f;

		// Max turn speed for facing changes (rad/s).
		float m_TurnSpeed = 12.0f;

		std::string m_CurrentClip;

		struct JoltCharacter;

		std::unique_ptr<JoltCharacter> m_Jolt;
	};
}

#endif // _FURY_CHARACTER_CONTROLLER_H_
