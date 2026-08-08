#include "Fury/PlayerController.h"

#include <algorithm>

#include "Fury/Camera.h"
#include "Fury/CharacterController.h"
#include "Fury/Engine.h"
#include "Fury/InputUtil.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/Pipeline.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

using namespace fury;

PlayerController::PlayerController()
{
}

PlayerController::~PlayerController()
{
}

bool PlayerController::LoadFields(const void* wrapper)
{
	LoadMemberValue(wrapper, "camera_node", m_CameraNodeName);
	LoadMemberValue(wrapper, "enabled", m_Enabled);
	return true;
}

void PlayerController::SaveFields(void* wrapper)
{
	SaveKey(wrapper, "camera_node");
	SaveValue(wrapper, m_CameraNodeName);

	SaveKey(wrapper, "enabled");
	SaveValue(wrapper, m_Enabled);
}

std::shared_ptr<SceneNode> PlayerController::ResolveCameraNode() const
{
	if (m_CameraNodeName.empty() || Scene::Active == nullptr || Scene::Active->GetRootNode() == nullptr)
		return nullptr;

	return Scene::Active->GetRootNode()->FindChildRecursively(m_CameraNodeName);
}

std::shared_ptr<SceneNode> PlayerController::GetDrivenNode() const
{
	if (auto cameraNode = ResolveCameraNode())
		return cameraNode;
	return m_Owner.lock();
}

namespace
{
	void CollectControllers(const std::shared_ptr<SceneNode> &node,
		std::vector<std::shared_ptr<PlayerController>> &out)
	{
		if (auto freefly = node->GetComponent<FreeFlyController>())
			out.push_back(freefly);
		if (auto character = node->GetComponent(typeid(CharacterController)))
			out.push_back(std::static_pointer_cast<PlayerController>(character));

		for (unsigned int i = 0; i < node->GetChildCount(); ++i)
			CollectControllers(node->GetChildAt(i), out);
	}
}

std::shared_ptr<PlayerController> PlayerController::FindFirstEnabled(const std::shared_ptr<SceneNode> &root)
{
	if (root == nullptr)
		return nullptr;

	std::vector<std::shared_ptr<PlayerController>> all;
	CollectControllers(root, all);

	for (const auto &controller : all)
		if (controller->IsEnabled())
			return controller;

	return nullptr;
}

std::shared_ptr<PlayerController> PlayerController::ActivateFirst(const std::shared_ptr<SceneNode> &root)
{
	if (root == nullptr)
		return nullptr;

	std::vector<std::shared_ptr<PlayerController>> all;
	CollectControllers(root, all);

	std::shared_ptr<PlayerController> first;
	for (const auto &controller : all)
	{
		if (!controller->IsEnabled())
			continue;

		if (first == nullptr)
		{
			first = controller;
		}
		else
		{
			FURYW << "PlayerController: ignoring extra enabled controller on node '"
				<< (controller->GetOwner() ? controller->GetOwner()->GetName() : "?") << "'.";
		}
	}

	if (first)
		first->Activate();

	return first;
}

void PlayerController::Activate()
{
	if (m_Active)
		return;
	m_Active = true;

	auto cameraNode = ResolveCameraNode();
	if (!cameraNode && m_CameraNodeName.empty())
	{
		// Unbound: the controller's own node may carry the Camera
		// (the freefly fallback layout puts both on one node).
		if (auto owner = m_Owner.lock())
			if (owner->GetComponent<Camera>())
				cameraNode = owner;
	}

	if (cameraNode)
	{
		if (Pipeline::Active)
			Pipeline::Active->SetCurrentCamera(cameraNode);
	}
	else if (!m_CameraNodeName.empty())
	{
		FURYW << "PlayerController: camera node '" << m_CameraNodeName << "' not found.";
	}
}

void PlayerController::Deactivate()
{
	m_Active = false;
}

// ---------------------------------------------------------------------------
// FreeFlyController - C++ port of the Editor.lua fly camera (yaw/pitch LMB
// drag, WASD+arrows, LShift x5 boost, wheel speed in [0.5, 50]).
// ---------------------------------------------------------------------------

FreeFlyController::Ptr FreeFlyController::Create()
{
	return std::make_shared<FreeFlyController>();
}

FreeFlyController::FreeFlyController()
{
	m_TypeIndex = typeid(FreeFlyController);
}

Component::Ptr FreeFlyController::Clone() const
{
	auto clone = FreeFlyController::Create();
	clone->m_CameraNodeName = m_CameraNodeName;
	clone->m_Enabled = m_Enabled;
	clone->m_MoveSpeed = m_MoveSpeed;
	clone->m_MouseSensitivity = m_MouseSensitivity;
	return clone;
}

bool FreeFlyController::Load(const void* wrapper, bool object)
{
	if (object && !IsObject(wrapper))
	{
		FURYE << "FreeFlyController: json node is not an object!";
		return false;
	}

	std::string str;
	if (!LoadMemberValue(wrapper, "type", str) || str != "FreeFlyController")
	{
		FURYE << "FreeFlyController: invalid type " << str << "!";
		return false;
	}

	LoadFields(wrapper);
	LoadMemberValue(wrapper, "move_speed", m_MoveSpeed);
	LoadMemberValue(wrapper, "mouse_sensitivity", m_MouseSensitivity);
	return true;
}

void FreeFlyController::Save(void* wrapper, bool object)
{
	if (object)
		StartObject(wrapper);

	SaveKey(wrapper, "type");
	SaveValue(wrapper, "FreeFlyController");

	SaveFields(wrapper);

	SaveKey(wrapper, "move_speed");
	SaveValue(wrapper, m_MoveSpeed);

	SaveKey(wrapper, "mouse_sensitivity");
	SaveValue(wrapper, m_MouseSensitivity);

	if (object)
		EndObject(wrapper);
}

void FreeFlyController::Activate()
{
	PlayerController::Activate();

	if (!m_Active)
		return;

	// Seed yaw/pitch from the driven node's current local rotation.
	if (auto node = GetDrivenNode())
	{
		const Vector4 euler = MathUtil::QuatToEulerRad(node->GetLocalRoattion());
		m_Yaw = euler.x;
		m_Pitch = euler.y;
	}

	auto self = m_Owner.expired() ? nullptr
		: std::static_pointer_cast<FreeFlyController>(m_Owner.lock()->GetComponent(typeid(FreeFlyController)));
	if (self)
		m_UpdateKey = Engine::OnUpdate->Connect(self, &FreeFlyController::TickUpdate);
}

void FreeFlyController::Deactivate()
{
	if (m_UpdateKey != 0)
	{
		Engine::OnUpdate->Disconnect(m_UpdateKey);
		m_UpdateKey = 0;
	}
	PlayerController::Deactivate();
}

void FreeFlyController::TickUpdate(float dt)
{
	auto node = GetDrivenNode();
	if (!m_Active || node == nullptr)
		return;

	auto &input = InputUtil::Instance();
	if (!input->GetWindowFocused())
		return;

	// Mouse-drag look (Editor.lua: yaw -= dx*sens, pitch -= dy*sens).
	if (input->GetMouseDown(sf::Mouse::Button::Left))
	{
		const auto [mx, my] = input->GetMousePosition();
		if (m_Dragging)
		{
			m_Yaw -= static_cast<float>(mx - m_LastMouseX) * m_MouseSensitivity;
			m_Pitch -= static_cast<float>(my - m_LastMouseY) * m_MouseSensitivity;
			const float limit = MathUtil::DegreeToRadian(89.0f);
			m_Pitch = std::clamp(m_Pitch, -limit, limit);
		}
		m_LastMouseX = mx;
		m_LastMouseY = my;
		m_Dragging = true;
	}
	else
	{
		m_Dragging = false;
	}

	// WASD / arrows in the camera basis (Editor.lua camera_basis()).
	const float cy = std::cos(m_Yaw), sy = std::sin(m_Yaw);
	const float cp = std::cos(m_Pitch), sp = std::sin(m_Pitch);
	const Vector4 fwd(-cp * sy, sp, -cp * cy, 0.0f);
	const Vector4 rgt(cy, 0.0f, -sy, 0.0f);

	Vector4 move(0.0f, 0.0f, 0.0f, 0.0f);
	if (input->GetKeyDown(sf::Keyboard::Key::W) || input->GetKeyDown(sf::Keyboard::Key::Up)) move = move + fwd;
	if (input->GetKeyDown(sf::Keyboard::Key::S) || input->GetKeyDown(sf::Keyboard::Key::Down)) move = move - fwd;
	if (input->GetKeyDown(sf::Keyboard::Key::A) || input->GetKeyDown(sf::Keyboard::Key::Left)) move = move - rgt;
	if (input->GetKeyDown(sf::Keyboard::Key::D) || input->GetKeyDown(sf::Keyboard::Key::Right)) move = move + rgt;

	const float moveLen = move.Length();
	Vector4 pos = node->GetLocalPosition();
	if (moveLen > 0.0f)
	{
		const float boost = input->GetKeyDown(sf::Keyboard::Key::LShift) ? 5.0f : 1.0f;
		pos = pos + move * (m_MoveSpeed * boost * dt / moveLen);
	}

	// Wheel adjusts base speed, clamped like the editor's.
	const float wheel = input->GetMouseWheel();
	if (wheel != 0.0f)
		m_MoveSpeed = std::clamp(m_MoveSpeed + wheel, 0.5f, 50.0f);

	node->SetLocalPosition(pos);
	node->SetLocalRoattion(MathUtil::EulerRadToQuat(m_Yaw, m_Pitch, 0.0f));
	node->Recompose(false);
}
