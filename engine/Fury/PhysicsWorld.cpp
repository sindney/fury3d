#include "Fury/PhysicsWorld.h"

#include <cstdarg>
#include <cstdio>

#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNINGS

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "Fury/BodySetup.h"
#include "Fury/BuoyancyComponent.h"
#include "Fury/CharacterController.h"
#include "Fury/Engine.h"
#include "Fury/Log.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

using namespace fury;

namespace
{
	// Object layers: NON_MOVING for static level geometry, MOVING for
	// dynamic bodies and characters. Kept in lockstep with
	// PhysicsWorld's public layer constants.
	namespace Layers
	{
		static constexpr JPH::ObjectLayer NON_MOVING = 0;
		static constexpr JPH::ObjectLayer MOVING = 1;
		static constexpr JPH::ObjectLayer NUM = 2;
	}

	namespace BroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer MOVING(1);
		static constexpr JPH::uint NUM = 2;
	}

	class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:

		virtual JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM;
		}

		virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
		{
			JPH_ASSERT(inLayer < Layers::NUM);
			if (inLayer == Layers::NON_MOVING)
				return BroadPhaseLayers::NON_MOVING;
			return BroadPhaseLayers::MOVING;
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		virtual const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
		{
			return inLayer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
		}
#endif
	};

	class ObjectVsBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:

		virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
		{
			if (inLayer1 == Layers::NON_MOVING)
				return inLayer2 == BroadPhaseLayers::MOVING;
			return true;
		}
	};

	class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
	{
	public:

		virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
		{
			// Static-vs-static never collides; everything else does.
			return !(inObject1 == Layers::NON_MOVING && inObject2 == Layers::NON_MOVING);
		}
	};

	void JoltTraceImpl(const char *inFMT, ...)
	{
		va_list args;
		va_start(args, inFMT);
		char buffer[1024];
		vsnprintf(buffer, sizeof(buffer), inFMT, args);
		va_end(args);
		FURYD << "Jolt: " << buffer;
	}

#ifdef JPH_ENABLE_ASSERTS
	bool JoltAssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, JPH::uint inLine)
	{
		// Log-and-continue (return false = don't break): keeps the editor
		// alive on recoverable Jolt asserts; the message lands in Log.txt.
		FURYE << "Jolt assert: (" << (inExpression != nullptr ? inExpression : "") << ") "
			<< (inMessage != nullptr ? inMessage : "") << " " << inFile << ":" << inLine;
		return false;
	}
#endif

	// World sizing - generous for an outdoor scene, small enough to stay
	// deterministic about memory.
	constexpr unsigned int cMaxBodies = 8192;
	constexpr unsigned int cMaxBodyPairs = 65536;
	constexpr unsigned int cMaxContactConstraints = 8192;
}

PhysicsWorld::PhysicsWorld()
{
	JPH::RegisterDefaultAllocator();

	JPH::Trace = JoltTraceImpl;
#ifdef JPH_ENABLE_ASSERTS
	JPH::AssertFailed = JoltAssertFailedImpl;
#endif

	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	m_TempAllocator = new JPH::TempAllocatorImpl(16 * 1024 * 1024);

	unsigned int threads = std::thread::hardware_concurrency();
	m_JobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
		threads > 1 ? static_cast<int>(threads - 1) : 1);

	static BPLayerInterfaceImpl s_BpLayerInterface;
	static ObjectVsBPLayerFilterImpl s_ObjectVsBpFilter;
	static ObjectLayerPairFilterImpl s_ObjectPairFilter;

	m_System = new JPH::PhysicsSystem();
	m_System->Init(cMaxBodies, 0, cMaxBodyPairs, cMaxContactConstraints,
		s_BpLayerInterface, s_ObjectVsBpFilter, s_ObjectPairFilter);
	m_System->SetGravity(JPH::Vec3(m_Gravity.x, m_Gravity.y, m_Gravity.z));

	FURYD << "PhysicsWorld created (Jolt " << JPH_VERSION_MAJOR << "." << JPH_VERSION_MINOR << "."
		<< JPH_VERSION_PATCH << ", " << threads << " hw threads).";
}

PhysicsWorld::~PhysicsWorld()
{
	// No logging here (singleton teardown order vs Log). Bodies must be
	// gone by now: Engine::Shutdown resets Scene::Active first, and
	// DisableSimulation destroys the rest.
	DestroyAllBodies();

	delete m_System;
	m_System = nullptr;
	delete m_JobSystem;
	m_JobSystem = nullptr;
	delete m_TempAllocator;
	m_TempAllocator = nullptr;

	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;
}

bool PhysicsWorld::Exists()
{
	return m_Instance != nullptr;
}

void PhysicsWorld::Subscribe()
{
	m_FixedUpdateKey = Engine::OnFixedUpdate->Connect(Instance(), &PhysicsWorld::TickFixed);
	m_UpdateKey = Engine::OnUpdate->Connect(Instance(), &PhysicsWorld::TickUpdate);
}

void PhysicsWorld::SetSimulationEnabled(bool enabled)
{
	if (m_SimulationEnabled == enabled)
		return;

	m_SimulationEnabled = enabled;

	// Creation is deferred to the next TickFixed (which recomposes the
	// scene root first - world matrices are stale mid-load). Destruction
	// is immediate.
	if (m_SimulationEnabled)
	{
		DiscoverComponentsInScene();
		m_CreationDirty = true;
	}
	else
	{
		DestroyAllBodies();
	}
}

void PhysicsWorld::DiscoverComponentsInScene()
{
	if (Scene::Active == nullptr || Scene::Active->GetRootNode() == nullptr)
		return;

	std::function<void(const std::shared_ptr<SceneNode>&)> walk =
		[&](const std::shared_ptr<SceneNode> &node)
	{
		auto alreadyRegistered = [](const auto &list, const auto &component)
		{
			for (const auto &weak : list)
				if (weak.lock() == component)
					return true;
			return false;
		};

		if (auto body = node->GetComponent<BodySetup>())
			if (!alreadyRegistered(m_BodySetups, body))
				RegisterBodySetup(body);

		if (auto character = node->GetComponent<CharacterController>())
			if (!alreadyRegistered(m_Characters, character))
				RegisterCharacter(character);

		if (auto buoyancy = node->GetComponent<BuoyancyComponent>())
			if (!alreadyRegistered(m_Buoyancies, buoyancy))
				RegisterBuoyancy(buoyancy);

		for (unsigned int i = 0; i < node->GetChildCount(); ++i)
			walk(node->GetChildAt(i));
	};

	walk(Scene::Active->GetRootNode());
}

void PhysicsWorld::SetGravity(const Vector4 &gravity)
{
	m_Gravity = gravity;
	if (m_System != nullptr)
		m_System->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
}

void PhysicsWorld::Step(int n)
{
	if (!m_SimulationEnabled || m_System == nullptr)
		return;

	for (int i = 0; i < n; ++i)
		TickFixed();

	// Exact final state for test assertions (no render interpolation
	// alpha exists outside the Run loop).
	SyncNodes(1.0f, Engine::GetFixedDt());
}

void PhysicsWorld::TickFixed()
{
	if (!m_SimulationEnabled || m_System == nullptr)
		return;

	// Lazy body creation: a full root recompose guarantees valid world
	// matrices (OnAttaching fires mid-Load while AddChild hasn't run yet).
	if (m_CreationDirty)
	{
		m_CreationDirty = false;
		if (Scene::Active && Scene::Active->GetRootNode())
			Scene::Active->GetRootNode()->Recompose(true);
		CreateAllBodies();
	}

	// Characters first: their velocity intent feeds this step's contacts
	// (Jolt samples update CharacterVirtual before PhysicsSystem::Update).
	for (auto it = m_Characters.begin(); it != m_Characters.end();)
	{
		if (auto character = it->lock())
		{
			character->PhysicsTick(Engine::GetFixedDt());
			++it;
		}
		else
		{
			it = m_Characters.erase(it);
		}
	}

	// Buoyancy forces apply to this same step (pre-Update, after characters).
	for (auto it = m_Buoyancies.begin(); it != m_Buoyancies.end();)
	{
		if (auto buoyancy = it->lock())
		{
			buoyancy->TickBuoyancy(Engine::GetFixedDt());
			++it;
		}
		else
		{
			it = m_Buoyancies.erase(it);
		}
	}

	// 2 collision substeps: effective 50 Hz integration on the 25 Hz tick.
	m_System->Update(Engine::GetFixedDt(), 2, m_TempAllocator, m_JobSystem);

	for (auto it = m_BodySetups.begin(); it != m_BodySetups.end();)
	{
		if (auto body = it->lock())
		{
			body->CapturePhysicsState();
			++it;
		}
		else
		{
			it = m_BodySetups.erase(it);
		}
	}
}

void PhysicsWorld::TickUpdate(float dt)
{
	if (!m_SimulationEnabled || m_System == nullptr)
		return;

	SyncNodes(Engine::GetFixedTickAlpha(), dt);
}

void PhysicsWorld::SyncNodes(float alpha, float dt)
{
	for (auto it = m_BodySetups.begin(); it != m_BodySetups.end();)
	{
		if (auto body = it->lock())
		{
			body->SyncFromPhysics(alpha);
			++it;
		}
		else
		{
			it = m_BodySetups.erase(it);
		}
	}

	for (auto it = m_Characters.begin(); it != m_Characters.end();)
	{
		if (auto character = it->lock())
		{
			character->SyncFromPhysics(alpha, dt);
			++it;
		}
		else
		{
			it = m_Characters.erase(it);
		}
	}
}

void PhysicsWorld::CreateAllBodies()
{
	for (auto it = m_BodySetups.begin(); it != m_BodySetups.end();)
	{
		if (auto body = it->lock())
		{
			body->CreateBody();
			++it;
		}
		else
		{
			it = m_BodySetups.erase(it);
		}
	}

	for (auto it = m_Characters.begin(); it != m_Characters.end();)
	{
		if (auto character = it->lock())
		{
			character->CreateCharacter();
			++it;
		}
		else
		{
			it = m_Characters.erase(it);
		}
	}
}

void PhysicsWorld::DestroyAllBodies()
{
	for (auto it = m_BodySetups.begin(); it != m_BodySetups.end();)
	{
		if (auto body = it->lock())
		{
			body->DestroyBody();
			++it;
		}
		else
		{
			it = m_BodySetups.erase(it);
		}
	}

	for (auto it = m_Characters.begin(); it != m_Characters.end();)
	{
		if (auto character = it->lock())
		{
			character->DestroyCharacter();
			++it;
		}
		else
		{
			it = m_Characters.erase(it);
		}
	}
}

void PhysicsWorld::RegisterBodySetup(const std::shared_ptr<BodySetup> &body)
{
	m_BodySetups.emplace_back(body);
	if (m_SimulationEnabled)
		m_CreationDirty = true;
}

void PhysicsWorld::UnregisterBodySetup(const std::shared_ptr<BodySetup> &body)
{
	if (body)
		body->DestroyBody();

	m_BodySetups.erase(std::remove_if(m_BodySetups.begin(), m_BodySetups.end(),
		[&body](const std::weak_ptr<BodySetup> &weak)
		{
			auto locked = weak.lock();
			return !locked || locked == body;
		}), m_BodySetups.end());
}

void PhysicsWorld::RegisterCharacter(const std::shared_ptr<CharacterController> &character)
{
	m_Characters.emplace_back(character);
	if (m_SimulationEnabled)
		m_CreationDirty = true;
}

void PhysicsWorld::UnregisterCharacter(const std::shared_ptr<CharacterController> &character)
{
	if (character)
		character->DestroyCharacter();

	m_Characters.erase(std::remove_if(m_Characters.begin(), m_Characters.end(),
		[&character](const std::weak_ptr<CharacterController> &weak)
		{
			auto locked = weak.lock();
			return !locked || locked == character;
		}), m_Characters.end());
}

void PhysicsWorld::RegisterBuoyancy(const std::shared_ptr<BuoyancyComponent> &buoyancy)
{
	m_Buoyancies.emplace_back(buoyancy);
}

void PhysicsWorld::UnregisterBuoyancy(const std::shared_ptr<BuoyancyComponent> &buoyancy)
{
	m_Buoyancies.erase(std::remove_if(m_Buoyancies.begin(), m_Buoyancies.end(),
		[&buoyancy](const std::weak_ptr<BuoyancyComponent> &weak)
		{
			auto locked = weak.lock();
			return !locked || locked == buoyancy;
		}), m_Buoyancies.end());
}

bool PhysicsWorld::HasBuoyancyDebugDraw() const
{
	for (const auto &weak : m_Buoyancies)
		if (auto buoyancy = weak.lock())
			if (buoyancy->GetDebugDraw())
				return true;
	return false;
}
