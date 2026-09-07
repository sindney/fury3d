#ifndef _FURY_PHYSICS_WORLD_H_
#define _FURY_PHYSICS_WORLD_H_

#include <memory>
#include <vector>

#include "Fury/Macros.h"
#include "Fury/Singleton.h"
#include "Fury/Vector4.h"

// Jolt types are forward-declared so this header stays Jolt-free (compile
// time + JPH_* config define isolation). Consumers that need the real types
// include <Jolt/Jolt.h> in their .cpp.
namespace JPH
{
	class PhysicsSystem;
	class TempAllocator;
	class JobSystem;
}

namespace fury
{
	class BodySetup;
	class BuoyancyComponent;
	class CharacterController;
	class SceneNode;

	// Owns the Jolt PhysicsSystem and steps it on the engine fixed tick.
	// Created by Engine::Initialize, destroyed by Engine::Shutdown (after
	// Scene::Active is reset, so components have already destroyed bodies).
	//
	// Units: 1 physics unit = 1 engine world unit = 1 cm (design D2). Default
	// gravity is (0, -981, 0). A scene's optional "physics" block overrides.
	//
	// Simulation gating: fury enables, furye disables (main.cpp). While
	// disabled, no bodies exist and TickFixed/TickUpdate are no-ops.
	class FURY_API PhysicsWorld : public Singleton<PhysicsWorld>
	{
	public:

		PhysicsWorld();

		virtual ~PhysicsWorld();

		// Connects to Engine::OnFixedUpdate/OnUpdate. Called from
		// Engine::Initialize after the shared_ptr exists (Signal::Connect
		// needs a live shared_ptr receiver, so this can't run in the ctor).
		void Subscribe();

		static bool Exists();

		bool IsSimulationEnabled() const { return m_SimulationEnabled; }

		// Enabling builds Jolt bodies for every registered BodySetup;
		// disabling destroys them. Stepping only happens while enabled.
		void SetSimulationEnabled(bool enabled);

		// Applies to the Jolt world immediately (and to future bodies).
		void SetGravity(const Vector4 &gravity);
		Vector4 GetGravity() const { return m_Gravity; }

		// Headless helper for Lua/tests: run n fixed ticks synchronously,
		// then sync nodes at alpha=1 (exact final state). No-op while
		// simulation is disabled.
		void Step(int n = 1);

		// Component registration. Components self-register on attach and
		// unregister on detach; the world uses the lists for body
		// create/destroy on enable/disable and for the per-frame sync.
		// Creation is LAZY: registration only marks the creation pass dirty,
		// and the next TickFixed recomposes the scene root (world matrices
		// are stale during Scene::Load) before creating bodies.
		void RegisterBodySetup(const std::shared_ptr<BodySetup> &body);
		void UnregisterBodySetup(const std::shared_ptr<BodySetup> &body);
		void RegisterCharacter(const std::shared_ptr<CharacterController> &character);
		void UnregisterCharacter(const std::shared_ptr<CharacterController> &character);
		// Buoyancy forces tick pre-step (TickBuoyancy runs right before
		// PhysicsSystem::Update, same-tick application).
		void RegisterBuoyancy(const std::shared_ptr<BuoyancyComponent> &buoyancy);
		void UnregisterBuoyancy(const std::shared_ptr<BuoyancyComponent> &buoyancy);

		// Registered buoyancy components (Pipeline::DrawDebug reads this).
		const std::vector<std::weak_ptr<BuoyancyComponent>> &GetBuoyancies() const { return m_Buoyancies; }

		// Jolt accessors for physics components (BodySetup/CharacterController
		// .cpps include Jolt and use these directly). Null while uninitialized.
		JPH::PhysicsSystem *GetSystem() const { return m_System; }
		JPH::TempAllocator *GetTempAllocator() const { return m_TempAllocator; }
		JPH::JobSystem *GetJobSystem() const { return m_JobSystem; }

	private:

		void TickFixed();

		void TickUpdate(float dt);

		void SyncNodes(float alpha, float dt);

		void CreateAllBodies();

		void DestroyAllBodies();

		// Walks the active scene and registers any BodySetup /
		// CharacterController components not already registered. Runs on
		// SetSimulationEnabled(true): components attached before the world
		// existed (lazy Lua creation) never got to self-register.
		void DiscoverComponentsInScene();

		bool m_SimulationEnabled = false;

		bool m_CreationDirty = false;

		Vector4 m_Gravity = Vector4(0.0f, -981.0f, 0.0f, 0.0f);

		size_t m_FixedUpdateKey = 0;

		size_t m_UpdateKey = 0;

		JPH::PhysicsSystem *m_System = nullptr;

		JPH::TempAllocator *m_TempAllocator = nullptr;

		JPH::JobSystem *m_JobSystem = nullptr;

		std::vector<std::weak_ptr<BodySetup>> m_BodySetups;

		std::vector<std::weak_ptr<CharacterController>> m_Characters;

		std::vector<std::weak_ptr<BuoyancyComponent>> m_Buoyancies;
	};
}

#endif // _FURY_PHYSICS_WORLD_H_
