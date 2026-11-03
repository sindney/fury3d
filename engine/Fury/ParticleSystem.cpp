#include "Fury/ParticleSystem.h"

#include <algorithm>
#include <cmath>

#include "Fury/Engine.h"
#include "Fury/Archive.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"

namespace fury
{
	ParticleSystem::Ptr ParticleSystem::Create(const std::string &name)
	{
		auto ptr = std::make_shared<ParticleSystem>(name);
		ptr->Subscribe();
		return ptr;
	}

	ParticleSystem::ParticleSystem(const std::string &name)
		: Entity(name)
	{
		m_Particles.resize(m_MaxParticles);
	}

	ParticleSystem::~ParticleSystem()
	{
		Unsubscribe();
	}

	void ParticleSystem::Subscribe()
	{
		if (m_Subscribed) return;
		auto self = std::static_pointer_cast<ParticleSystem>(shared_from_this());
		m_UpdateKey = Engine::OnUpdate->Connect(self, &ParticleSystem::TickOnUpdate);
		m_Subscribed = true;
		FURYD << "ParticleSystem '" << GetName() << "' subscribed (key=" << m_UpdateKey << ")";
	}

	void ParticleSystem::Unsubscribe()
	{
		if (!m_Subscribed) return;
		Engine::OnUpdate->Disconnect(m_UpdateKey);
		m_Subscribed = false;
	}

	void ParticleSystem::TickOnUpdate(float dt)
	{
		if (m_ExternallyDriven) return;
		Update(dt);
	}

	void ParticleSystem::Serialize(Archive& ar)
	{
		Entity::SerializeFields(ar);

		std::string type = "ParticleSystem";
		ar << Required("type", type);
		if (ar.IsLoading() && type != "ParticleSystem")
		{
			ar.SetError("invalid particle system type '" + type + "'");
			return;
		}

		unsigned int cap = m_MaxParticles;
		ar << Optional("maxParticles", cap, m_MaxParticles);
		if (ar.IsLoading())
			SetMaxParticles(cap);
		ar << Optional("lifetime", m_Lifetime, 1.0f);
		ar << Optional("startSize", m_StartSize, 0.2f);

		// Modules: loaded only when their block is present (defaults
		// kept otherwise), always written on save.
		auto transferModule = [&ar](const char *name, Serializable &module)
		{
			if (ar.EnterObject(name, ArchiveFieldPolicy::Optional))
			{
				module.Serialize(ar);
				ar.LeaveObject();
			}
		};
		transferModule("emission", m_Emission);
		transferModule("shape", m_Shape);
		transferModule("velocity", m_Velocity);
		transferModule("colorOverLifetime", m_Color);
		transferModule("sizeOverLifetime", m_Size);
		transferModule("rotationOverLifetime", m_Rotation);
		transferModule("renderer", m_Renderer);
	}

	ParticleSystem::Ptr ParticleSystem::Clone() const
	{
		auto clone = Create(GetName());
		clone->m_MaxParticles = m_MaxParticles;
		clone->m_Lifetime = m_Lifetime;
		clone->m_StartSize = m_StartSize;
		clone->m_Emission = m_Emission;
		clone->m_Shape = m_Shape;
		clone->m_Velocity = m_Velocity;
		clone->m_Color = m_Color;
		clone->m_Size = m_Size;
		clone->m_Rotation = m_Rotation;
		clone->m_Renderer = m_Renderer;
		clone->m_RandomSeed = m_RandomSeed;
		clone->m_Particles.resize(clone->m_MaxParticles);
		clone->Reset();
		return clone;
	}

	unsigned int ParticleSystem::GetAliveCount() const
	{
		unsigned int n = 0;
		for (const auto &p : m_Particles)
			if (p.alive) ++n;
		return n;
	}

	void ParticleSystem::SetMaxParticles(unsigned int cap)
	{
		m_MaxParticles = std::min<unsigned int>(cap, FURY_PARTICLE_MAX_PER_SYSTEM);
		m_Particles.resize(m_MaxParticles);
		for (auto &p : m_Particles) p.alive = false;
		m_SpawnAccumulator = 0.0f;
		m_WarnedCap = false;
	}

	void ParticleSystem::Emit(int count)
	{
		if (count <= 0) return;
		for (int i = 0; i < count; ++i)
			SpawnOne();
	}

	bool ParticleSystem::IsAlive() const
	{
		for (const auto &p : m_Particles)
			if (p.alive) return true;
		return false;
	}

	BoxBounds ParticleSystem::GetLocalBounds() const
	{
		float shapeR = m_Shape.radius;
		if (m_Shape.type == ParticleShape::BOX)
			shapeR = std::max(std::abs(m_Shape.scale.x),
				std::max(std::abs(m_Shape.scale.y), std::abs(m_Shape.scale.z))) * 0.5f;
		else if (m_Shape.type == ParticleShape::CONE)
			shapeR = m_Shape.radius * 2.0f; // cone tip extends radius up

		float maxCurve = 1.0f;
		for (const auto &k : m_Size.size.keys)
			maxCurve = std::max(maxCurve, std::abs(k.value));

		const float r = shapeR + m_Velocity.speed * m_Lifetime
			+ m_StartSize * maxCurve * 0.5f;
		return BoxBounds(Vector4(-r, -r, -r, 1.0f), Vector4(r, r, r, 1.0f));
	}

	void ParticleSystem::Reset()
	{
		for (auto &p : m_Particles) p.alive = false;
		m_SpawnAccumulator = 0.0f;
		m_BurstCursor = 0.0f;
		m_WarnedCap = false;
		m_RandomState = m_RandomSeed;
	}

	void ParticleSystem::Update(float dt)
	{
		if (dt <= 0.0f) return;

		for (auto &p : m_Particles)
		{
			if (!p.alive) continue;
			p.age += dt;
			if (p.age >= p.lifetime)
				p.alive = false;
		}

		for (auto &p : m_Particles)
		{
			if (!p.alive) continue;
			const float t01 = (p.lifetime > 0.0f) ? (p.age / p.lifetime) : 0.0f;
			p.color = m_Color.color.Sample(t01);
			p.size = m_StartSize * m_Size.size.Sample(t01);
			if (m_Rotation.angularVelocity != 0.0f)
				p.rotation += MathUtil::DegToRad * m_Rotation.angularVelocity * dt;
		}

		for (auto &p : m_Particles)
		{
			if (!p.alive) continue;
			p.position.x += p.velocity.x * dt;
			p.position.y += p.velocity.y * dt;
			p.position.z += p.velocity.z * dt;
		}

		m_SpawnAccumulator += m_Emission.rateOverTime * dt;
		int toSpawn = static_cast<int>(m_SpawnAccumulator);
		m_SpawnAccumulator -= static_cast<float>(toSpawn);
		for (int i = 0; i < toSpawn; ++i)
			SpawnOne();

		for (const auto &b : m_Emission.bursts)
		{
			if (b.time >= m_BurstCursor && b.time < m_BurstCursor + dt)
			{
				if (b.probability >= 1.0f ||
					NextRandom() <= b.probability)
				{
					for (int i = 0; i < b.count; ++i)
						SpawnOne();
				}
			}
		}
		m_BurstCursor += dt;
	}

	bool ParticleSystem::SpawnOne()
	{
		for (auto &p : m_Particles)
		{
			if (p.alive) continue;

			p.alive = true;
			p.age = 0.0f;
			p.lifetime = m_Lifetime;
			p.position = SampleSpawnPosition();

			const Vector4 dir = m_Velocity.linear;
			const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len > 1e-6f)
			{
				p.velocity.x = (dir.x / len) * m_Velocity.speed;
				p.velocity.y = (dir.y / len) * m_Velocity.speed;
				p.velocity.z = (dir.z / len) * m_Velocity.speed;
			}
			else
			{
				p.velocity.x = 0.0f;
				p.velocity.y = m_Velocity.speed;
				p.velocity.z = 0.0f;
			}
			p.color = m_Color.color.Sample(0.0f);
			p.size = m_StartSize * m_Size.size.Sample(0.0f);
			p.rotation = 0.0f;
			return true;
		}

		if (!m_WarnedCap)
		{
			FURYW << "ParticleSystem '" << GetName() << "': live pool clamped at "
				  << m_MaxParticles << " (FURY_PARTICLE_MAX_PER_SYSTEM cap)";
			m_WarnedCap = true;
		}
		return false;
	}

	float ParticleSystem::NextRandom()
	{
		unsigned int x = m_RandomState;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		m_RandomState = x;
		return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
	}

	Vector4 ParticleSystem::SampleSpawnPosition()
	{
		auto rng = [this]() -> float {
			return NextRandom();
		};
		auto rngSigned = [&]() -> float {
			return rng() * 2.0f - 1.0f;
		};

		switch (m_Shape.type)
		{
		case ParticleShape::BOX:
		{
			return Vector4(
				rngSigned() * m_Shape.scale.x * 0.5f,
				rngSigned() * m_Shape.scale.y * 0.5f,
				rngSigned() * m_Shape.scale.z * 0.5f, 1.0f);
		}
		case ParticleShape::SPHERE:
		{
			const float u = rng() * 2.0f - 1.0f;
			const float theta = rng() * 2.0f * 3.14159265f;
			const float r = std::sqrt(std::max(0.0f, 1.0f - u * u));
			return Vector4(
				r * std::cos(theta) * m_Shape.radius,
				u * m_Shape.radius,
				r * std::sin(theta) * m_Shape.radius, 1.0f);
		}
		case ParticleShape::CONE:
		{
			const float halfAngle = MathUtil::DegToRad * m_Shape.angle;
			const float phi = rng() * 2.0f * 3.14159265f;
			const float cosTheta = std::cos(halfAngle * rng());
			const float sinTheta = std::sin(halfAngle * rng());
			return Vector4(
				sinTheta * std::cos(phi) * m_Shape.radius,
				cosTheta * m_Shape.radius,
				sinTheta * std::sin(phi) * m_Shape.radius, 1.0f);
		}
		}
		return Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	}
}