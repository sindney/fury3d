#include "Fury/CharacterController.h"

#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNINGS

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "Fury/AnimationPlayer.h"
#include "Fury/AnimationState.h"
#include "Fury/Engine.h"
#include "Fury/InputUtil.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

using namespace fury;

namespace
{
	// World to local TRS write-back (scaled-ancestor trap: always via the
	// parent-world matrix inverse + Decompose). Keeps the node's authored
	// local scale.
	void WriteNodeWorldTRS(const std::shared_ptr<SceneNode> &node, const Vector4 &worldPos, const Quaternion &worldRot)
	{
		Matrix4 world;
		world.Identity();
		world.AppendTranslation(worldPos);
		world.AppendRotation(worldRot);

		Matrix4 parentWorld;
		parentWorld.Identity();
		if (auto parent = node->GetParent())
			parentWorld = parent->GetWorldMatrix();

		const Matrix4 local = parentWorld.Inverse() * world;

		Vector4 localPos, ignoredScale;
		Quaternion localRot;
		MathUtil::Decompose(local, localPos, localRot, ignoredScale);

		node->SetLocalPosition(localPos);
		node->SetLocalRoattion(localRot);
		node->Recompose(false);
	}

	// Orbit-frame forward from yaw/pitch (Editor.lua camera_basis convention).
	Vector4 OrbitForward(float yaw, float pitch)
	{
		const float cy = std::cos(yaw), sy = std::sin(yaw);
		const float cp = std::cos(pitch), sp = std::sin(pitch);
		return Vector4(-cp * sy, sp, -cp * cy, 0.0f);
	}
}

struct CharacterController::JoltCharacter
{
	JPH::CharacterVirtual *m_Character = nullptr;
	JPH::RefConst<JPH::Shape> m_Shape;

	~JoltCharacter()
	{
		delete m_Character;
	}
};

CharacterController::Ptr CharacterController::Create()
{
	return std::make_shared<CharacterController>();
}

CharacterController::CharacterController()
{
	m_TypeIndex = typeid(CharacterController);
}

CharacterController::~CharacterController()
{
	DestroyCharacter();
}

Component::Ptr CharacterController::Clone() const
{
	auto clone = CharacterController::Create();
	clone->m_CameraNodeName = m_CameraNodeName;
	clone->m_Enabled = m_Enabled;
	clone->m_Height = m_Height;
	clone->m_Radius = m_Radius;
	clone->m_WalkSpeed = m_WalkSpeed;
	clone->m_RunSpeed = m_RunSpeed;
	clone->m_JumpSpeed = m_JumpSpeed;
	clone->m_CameraDistance = m_CameraDistance;
	clone->m_CameraHeight = m_CameraHeight;
	clone->m_ModelYawOffset = m_ModelYawOffset;
	clone->m_IdleClip = m_IdleClip;
	clone->m_WalkClip = m_WalkClip;
	clone->m_RunClip = m_RunClip;
	clone->m_JumpClip = m_JumpClip;
	return clone;
}

bool CharacterController::Load(const void* wrapper, bool object)
{
	if (object && !IsObject(wrapper))
	{
		FURYE << "CharacterController: json node is not an object!";
		return false;
	}

	std::string str;
	if (!LoadMemberValue(wrapper, "type", str) || str != "CharacterController")
	{
		FURYE << "CharacterController: invalid type " << str << "!";
		return false;
	}

	LoadFields(wrapper);
	LoadMemberValue(wrapper, "height", m_Height);
	LoadMemberValue(wrapper, "radius", m_Radius);
	LoadMemberValue(wrapper, "walk_speed", m_WalkSpeed);
	LoadMemberValue(wrapper, "run_speed", m_RunSpeed);
	LoadMemberValue(wrapper, "jump_speed", m_JumpSpeed);
	LoadMemberValue(wrapper, "camera_distance", m_CameraDistance);
	LoadMemberValue(wrapper, "camera_height", m_CameraHeight);
	LoadMemberValue(wrapper, "model_yaw_offset", m_ModelYawOffset);
	LoadMemberValue(wrapper, "idle_clip", m_IdleClip);
	LoadMemberValue(wrapper, "walk_clip", m_WalkClip);
	LoadMemberValue(wrapper, "run_clip", m_RunClip);
	LoadMemberValue(wrapper, "jump_clip", m_JumpClip);
	return true;
}

void CharacterController::Save(void* wrapper, bool object)
{
	if (object)
		StartObject(wrapper);

	SaveKey(wrapper, "type");
	SaveValue(wrapper, "CharacterController");

	SaveFields(wrapper);

	SaveKey(wrapper, "height");
	SaveValue(wrapper, m_Height);
	SaveKey(wrapper, "radius");
	SaveValue(wrapper, m_Radius);
	SaveKey(wrapper, "walk_speed");
	SaveValue(wrapper, m_WalkSpeed);
	SaveKey(wrapper, "run_speed");
	SaveValue(wrapper, m_RunSpeed);
	SaveKey(wrapper, "jump_speed");
	SaveValue(wrapper, m_JumpSpeed);
	SaveKey(wrapper, "camera_distance");
	SaveValue(wrapper, m_CameraDistance);
	SaveKey(wrapper, "camera_height");
	SaveValue(wrapper, m_CameraHeight);
	SaveKey(wrapper, "model_yaw_offset");
	SaveValue(wrapper, m_ModelYawOffset);
	SaveKey(wrapper, "idle_clip");
	SaveValue(wrapper, m_IdleClip);
	SaveKey(wrapper, "walk_clip");
	SaveValue(wrapper, m_WalkClip);
	SaveKey(wrapper, "run_clip");
	SaveValue(wrapper, m_RunClip);
	SaveKey(wrapper, "jump_clip");
	SaveValue(wrapper, m_JumpClip);

	if (object)
		EndObject(wrapper);
}

void CharacterController::OnAttaching(const std::shared_ptr<SceneNode> &node)
{
	PlayerController::OnAttaching(node);

	if (!PhysicsWorld::Exists())
		return;

	auto self = std::static_pointer_cast<CharacterController>(node->GetComponent(typeid(CharacterController)));
	if (self)
		PhysicsWorld::Instance()->RegisterCharacter(self);
}

void CharacterController::OnDetaching(const std::shared_ptr<SceneNode> &node)
{
	PlayerController::OnDetaching(node);

	if (!PhysicsWorld::Exists())
		return;

	if (auto self = std::static_pointer_cast<CharacterController>(node->GetComponent(typeid(CharacterController))))
		PhysicsWorld::Instance()->UnregisterCharacter(self);
	else
		DestroyCharacter();
}

void CharacterController::OnOwnerDestructing(SceneNode &node)
{
	DestroyCharacter();
}

void CharacterController::Activate()
{
	PlayerController::Activate();

	if (!m_Active)
		return;

	// Seed the orbit yaw from the node's current facing so the camera
	// starts behind the player.
	if (auto node = m_Owner.lock())
	{
		const Vector4 euler = MathUtil::QuatToEulerRad(node->GetLocalRoattion());
		m_Yaw = euler.x;
		m_CurrentYaw = euler.x;
		m_Pitch = -0.35f;
		m_CurrentClip.clear();
	}
}

void CharacterController::Deactivate()
{
	PlayerController::Deactivate();
}

void CharacterController::AutoFitFromNode()
{
	auto node = m_Owner.lock();
	if (!node)
		return;

	// World AABB includes descendants (the model child), which is what the
	// capsule wraps. Capsule base sits at the node origin.
	const BoxBounds aabb = node->GetWorldAABB();
	const Vector4 size = aabb.GetMax() - aabb.GetMin();
	if (size.y > 1.0f)
	{
		m_Height = size.y * 0.9f;
		m_Radius = std::max(size.x, size.z) * 0.5f;
		// Capsule needs height >= 2*radius; clamp the radius if the model
		// is long (the fox is ~2x longer than tall).
		const float maxRadius = m_Height * 0.45f;
		if (m_Radius > maxRadius)
			m_Radius = maxRadius;
		if (m_Radius < 1.0f)
			m_Radius = 1.0f;
	}
}

void CharacterController::CreateCharacter()
{
	if (m_Jolt)
		return;

	if (!PhysicsWorld::Exists() || !PhysicsWorld::Instance()->IsSimulationEnabled())
		return;

	auto node = m_Owner.lock();
	if (!node)
		return;

	JPH::PhysicsSystem *system = PhysicsWorld::Instance()->GetSystem();
	if (system == nullptr)
		return;

	// Total height includes both caps; cylinder part is what's left.
	const float cylHalfHeight = std::max(0.05f, (m_Height - 2.0f * m_Radius) * 0.5f);
	const float radius = std::max(0.05f, m_Radius);

	m_Jolt = std::make_unique<JoltCharacter>();
	m_Jolt->m_Shape = JPH::CapsuleShapeSettings(cylHalfHeight, radius).Create().Get();

	// Centimeter-tuned versions of Jolt's meter defaults (design D2).
	JPH::CharacterVirtualSettings settings;
	settings.mMass = 70.0f;
	settings.mMaxStrength = 100.0f * 100.0f;
	settings.mPredictiveContactDistance = 0.1f * 100.0f;
	settings.mCharacterPadding = 0.02f * 100.0f;
	settings.mCollisionTolerance = 1.0e-3f * 100.0f;
	settings.mShape = m_Jolt->m_Shape;
	settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;

	// Node origin = capsule base (feet). Characters spawn standing.
	Vector4 worldPos, worldScale;
	Quaternion worldRot;
	MathUtil::Decompose(node->GetWorldMatrix(), worldPos, worldRot, worldScale);

	m_Jolt->m_Character = new JPH::CharacterVirtual(&settings,
		JPH::RVec3(worldPos.x, worldPos.y, worldPos.z),
		JPH::Quat::sIdentity(), 0, system);

	m_PrevPos = m_CurrPos = worldPos;
}

void CharacterController::DestroyCharacter()
{
	m_Jolt.reset();
}

void CharacterController::PhysicsTick(float fixedDt)
{
	if (!m_Active || !m_Jolt || m_Jolt->m_Character == nullptr)
		return;

	auto &input = InputUtil::Instance();
	const bool focused = input->GetWindowFocused();

	// Orbit from mouse drag (same feel as the fly camera).
	if (focused && input->GetMouseDown(sf::Mouse::Button::Left))
	{
		const auto [mx, my] = input->GetMousePosition();
		if (m_Dragging)
		{
			m_Yaw -= static_cast<float>(mx - m_LastMouseX) * 0.004f;
			m_Pitch -= static_cast<float>(my - m_LastMouseY) * 0.004f;
			const float limit = MathUtil::DegreeToRadian(80.0f);
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

	// Camera-relative planar input.
	const float cy = std::cos(m_Yaw), sy = std::sin(m_Yaw);
	const Vector4 fwd(-sy, 0.0f, -cy, 0.0f);
	const Vector4 rgt(cy, 0.0f, -sy, 0.0f);

	Vector4 move(0.0f, 0.0f, 0.0f, 0.0f);
	bool jump = false;
	if (focused)
	{
		if (input->GetKeyDown(sf::Keyboard::Key::W) || input->GetKeyDown(sf::Keyboard::Key::Up)) move = move + fwd;
		if (input->GetKeyDown(sf::Keyboard::Key::S) || input->GetKeyDown(sf::Keyboard::Key::Down)) move = move - fwd;
		if (input->GetKeyDown(sf::Keyboard::Key::A) || input->GetKeyDown(sf::Keyboard::Key::Left)) move = move - rgt;
		if (input->GetKeyDown(sf::Keyboard::Key::D) || input->GetKeyDown(sf::Keyboard::Key::Right)) move = move + rgt;
		jump = input->GetKeyDown(sf::Keyboard::Key::Space);
	}

	const float moveLen = move.Length();
	const float speed = input->GetKeyDown(sf::Keyboard::Key::LShift) ? m_RunSpeed : m_WalkSpeed;

	JPH::CharacterVirtual *character = m_Jolt->m_Character;
	const Vector4 gravity = PhysicsWorld::Instance()->GetGravity();

	m_Grounded = character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
	m_AirTime = m_Grounded ? 0.0f : m_AirTime + fixedDt;

	JPH::Vec3 velocity = character->GetLinearVelocity();
	float vy = velocity.GetY();
	if (m_Grounded)
		vy = jump ? m_JumpSpeed : -10.0f; // small stick-to-ground
	else
		vy += gravity.y * fixedDt;

	JPH::Vec3 newVelocity(0.0f, vy, 0.0f);
	if (moveLen > 0.0f)
	{
		const float inv = speed / moveLen;
		newVelocity.SetX(move.x * inv);
		newVelocity.SetZ(move.z * inv);
	}
	character->SetLinearVelocity(newVelocity);

	// Gravity is also passed for slope/stair behavior inside ExtendedUpdate.
	JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
	character->ExtendedUpdate(fixedDt,
		JPH::Vec3(gravity.x, gravity.y, gravity.z), updateSettings,
		JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(),
		JPH::BodyFilter(), JPH::ShapeFilter(),
		*PhysicsWorld::Instance()->GetTempAllocator());

	m_PrevPos = m_CurrPos;
	const JPH::RVec3 pos = character->GetPosition();
	m_CurrPos = Vector4(static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()),
		static_cast<float>(pos.GetZ()), 1.0f);
}

void CharacterController::SyncFromPhysics(float alpha, float dt)
{
	auto node = m_Owner.lock();
	if (!m_Jolt || m_Jolt->m_Character == nullptr || !node)
		return;

	const Vector4 pos = m_PrevPos + (m_CurrPos - m_PrevPos) * alpha;

	// Face the planar velocity - smoothed: approach the target yaw at a
	// capped rate (shortest arc) instead of snapping to it.
	const JPH::Vec3 velocity = m_Jolt->m_Character->GetLinearVelocity();
	const float planarSpeed = std::sqrt(velocity.GetX() * velocity.GetX() + velocity.GetZ() * velocity.GetZ());

	if (planarSpeed > 1.0f)
	{
		const float targetYaw = std::atan2(-velocity.GetX(), -velocity.GetZ());
		float delta = targetYaw - m_CurrentYaw;
		// Wrap to [-PI, PI] so the turn takes the short way around.
		while (delta > MathUtil::PI) delta -= 2.0f * MathUtil::PI;
		while (delta < -MathUtil::PI) delta += 2.0f * MathUtil::PI;
		const float maxStep = m_TurnSpeed * dt;
		m_CurrentYaw += std::clamp(delta, -maxStep, maxStep);
	}

	const Quaternion nodeRot = MathUtil::EulerRadToQuat(
		m_CurrentYaw + MathUtil::DegreeToRadian(m_ModelYawOffset), 0.0f, 0.0f);

	WriteNodeWorldTRS(node, pos, nodeRot);

	// Third-person boom on the bound camera (world space, then written
	// through the parent inverse like any other node).
	if (auto cameraNode = ResolveCameraNode())
	{
		const Vector4 target = pos + Vector4(0.0f, m_CameraHeight, 0.0f, 0.0f);
		const Vector4 fwd = OrbitForward(m_Yaw, m_Pitch);
		const Vector4 camPos = target - fwd * m_CameraDistance;

		Vector4 dir = target - camPos;
		dir = dir.Normalized();
		const float camYaw = std::atan2(-dir.x, -dir.z);
		const float camPitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));

		WriteNodeWorldTRS(cameraNode, camPos, MathUtil::EulerRadToQuat(camYaw, camPitch, 0.0f));
	}

	// Locomotion clips from planar speed (idle / walk / run bands).
	std::shared_ptr<Animator> animator = node->GetComponent<Animator>();
	if (!animator)
	{
		for (unsigned int i = 0; i < node->GetChildCount(); ++i)
		{
			if ((animator = node->GetChildAt(i)->GetComponent<Animator>()))
				break;
		}
	}
	if (animator)
	{
		const float runThreshold = (m_WalkSpeed + m_RunSpeed) * 0.5f;
		const char *clip = m_IdleClip.c_str();
		if (!m_JumpClip.empty() && m_AirTime > 0.2f)
			clip = m_JumpClip.c_str();
		else if (planarSpeed > runThreshold)
			clip = m_RunClip.c_str();
		else if (planarSpeed > 10.0f)
			clip = m_WalkClip.c_str();

		if (m_CurrentClip != clip)
		{
			if (m_CurrentClip.empty() && planarSpeed <= 10.0f)
			{
				animator->Play(clip);
			}
			else
			{
				animator->CrossFade(clip, 0.2f);
			}
			if (auto state = animator->GetState(clip))
				state->SetWrapMode(AnimWrapMode::Loop);
			m_CurrentClip = clip;
		}
	}
}
