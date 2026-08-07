#ifndef _FURY_BODY_SETUP_H_
#define _FURY_BODY_SETUP_H_

#include <string>

#include "Fury/Component.h"
#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"

namespace fury
{
	class Mesh;

	// Physics collision authoring for a SceneNode (design D7). Default shape
	// is the sibling MeshRender's mesh; box/sphere are auto-fit from its AABB.
	// Static bodies bake the node's world matrix into the shape; dynamic
	// bodies bake world scale into shape dims and write their simulated
	// transform back to the node each frame (design D5).
	//
	// This header is Jolt-free: the JPH::BodyID is stored as its raw uint32.
	class FURY_API BodySetup : public Component
	{
	public:

		enum class ShapeType
		{
			Mesh = 0,
			Box = 1,
			Sphere = 2
		};

		enum class MotionType
		{
			Static = 0,
			Dynamic = 1
		};

		// JPH::BodyID::cInvalidBodyID - kept as a literal so this header
		// doesn't include Jolt.
		static constexpr unsigned int kInvalidBodyID = 0xFFFFFFFFu;

		typedef std::shared_ptr<BodySetup> Ptr;

		static Ptr Create();

		BodySetup();

		virtual ~BodySetup();

		virtual Component::Ptr Clone() const override;

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		ShapeType GetShapeType() const { return m_ShapeType; }
		void SetShapeType(ShapeType type);

		MotionType GetMotionType() const { return m_MotionType; }
		void SetMotionType(MotionType type);

		// Empty = use the sibling MeshRender's mesh.
		const std::string &GetCollisionMeshName() const { return m_CollisionMeshName; }
		void SetCollisionMeshName(const std::string &name) { m_CollisionMeshName = name; }

		Vector4 GetHalfExtents() const { return m_HalfExtents; }
		void SetHalfExtents(const Vector4 &extents) { m_HalfExtents = extents; }

		float GetRadius() const { return m_Radius; }
		void SetRadius(float radius) { m_Radius = radius; }

		float GetMass() const { return m_Mass; }
		void SetMass(float mass) { m_Mass = mass; }

		float GetFriction() const { return m_Friction; }
		void SetFriction(float friction) { m_Friction = friction; }

		float GetRestitution() const { return m_Restitution; }
		void SetRestitution(float restitution) { m_Restitution = restitution; }

		bool HasBody() const { return m_BodyID != kInvalidBodyID; }
		unsigned int GetBodyID() const { return m_BodyID; }

		// Resolves the effective collision source mesh: the named collision
		// mesh asset, else the sibling MeshRender's base mesh. Can be null.
		std::shared_ptr<Mesh> ResolveCollisionMesh() const;

		// Initializes halfExtents/radius from the resolved mesh's local AABB.
		void AutoFitFromMesh();

		// PhysicsWorld hooks (called only while simulation is enabled).
		void CreateBody();
		void DestroyBody();
		void CapturePhysicsState();
		void SyncFromPhysics(float alpha);

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnOwnerDestructing(SceneNode &node) override;

		ShapeType m_ShapeType = ShapeType::Mesh;

		MotionType m_MotionType = MotionType::Static;

		std::string m_CollisionMeshName;

		Vector4 m_HalfExtents = Vector4(50.0f, 50.0f, 50.0f, 0.0f);

		float m_Radius = 50.0f;

		float m_Mass = 10.0f;

		float m_Friction = 0.5f;

		float m_Restitution = 0.1f;

		unsigned int m_BodyID = kInvalidBodyID;

		// Interpolation state: previous/current physics-tick transforms.
		// SyncFromPhysics lerps between them with Engine::GetFixedTickAlpha().
		Vector4 m_PrevPos;

		Vector4 m_CurrPos;

		Quaternion m_PrevRot;

		Quaternion m_CurrRot;
	};
}

#endif // _FURY_BODY_SETUP_H_
