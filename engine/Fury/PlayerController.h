#ifndef _FURY_PLAYER_CONTROLLER_H_
#define _FURY_PLAYER_CONTROLLER_H_

#include <string>

#include "Fury/Component.h"
#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"

namespace fury
{
	class SceneNode;

	// Base for play-mode player controllers (design D10). A controller binds
	// a camera node by name; the first enabled controller in tree order is
	// the "active" one and its camera renders the play session.
	//
	// Controllers are inert while PhysicsWorld simulation is disabled (the
	// editor never simulates). Player.lua calls ActivateFirst() after loading
	// the scene; nothing else activates controllers.
	class FURY_API PlayerController : public Component
	{
	public:

		typedef std::shared_ptr<PlayerController> Ptr;

		PlayerController();

		virtual ~PlayerController();

		virtual bool LoadFields(const void* wrapper);

		virtual void SaveFields(void* wrapper);

		bool IsEnabled() const { return m_Enabled; }
		void SetEnabled(bool enabled) { m_Enabled = enabled; }

		const std::string &GetCameraNodeName() const { return m_CameraNodeName; }
		void SetCameraNodeName(const std::string &name) { m_CameraNodeName = name; }

		bool IsActive() const { return m_Active; }

		// Finds the bound camera node (scene-root recursive name lookup).
		std::shared_ptr<SceneNode> ResolveCameraNode() const;

		// Finds the first enabled controller under root (tree order).
		static std::shared_ptr<PlayerController> FindFirstEnabled(const std::shared_ptr<SceneNode> &root);

		// Activates the first enabled controller: it claims input and its
		// bound camera becomes the pipeline's current camera. Warns once per
		// extra enabled controller. Returns the activated controller (may be
		// null when none exists - Player.lua then spawns a freefly fallback).
		static std::shared_ptr<PlayerController> ActivateFirst(const std::shared_ptr<SceneNode> &root);

		virtual void Activate();

		virtual void Deactivate();

	protected:

		// The node this controller drives with its own transform (camera node
		// when bound, else the owning node).
		std::shared_ptr<SceneNode> GetDrivenNode() const;

		std::string m_CameraNodeName;

		bool m_Enabled = true;

		bool m_Active = false;
	};

	// Editor-style fly camera: LMB-drag look, WASD/arrows translate, LShift
	// boost, wheel adjusts base speed. Direct C++ port of the Editor.lua
	// camera scheme - drives the bound camera node's LOCAL TRS (root-level
	// camera nodes recommended, like the editor's).
	class FURY_API FreeFlyController : public PlayerController
	{
	public:

		typedef std::shared_ptr<FreeFlyController> Ptr;

		static Ptr Create();

		FreeFlyController();

		virtual Component::Ptr Clone() const override;

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		virtual void Activate() override;

		virtual void Deactivate() override;

		float GetMoveSpeed() const { return m_MoveSpeed; }
		void SetMoveSpeed(float speed) { m_MoveSpeed = speed; }

		float GetMouseSensitivity() const { return m_MouseSensitivity; }
		void SetMouseSensitivity(float sens) { m_MouseSensitivity = sens; }

	private:

		void TickUpdate(float dt);

		float m_Yaw = 0.0f;

		float m_Pitch = 0.0f;

		float m_MoveSpeed = 500.0f;

		float m_MouseSensitivity = 0.004f;

		bool m_Dragging = false;

		int m_LastMouseX = 0;

		int m_LastMouseY = 0;

		size_t m_UpdateKey = 0;
	};
}

#endif // _FURY_PLAYER_CONTROLLER_H_
