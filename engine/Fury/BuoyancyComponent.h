#ifndef _FURY_BUOYANCY_COMPONENT_H_
#define _FURY_BUOYANCY_COMPONENT_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/Component.h"
#include "Fury/Vector4.h"

namespace fury
{
	class OceanComponent;

	// Buoyancy for a sibling BodySetup's dynamic body (change: add-fft-ocean,
	// design D8). Float points (node-local offsets + radii) sample the named
	// ocean's CPU wave height each fixed tick; differential submersion yields
	// buoyant force + natural righting torque, plus our own linear/angular
	// drag (BodySetup needs no damping fields). Registered with PhysicsWorld
	// on attach and ticked pre-step (TickBuoyancy runs right before
	// PhysicsSystem::Update so forces apply same-tick). Inert outside play
	// mode (TickFixed never runs there) and flat-fallback when the ocean or
	// its wave asset is missing (one warning per attach).
	class FURY_API BuoyancyComponent : public Component
	{
	public:

		struct FloatPoint
		{
			Vector4 Offset = Vector4(0.0f, 0.0f, 0.0f, 1.0f); // node-local, cm
			float Radius = 25.0f;                              // submersion depth scale, cm
		};

		typedef std::shared_ptr<BuoyancyComponent> Ptr;

		static Ptr Create();

		BuoyancyComponent(const std::string &name = "BuoyancyComponent");

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
		Component::Ptr Clone() const override;

		const std::string &GetName() const { return m_Name; }
		void SetName(const std::string &name) { m_Name = name; }

		unsigned int GetFloatPointCount() const { return (unsigned int)m_FloatPoints.size(); }
		const FloatPoint &GetFloatPoint(unsigned int index) const { return m_FloatPoints[index]; }
		void AddFloatPoint(const Vector4 &offset, float radius);
		void SetFloatPoint(unsigned int index, const Vector4 &offset, float radius);
		void RemoveFloatPoint(unsigned int index);
		void ClearFloatPoints();

		// Buoyant force multiplier per unit submersion: F = s * (mass/n) *
		// |g| * density. 1.0 = neutral at full submersion; ~2 floats a body
		// half-submerged.
		float GetWaterDensity() const { return m_WaterDensity; }
		void SetWaterDensity(float v) { m_WaterDensity = v; }

		float GetLinearDrag() const { return m_LinearDrag; }
		void SetLinearDrag(float v) { m_LinearDrag = v; }

		float GetAngularDrag() const { return m_AngularDrag; }
		void SetAngularDrag(float v) { m_AngularDrag = v; }

		float GetRightingStrength() const { return m_RightingStrength; }
		void SetRightingStrength(float v) { m_RightingStrength = v; }

		const std::string &GetOceanNodeName() const { return m_OceanNodeName; }
		void SetOceanNodeName(const std::string &name);

		bool GetDebugDraw() const { return m_DebugDraw; }
		void SetDebugDraw(bool v) { m_DebugDraw = v; }

		// PhysicsWorld::TickFixed hook (pre-step). No-op without a live
		// body / ocean, or with zero float points.
		void TickBuoyancy(float fixedDt);

		// Debug-draw support: last tick's submersion per point (0 dry ..
		// 1 fully submerged); stale while never ticked.
		float GetLastSubmersion(unsigned int index) const;
		const std::vector<FloatPoint> &GetFloatPoints() const { return m_FloatPoints; }

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;
		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		// Resolves (and caches) the named ocean node in the active scene.
		// One logged warning per resolution failure streak.
		std::shared_ptr<OceanComponent> ResolveOcean();

		std::string m_Name = "BuoyancyComponent";

		std::vector<FloatPoint> m_FloatPoints;

		float m_WaterDensity = 1.0f;

		float m_LinearDrag = 0.5f;

		float m_AngularDrag = 0.5f;

		float m_RightingStrength = 0.0f;

		std::string m_OceanNodeName = "Ocean";

		bool m_DebugDraw = false;

		// transient
		std::weak_ptr<OceanComponent> m_Ocean;
		std::vector<float> m_LastSubmersion;
		bool m_WarnedNoOcean = false;
	};
}

#endif // _FURY_BUOYANCY_COMPONENT_H_
