#ifndef _FURY_PARTICLE_SYSTEM_H_
#define _FURY_PARTICLE_SYSTEM_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/BoxBounds.h"
#include "Fury/Entity.h"
#include "Fury/ParticleModules.h"

namespace fury
{
	class SceneNode;

	// Unity-style CPU-driven particle system, treated as a project asset
	// (registered in the active Scene's EntityManager alongside Mesh /
	// Material / Texture / AnimationClip) -- referenced by name from a
	// ParticleRenderer component on a SceneNode.
	//
	// Mirrors Animator's per-instance Engine::OnUpdate subscription
	// (subscribe in Create / on first use, unsubscribe in the dtor)
	// so each emitter ticks every frame independently. The editor's
	// preview seeks the simulation by calling Update(dt) directly
	// with the scrubbed time.
	class FURY_API ParticleSystem : public Entity, public std::enable_shared_from_this<ParticleSystem>
	{
	public:
		typedef std::shared_ptr<ParticleSystem> Ptr;

		static Ptr Create(const std::string &name = "ParticleSystem");

		ParticleSystem(const std::string &name = "ParticleSystem");

		virtual ~ParticleSystem();

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
		Ptr Clone() const;

		// Live particle count (slots whose `alive` flag is true).
		unsigned int GetAliveCount() const;

		// Pool capacity -- bounded by FURY_PARTICLE_MAX_PER_SYSTEM.
		unsigned int GetMaxParticles() const { return m_MaxParticles; }
		void SetMaxParticles(unsigned int cap);

		float GetLifetime() const { return m_Lifetime; }
		void SetLifetime(float seconds) { m_Lifetime = std::max(0.01f, seconds); }

		float GetStartSize() const { return m_StartSize; }
		void SetStartSize(float size) { m_StartSize = std::max(0.0f, size); }

		EmissionModule &GetEmission() { return m_Emission; }
		const EmissionModule &GetEmission() const { return m_Emission; }

		ShapeModule &GetShape() { return m_Shape; }
		const ShapeModule &GetShape() const { return m_Shape; }

		VelocityModule &GetVelocity() { return m_Velocity; }
		const VelocityModule &GetVelocity() const { return m_Velocity; }

		ColorOverLifetimeModule &GetColorOverLifetime() { return m_Color; }
		const ColorOverLifetimeModule &GetColorOverLifetime() const { return m_Color; }

		SizeOverLifetimeModule &GetSizeOverLifetime() { return m_Size; }
		const SizeOverLifetimeModule &GetSizeOverLifetime() const { return m_Size; }

		RotationOverLifetimeModule &GetRotationOverLifetime() { return m_Rotation; }
		const RotationOverLifetimeModule &GetRotationOverLifetime() const { return m_Rotation; }

		RendererModule &GetRenderer() { return m_Renderer; }
		const RendererModule &GetRenderer() const { return m_Renderer; }

		void Emit(int count);

		bool IsAlive() const;

		// Suspends the Engine::OnUpdate tick while an editor preview
		// drives Update() manually (a particle editor window has this
		// system open -- without it the wall-clock tick double-advances
		// the preview and fights the paused/scrub Reset+seek path).
		// Runtime-only state, never serialized.
		void SetExternallyDriven(bool driven) { m_ExternallyDriven = driven; }
		bool IsExternallyDriven() const { return m_ExternallyDriven; }

		// Advance the simulation by `dt` seconds. Connected to
		// Engine::OnUpdate by Create() -- the editor's preview can
		// also call this directly with a seeked dt when the user
		// scrubs the timeline.
		void Update(float dt);

		void Reset();

		const std::vector<Particle> &GetParticles() const { return m_Particles; }

		// Conservative LOCAL-space bounds of everything this system can
		// draw: shape extent + velocity travel over a full lifetime +
		// the largest quad the size curve can produce. ParticleRenderer
		// expands its owner node's AABB with this so frustum culling
		// (which tests the node's AABB) never culls visible particles --
		// the emitter node's authored AABB only covers the spawn point.
		BoxBounds GetLocalBounds() const;

	private:
		std::vector<Particle> m_Particles;
		unsigned int m_MaxParticles = FURY_PARTICLE_MAX_PER_SYSTEM;
		float m_SpawnAccumulator = 0.0f;
		float m_BurstCursor = 0.0f;
		float m_Lifetime = 1.0f;
		float m_StartSize = 0.2f;
		bool m_WarnedCap = false;

		EmissionModule m_Emission;
		ShapeModule m_Shape;
		VelocityModule m_Velocity;
		ColorOverLifetimeModule m_Color;
		SizeOverLifetimeModule m_Size;
		RotationOverLifetimeModule m_Rotation;
		RendererModule m_Renderer;

		// Engine::OnUpdate subscription (mirrors Animator's pattern).
		size_t m_UpdateKey = 0;
		bool m_Subscribed = false;
		bool m_ExternallyDriven = false;

		void Subscribe();
		void Unsubscribe();
		void TickOnUpdate(float dt);

		bool SpawnOne();
		Vector4 SampleSpawnPosition();
	};
}

#endif // _FURY_PARTICLE_SYSTEM_H_