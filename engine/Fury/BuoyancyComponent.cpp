#include "Fury/BuoyancyComponent.h"

#include <cmath>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "Fury/BodySetup.h"
#include "Fury/Log.h"
#include "Fury/Matrix4.h"
#include "Fury/OceanComponent.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

namespace fury
{
	BuoyancyComponent::Ptr BuoyancyComponent::Create()
	{
		return std::make_shared<BuoyancyComponent>();
	}

	BuoyancyComponent::BuoyancyComponent(const std::string &name)
		: m_Name(name)
	{
		m_TypeIndex = typeid(BuoyancyComponent);
	}

	bool BuoyancyComponent::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "BuoyancyComponent: json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "BuoyancyComponent")
		{
			FURYE << "BuoyancyComponent: invalid type " << str;
			return false;
		}

		LoadMemberValue(wrapper, "name", m_Name);
		LoadMemberValue(wrapper, "waterDensity", m_WaterDensity);
		LoadMemberValue(wrapper, "linearDrag", m_LinearDrag);
		LoadMemberValue(wrapper, "angularDrag", m_AngularDrag);
		LoadMemberValue(wrapper, "rightingStrength", m_RightingStrength);
		LoadMemberValue(wrapper, "oceanNode", m_OceanNodeName);
		LoadMemberValue(wrapper, "debugDraw", m_DebugDraw);

		m_FloatPoints.clear();
		LoadArray(wrapper, "floatPoints", [&](const void* elem) -> bool
		{
			FloatPoint point;
			LoadMemberValue(elem, "offset", point.Offset);
			LoadMemberValue(elem, "radius", point.Radius);
			m_FloatPoints.push_back(point);
			return true;
		});

		m_Ocean.reset();
		m_WarnedNoOcean = false;
		m_LastSubmersion.clear();
		return true;
	}

	void BuoyancyComponent::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);

		SaveKey(wrapper, "type"); SaveValue(wrapper, "BuoyancyComponent");
		SaveKey(wrapper, "name"); SaveValue(wrapper, m_Name);
		SaveKey(wrapper, "waterDensity"); SaveValue(wrapper, m_WaterDensity);
		SaveKey(wrapper, "linearDrag"); SaveValue(wrapper, m_LinearDrag);
		SaveKey(wrapper, "angularDrag"); SaveValue(wrapper, m_AngularDrag);
		SaveKey(wrapper, "rightingStrength"); SaveValue(wrapper, m_RightingStrength);
		SaveKey(wrapper, "oceanNode"); SaveValue(wrapper, m_OceanNodeName);
		SaveKey(wrapper, "debugDraw"); SaveValue(wrapper, m_DebugDraw);

		SaveKey(wrapper, "floatPoints");
		SaveArray(wrapper, (unsigned int)m_FloatPoints.size(), [&](unsigned int i)
		{
			StartObject(wrapper);
			SaveKey(wrapper, "offset"); SaveValue(wrapper, m_FloatPoints[i].Offset);
			SaveKey(wrapper, "radius"); SaveValue(wrapper, m_FloatPoints[i].Radius);
			EndObject(wrapper);
		});

		if (object) EndObject(wrapper);
	}

	Component::Ptr BuoyancyComponent::Clone() const
	{
		auto clone = BuoyancyComponent::Create();
		clone->m_Name = m_Name;
		clone->m_FloatPoints = m_FloatPoints;
		clone->m_WaterDensity = m_WaterDensity;
		clone->m_LinearDrag = m_LinearDrag;
		clone->m_AngularDrag = m_AngularDrag;
		clone->m_RightingStrength = m_RightingStrength;
		clone->m_OceanNodeName = m_OceanNodeName;
		clone->m_DebugDraw = m_DebugDraw;
		return clone;
	}

	void BuoyancyComponent::AddFloatPoint(const Vector4 &offset, float radius)
	{
		FloatPoint point;
		point.Offset = offset;
		point.Offset.w = 1.0f;
		point.Radius = radius;
		m_FloatPoints.push_back(point);
	}

	void BuoyancyComponent::SetFloatPoint(unsigned int index, const Vector4 &offset, float radius)
	{
		if (index >= m_FloatPoints.size())
			return;
		m_FloatPoints[index].Offset = offset;
		m_FloatPoints[index].Offset.w = 1.0f;
		m_FloatPoints[index].Radius = radius;
	}

	void BuoyancyComponent::RemoveFloatPoint(unsigned int index)
	{
		if (index >= m_FloatPoints.size())
			return;
		m_FloatPoints.erase(m_FloatPoints.begin() + index);
	}

	void BuoyancyComponent::ClearFloatPoints()
	{
		m_FloatPoints.clear();
	}

	void BuoyancyComponent::SetOceanNodeName(const std::string &name)
	{
		if (m_OceanNodeName != name)
		{
			m_OceanNodeName = name;
			m_Ocean.reset();
			m_WarnedNoOcean = false;
		}
	}

	float BuoyancyComponent::GetLastSubmersion(unsigned int index) const
	{
		if (index >= m_LastSubmersion.size())
			return 0.0f;
		return m_LastSubmersion[index];
	}

	void BuoyancyComponent::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);

		if (!PhysicsWorld::Exists())
			return;

		auto self = std::static_pointer_cast<BuoyancyComponent>(node->GetComponent(typeid(BuoyancyComponent)));
		if (self)
			PhysicsWorld::Instance()->RegisterBuoyancy(self);
	}

	void BuoyancyComponent::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnDetaching(node);

		if (!PhysicsWorld::Exists())
			return;

		if (auto self = std::static_pointer_cast<BuoyancyComponent>(node->GetComponent(typeid(BuoyancyComponent))))
			PhysicsWorld::Instance()->UnregisterBuoyancy(self);
	}

	std::shared_ptr<OceanComponent> BuoyancyComponent::ResolveOcean()
	{
		if (auto cached = m_Ocean.lock())
			return cached;
		if (Scene::Active == nullptr || m_OceanNodeName.empty())
			return nullptr;

		auto node = Scene::Active->GetRootNode()->FindChildRecursively(m_OceanNodeName);
		std::shared_ptr<OceanComponent> ocean = node ? node->GetComponent<OceanComponent>() : nullptr;
		if (ocean)
		{
			m_Ocean = ocean;
			m_WarnedNoOcean = false;
		}
		else if (!m_WarnedNoOcean)
		{
			m_WarnedNoOcean = true;
			FURYW << "BuoyancyComponent on '" << (m_Owner.lock() ? m_Owner.lock()->GetName() : m_Name)
				<< "': ocean node '" << m_OceanNodeName << "' not found (or no OceanComponent); body stays under normal gravity";
		}
		return ocean;
	}

	void BuoyancyComponent::TickBuoyancy(float fixedDt)
	{
		// forces are per-step accumulators; Jolt integrates. fixedDt is the
		// fixed step (used by the drag stability clamp).

		unsigned int count = (unsigned int)m_FloatPoints.size();
		if (count == 0 || !PhysicsWorld::Exists())
			return;

		auto node = m_Owner.lock();
		if (!node)
			return;

		auto body = node->GetComponent<BodySetup>();
		if (!body || !body->HasBody() || body->GetMotionType() != BodySetup::MotionType::Dynamic)
			return;

		auto ocean = ResolveOcean();
		if (!ocean)
			return;

		auto *system = PhysicsWorld::Instance()->GetSystem();
		if (system == nullptr)
			return;

		JPH::BodyInterface &bodies = system->GetBodyInterface();
		JPH::BodyID id(body->GetBodyID());
		if (!bodies.IsAdded(id))
			return;

		float mass = body->GetMass();
		if (mass <= 0.0001f)
			mass = 1.0f;

		Vector4 gravity = PhysicsWorld::Instance()->GetGravity();
		JPH::Vec3 lift(-gravity.x, -gravity.y, -gravity.z); // -g: buoyancy direction

		// Live body state from Jolt, NOT the node matrix: SyncNodes only
		// runs after the step batch (and at frame rate in play mode), so a
		// node read is the stale pre-simulation pose. Jolt body position is
		// COM-centered; float points are authored node-local, which matches
		// for the centered primitive shapes (sphere/box) floaters use.
		JPH::Vec3 bodyPos = bodies.GetPosition(id);
		JPH::Quat bodyRot = bodies.GetRotation(id);

		m_LastSubmersion.resize(count);

		Vector4 extents = body->GetHalfExtents();
		float size = extents.Length() > body->GetRadius() ? extents.Length() : body->GetRadius();

		bool applied = false;
		for (unsigned int i = 0; i < count; i++)
		{
			const FloatPoint &point = m_FloatPoints[i];
			JPH::Vec3 wp = bodyPos + bodyRot * JPH::Vec3(point.Offset.x, point.Offset.y, point.Offset.z);

			float waterY = ocean->WaveHeightAtWorld(wp.GetX(), wp.GetZ());
			float r = point.Radius > 0.001f ? point.Radius : 0.001f;
			float s = (waterY - wp.GetY() + r) / (2.0f * r);
			s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
			m_LastSubmersion[i] = s;
			if (s <= 0.0f)
				continue;

			JPH::Vec3 force = lift * (s * (mass / (float)count) * m_WaterDensity);

			if (m_LinearDrag > 0.0f)
				force -= bodies.GetPointVelocity(id, wp) * (m_LinearDrag * s * mass);

			bodies.AddForce(id, force, wp);
			applied = true;
		}

		// Rotational terms (water angular drag + righting) gate on proximity
		// to the surface, NOT on a wet point: an inverted floater hangs with
		// its bottom-face points dry, and gating on them would trap it
		// upside-down forever.
		float waterYCom = ocean->WaveHeightAtWorld(bodyPos.GetX(), bodyPos.GetZ());
		bool nearWater = std::fabs(bodyPos.GetY() - waterYCom) < size * 2.0f + 100.0f;

		if (!applied && !nearWater)
			return;

		// sleeping bodies ignore force/torque accumulators
		bodies.ActivateBody(id);

		if (!nearWater)
			return;

		// angular drag + righting scale with a crude inertia estimate
		// (mass * size^2) so the coefficients stay unitless-ish
		float inertia = mass * size * size;

		// angular drag: per-axis in body-local space using the REAL inverse
		// inertia from Jolt. The crude mass*size^2 estimate (used below for
		// righting) overshoots a thin long shape's short axes by 40x+, and at
		// the 25 Hz fixed tick the explicit factor drag*Iest/Ireal*dt then
		// exceeds 2 - the plank's roll diverged into a fast spin. The
		// per-tick factor is clamped to 0.9: unconditionally stable, and
		// identical to the old behavior wherever it was already stable.
		if (m_AngularDrag > 0.0f)
		{
			JPH::Vec3 omega = bodies.GetAngularVelocity(id);
			if (omega.LengthSq() > 1e-10f)
			{
				JPH::Vec3 invI(1.0f, 1.0f, 1.0f);
				{
					JPH::BodyLockRead lock(system->GetBodyLockInterface(), id);
					if (lock.Succeeded())
					{
						const JPH::MotionProperties *mp = lock.GetBody().GetMotionProperties();
						if (mp != nullptr)
						{
							// diagonal of the local inverse inertia tensor
							JPH::Mat44 invIM = mp->GetLocalSpaceInverseInertia();
							invI = JPH::Vec3(invIM(0, 0), invIM(1, 1), invIM(2, 2));
						}
					}
				}
				JPH::Quat conj = bodyRot.Conjugated();
				JPH::Vec3 wLoc = conj * omega;
				JPH::Vec3 tLoc(0.0f, 0.0f, 0.0f);
				for (int axis = 0; axis < 3; ++axis)
				{
					float w = wLoc[axis];
					float ii = invI[axis];
					if (w == 0.0f || ii <= 1e-8f)
						continue;
					float f = m_AngularDrag * inertia * ii * fixedDt;
					if (f > 0.9f)
						f = 0.9f;
					// torque that lands the clamped delta-omega this tick
					tLoc.SetComponent(axis, -w * f / (ii * fixedDt));
				}
				bodies.AddTorque(id, bodyRot * tLoc);
			}
		}

		if (m_RightingStrength > 0.0f)
		{
			JPH::Vec3 up = bodyRot * JPH::Vec3(0.0f, 1.0f, 0.0f);
			float cosTheta = up.Dot(JPH::Vec3(0.0f, 1.0f, 0.0f));
			JPH::Vec3 axis = up.Cross(JPH::Vec3(0.0f, 1.0f, 0.0f));
			float len = axis.Length();
			// magnitude ~ sin(theta/2): stays strong through horizontal and
			// is maximal inverted, where the plain cross product (~ sin
			// theta) vanishes and the top-points-dry pendulum would
			// otherwise be a stable trap
			float mag = std::sqrt(std::max(0.0f, (1.0f - cosTheta) * 0.5f));
			if (len > 0.0001f)
				bodies.AddTorque(id, axis * ((m_RightingStrength * inertia * mag) / len));
			else if (cosTheta < 0.0f)
				// exactly inverted: degenerate axis, flip around any axis
				bodies.AddTorque(id, JPH::Vec3(1.0f, 0.0f, 0.0f) * (m_RightingStrength * inertia));
		}
	}
}
