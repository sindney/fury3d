#ifndef _FURY_PARTICLE_MODULES_H_
#define _FURY_PARTICLE_MODULES_H_

#include <vector>

#include "Fury/Color.h"
#include "Fury/Macros.h"
#include "Fury/Serializable.h"
#include "Fury/Vector4.h"

// Per-emitter cap on live particles. Tasks 1.6 + 5.x: warn-once at clamp.
#ifndef FURY_PARTICLE_MAX_PER_SYSTEM
#define FURY_PARTICLE_MAX_PER_SYSTEM 4096
#endif

namespace fury
{
	// Shape primitives the emitter spawns from. BOX/SPHERE/CONE — the v1 set
	// the wood-pile scene needs (cone for the fire column, sphere fallback
	// for smoke). New shapes add a case in ParticleSystem::Spawn.
	enum class ParticleShape : unsigned int
	{
		BOX = 0,
		SPHERE,
		CONE
	};

	// Particle renderer blend mode. ALPHA → SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
	// ADDITIVE → ONE/ONE. TASK 1.4: respected by ParticleShader + Pass state.
	enum class ParticleBlend : unsigned int
	{
		ALPHA = 0,
		ADDITIVE
	};

	// (time01, color) pair. Linear interpolation between adjacent keys. The
	// default-constructed gradient is solid white so a fresh
	// ColorOverLifetimeModule doesn't darken particles on first use.
	struct FURY_API GradientKey
	{
		float time = 0.0f;
		Color color = Color::White;
	};

	// Inherit from Serializable so Load/Save can reach the
	// Serializable::IsObject / LoadMemberValue / SaveKey helpers
	// (those are protected; the inheritance is the documented
	// access path).
	struct FURY_API Gradient : public Serializable
	{
		std::vector<GradientKey> keys;

		Gradient();

		// Linear interpolation between the surrounding keys. time01 is
		// clamped to [0, 1]; empty gradient returns white.
		Color Sample(float time01) const;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	// (time01, value) key. Linear interpolation; values multiply the
	// particle's initial size. Default curve is `[(0, 1), (1, 1)]` so
	// particles keep their authored size unless the user authors a curve.
	struct FURY_API CurveKey
	{
		float time = 0.0f;
		float value = 1.0f;
	};

	struct FURY_API AnimationCurve : public Serializable
	{
		std::vector<CurveKey> keys;

		AnimationCurve();

		// Linear interpolation between adjacent keys. Returns 1.0 for
		// empty / default curves.
		float Sample(float time01) const;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	// One-shot emission pulse. Task 5.x: EmissionModule applies bursts on
	// top of the rateOverTime stream. Probability lets the user make
	// stochastic bursts (e.g. embers with 0.3 chance per spawn event).
	struct FURY_API Burst : public Serializable
	{
		float time = 0.0f;
		int count = 0;
		float probability = 1.0f;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	// Spawn cadence. Defaults to a no-op (rate = 0, no bursts); the
	// ParticleSystem only emits when the user has configured at least one
	// source.
	struct FURY_API EmissionModule : public Serializable
	{
		float rateOverTime = 0.0f;
		std::vector<Burst> bursts;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	// Where the particle spawns. Defaults to a unit box so a freshly
	// constructed emitter still places particles around the owner node.
	struct FURY_API ShapeModule : public Serializable
	{
		ParticleShape type = ParticleShape::BOX;
		Vector4 scale = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		float radius = 0.5f;
		float angle = 25.0f; // CONE half-angle in degrees

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	// Initial linear velocity, multiplied by `speed`. Inherit-from-parent
	// adds the owner node's world-velocity so emitters parented to moving
	// scene nodes (vehicles, doors) drag along.
	struct FURY_API VelocityModule : public Serializable
	{
		Vector4 linear = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
		float speed = 1.0f;
		bool inheritFromParent = false;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	struct FURY_API ColorOverLifetimeModule : public Serializable
	{
		Gradient color;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	struct FURY_API SizeOverLifetimeModule : public Serializable
	{
		AnimationCurve size;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	struct FURY_API RotationOverLifetimeModule : public Serializable
	{
		float angularVelocity = 0.0f; // degrees / second, around camera-axis

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	// Renderer-side authoring. materialName references an EntityManager
	// entry by name (Material is the carrier for the diffuse texture);
	// blendMode drives the ParticleShader's blend state.
	struct FURY_API RendererModule : public Serializable
	{
		std::string materialName;
		ParticleBlend blendMode = ParticleBlend::ALPHA;
		// Sample the first shadow-casting light's shadow map and dim the
		// particles by it (ALPHA blend only — ADDITIVE is light-emitting
		// by convention and never shadowed).
		bool receiveShadows = true;

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
	};

	// Live particle POD. Initial size lives on the Particle; SizeOverLifetime
	// multiplies it. Position/velocity are in owner-local space and
	// transformed by ParticleRenderer via the owner's world matrix.
	struct FURY_API Particle
	{
		Vector4 position = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
		Vector4 velocity = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		Color color = Color::White;
		float age = 0.0f;
		float lifetime = 1.0f;
		float size = 0.1f;
		float rotation = 0.0f; // radians, around camera-axis
		bool alive = false;
	};
}

#endif // _FURY_PARTICLE_MODULES_H_