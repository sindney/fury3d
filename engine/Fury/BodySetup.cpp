#include "Fury/BodySetup.h"

#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNINGS

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Terrain.h"

using namespace fury;

namespace
{
	// Must match the Layers mapping inside PhysicsWorld.cpp.
	constexpr JPH::ObjectLayer cLayerNonMoving = 0;
	constexpr JPH::ObjectLayer cLayerMoving = 1;

	const char *ShapeTypeToString(BodySetup::ShapeType type)
	{
		switch (type)
		{
		case BodySetup::ShapeType::Box: return "box";
		case BodySetup::ShapeType::Sphere: return "sphere";
		case BodySetup::ShapeType::HeightField: return "heightfield";
		default: return "mesh";
		}
	}

	bool StringToShapeType(const std::string &str, BodySetup::ShapeType &type)
	{
		if (str == "box") { type = BodySetup::ShapeType::Box; return true; }
		if (str == "sphere") { type = BodySetup::ShapeType::Sphere; return true; }
		if (str == "heightfield") { type = BodySetup::ShapeType::HeightField; return true; }
		if (str == "mesh") { type = BodySetup::ShapeType::Mesh; return true; }
		return false;
	}
}

BodySetup::Ptr BodySetup::Create()
{
	return std::make_shared<BodySetup>();
}

BodySetup::BodySetup()
{
	m_TypeIndex = typeid(BodySetup);
}

BodySetup::~BodySetup()
{
	// DestroyBody already ran via OnDetaching / OnOwnerDestructing /
	// UnregisterBodySetup (the world outlives components).
}

Component::Ptr BodySetup::Clone() const
{
	auto clone = BodySetup::Create();
	clone->m_ShapeType = m_ShapeType;
	clone->m_MotionType = m_MotionType;
	clone->m_CollisionMeshName = m_CollisionMeshName;
	clone->m_HalfExtents = m_HalfExtents;
	clone->m_Radius = m_Radius;
	clone->m_Mass = m_Mass;
	clone->m_Friction = m_Friction;
	clone->m_Restitution = m_Restitution;
	return clone;
}

bool BodySetup::Load(const void* wrapper, bool object)
{
	if (object && !IsObject(wrapper))
	{
		FURYE << "BodySetup: json node is not an object!";
		return false;
	}

	std::string str;
	if (!LoadMemberValue(wrapper, "type", str) || str != "BodySetup")
	{
		FURYE << "BodySetup: invalid type " << str << "!";
		return false;
	}

	if (LoadMemberValue(wrapper, "shape_type", str))
	{
		if (!StringToShapeType(str, m_ShapeType))
			FURYW << "BodySetup: unknown shape_type '" << str << "', keeping mesh.";
	}
	str.clear();

	if (LoadMemberValue(wrapper, "motion_type", str))
		m_MotionType = (str == "dynamic") ? MotionType::Dynamic : MotionType::Static;
	str.clear();

	LoadMemberValue(wrapper, "collision_mesh", m_CollisionMeshName);
	LoadMemberValue(wrapper, "half_extents", m_HalfExtents);
	LoadMemberValue(wrapper, "radius", m_Radius);
	LoadMemberValue(wrapper, "mass", m_Mass);
	LoadMemberValue(wrapper, "friction", m_Friction);
	LoadMemberValue(wrapper, "restitution", m_Restitution);

	return true;
}

void BodySetup::Save(void* wrapper, bool object)
{
	if (object)
		StartObject(wrapper);

	SaveKey(wrapper, "type");
	SaveValue(wrapper, "BodySetup");

	SaveKey(wrapper, "shape_type");
	SaveValue(wrapper, ShapeTypeToString(m_ShapeType));

	SaveKey(wrapper, "motion_type");
	SaveValue(wrapper, m_MotionType == MotionType::Dynamic ? "dynamic" : "static");

	SaveKey(wrapper, "collision_mesh");
	SaveValue(wrapper, m_CollisionMeshName);

	SaveKey(wrapper, "half_extents");
	SaveValue(wrapper, m_HalfExtents);

	SaveKey(wrapper, "radius");
	SaveValue(wrapper, m_Radius);

	SaveKey(wrapper, "mass");
	SaveValue(wrapper, m_Mass);

	SaveKey(wrapper, "friction");
	SaveValue(wrapper, m_Friction);

	SaveKey(wrapper, "restitution");
	SaveValue(wrapper, m_Restitution);

	if (object)
		EndObject(wrapper);
}

void BodySetup::SetShapeType(ShapeType type)
{
	if (m_ShapeType == type)
		return;
	m_ShapeType = type;
	AutoFitFromMesh();
}

void BodySetup::SetMotionType(MotionType type)
{
	m_MotionType = type;
}

std::shared_ptr<Mesh> BodySetup::ResolveCollisionMesh() const
{
	if (!m_CollisionMeshName.empty())
	{
		if (Scene::Active)
			return Scene::Manager()->Get<Mesh>(m_CollisionMeshName);
		return nullptr;
	}

	if (auto node = m_Owner.lock())
		if (auto render = node->GetComponent<MeshRender>())
			return render->GetMesh();

	return nullptr;
}

void BodySetup::AutoFitFromMesh()
{
	auto mesh = ResolveCollisionMesh();
	if (!mesh)
		return;

	const BoxBounds aabb = mesh->GetAABB();
	const Vector4 half = (aabb.GetMax() - aabb.GetMin()) * 0.5f;
	m_HalfExtents = Vector4(std::max(half.x, 0.5f), std::max(half.y, 0.5f), std::max(half.z, 0.5f), 0.0f);
	m_Radius = std::max({ half.x, half.y, half.z, 0.5f });
}

void BodySetup::OnAttaching(const std::shared_ptr<SceneNode> &node)
{
	Component::OnAttaching(node);

	if (!PhysicsWorld::Exists())
		return;

	auto self = std::static_pointer_cast<BodySetup>(node->GetComponent(typeid(BodySetup)));
	if (self)
		PhysicsWorld::Instance()->RegisterBodySetup(self);
}

void BodySetup::OnDetaching(const std::shared_ptr<SceneNode> &node)
{
	Component::OnDetaching(node);

	if (!PhysicsWorld::Exists())
		return;

	if (auto self = std::static_pointer_cast<BodySetup>(node->GetComponent(typeid(BodySetup))))
		PhysicsWorld::Instance()->UnregisterBodySetup(self);
	else
		DestroyBody();
}

void BodySetup::OnOwnerDestructing(SceneNode &node)
{
	DestroyBody();
}

void BodySetup::CreateBody()
{
	if (HasBody())
		return;

	if (!PhysicsWorld::Exists() || !PhysicsWorld::Instance()->IsSimulationEnabled())
		return;

	auto node = m_Owner.lock();
	if (!node)
		return;

	JPH::PhysicsSystem *system = PhysicsWorld::Instance()->GetSystem();
	if (system == nullptr)
		return;

	// Decompose the world matrix; the piecewise GetWorld* getters are
	// scale-polluted under scaled ancestors.
	Vector4 worldPos, worldScale;
	Quaternion worldRot;
	const Matrix4 worldMatrix = node->GetWorldMatrix();
	MathUtil::Decompose(worldMatrix, worldPos, worldRot, worldScale);

	JPH::ShapeSettings::ShapeResult shapeResult;
	const bool dynamic = (m_MotionType == MotionType::Dynamic);

	if (m_ShapeType == ShapeType::Mesh)
	{
		auto mesh = ResolveCollisionMesh();
		if (!mesh || mesh->Positions.Data.empty())
		{
			FURYE << "BodySetup: node '" << node->GetName() << "' has no mesh with CPU vertex data.";
			return;
		}

		// Submesh indices share the mesh's vertex buffer - plain concat.
		std::vector<unsigned int> indices = mesh->Indices.Data;
		for (unsigned int s = 0; s < mesh->GetSubMeshCount(); ++s)
		{
			if (auto sub = mesh->GetSubMeshAt(s))
				indices.insert(indices.end(), sub->Indices.Data.begin(), sub->Indices.Data.end());
		}
		if (indices.size() < 3)
		{
			// VERIFIED 2026-08: SubMesh::DeleteRawData has no callers in the
			// engine - CPU data lives for the whole process, so this guard
			// only trips on meshes loaded without indices (corrupt assets).
			FURYE << "BodySetup: mesh '" << mesh->GetName() << "' has no CPU index data.";
			return;
		}

		const auto &positions = mesh->Positions.Data;
		const unsigned int vertexCount = static_cast<unsigned int>(positions.size() / 3);

		if (dynamic)
		{
			// Jolt MeshShape is static-only - dynamic mesh bodies fall back
			// to a convex hull with world scale baked in.
			JPH::Array<JPH::Vec3> points;
			points.reserve(vertexCount);
			for (unsigned int i = 0; i < vertexCount; ++i)
				points.emplace_back(positions[i * 3 + 0] * worldScale.x,
					positions[i * 3 + 1] * worldScale.y, positions[i * 3 + 2] * worldScale.z);

			shapeResult = JPH::ConvexHullShapeSettings(points.data(),
				static_cast<int>(points.size())).Create();
			FURYW << "BodySetup: dynamic mesh body on '" << node->GetName()
				<< "' uses a convex hull of '" << mesh->GetName() << "'.";
		}
		else
		{
			// Static: bake the FULL world matrix into the vertices so the
			// body sits at identity - scaled ancestors become irrelevant.
			JPH::VertexList vertices;
			vertices.reserve(vertexCount);
			for (unsigned int i = 0; i < vertexCount; ++i)
			{
				const Vector4 world = worldMatrix.Multiply(
					Vector4(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2], 1.0f));
				vertices.push_back(JPH::Float3(world.x, world.y, world.z));
			}

			JPH::IndexedTriangleList triangles;
			triangles.reserve(indices.size() / 3);
			for (size_t i = 0; i + 2 < indices.size(); i += 3)
				triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));

			shapeResult = JPH::MeshShapeSettings(vertices, triangles).Create();
		}
	}
	else if (m_ShapeType == ShapeType::Box)
	{
		shapeResult = JPH::BoxShapeSettings(JPH::Vec3(
			m_HalfExtents.x * std::abs(worldScale.x),
			m_HalfExtents.y * std::abs(worldScale.y),
			m_HalfExtents.z * std::abs(worldScale.z))).Create();
	}
	else if (m_ShapeType == ShapeType::HeightField)
	{
		if (dynamic)
		{
			FURYW << "BodySetup: heightfield is static-only, skipping body on '" << node->GetName() << "'.";
			return;
		}

		auto terrain = node->GetComponent<Terrain>();
		if (!terrain || !terrain->HasHeights())
		{
			FURYE << "BodySetup: heightfield shape on '" << node->GetName()
				<< "' needs a sibling Terrain with loaded heights.";
			return;
		}

		if (std::abs(worldScale.x - 1.0f) > 0.001f || std::abs(worldScale.y - 1.0f) > 0.001f
			|| std::abs(worldScale.z - 1.0f) > 0.001f)
		{
			FURYW << "BodySetup: heightfield on '" << node->GetName()
				<< "' ignores node scale; size the terrain via its world size.";
		}

		// local frame: offset(-sx/2, 0, -sz/2), scale(cellX, 1, cellZ);
		// heights already decoded to cm. The body carries the world
		// transform (no vertex baking for heightfields).
		const int N = terrain->GetResolution();
		const float cellX = terrain->GetWorldSizeX() / (N - 1);
		const float cellZ = terrain->GetWorldSizeZ() / (N - 1);
		shapeResult = JPH::HeightFieldShapeSettings(
			terrain->GetHeights().data(),
			JPH::Vec3(-terrain->GetWorldSizeX() * 0.5f, 0.0f, -terrain->GetWorldSizeZ() * 0.5f),
			JPH::Vec3(cellX, 1.0f, cellZ),
			static_cast<JPH::uint32>(N)).Create();
	}
	else
	{
		const float scale = std::max({ std::abs(worldScale.x), std::abs(worldScale.y), std::abs(worldScale.z) });
		shapeResult = JPH::SphereShapeSettings(m_Radius * scale).Create();
	}

	if (!shapeResult.IsValid() || shapeResult.HasError())
	{
		FURYE << "BodySetup: shape creation failed for '" << node->GetName()
			<< "': " << shapeResult.GetError().c_str();
		return;
	}

	// Static mesh bodies baked the world transform into their vertices.
	const bool bakedWorld = (m_ShapeType == ShapeType::Mesh && !dynamic);
	const JPH::RVec3 bodyPos = bakedWorld
		? JPH::RVec3::sZero()
		: JPH::RVec3(worldPos.x, worldPos.y, worldPos.z);
	const JPH::Quat bodyRot = bakedWorld
		? JPH::Quat::sIdentity()
		: JPH::Quat(worldRot.x, worldRot.y, worldRot.z, worldRot.w);

	JPH::BodyCreationSettings settings(shapeResult.Get(),
		bodyPos, bodyRot,
		dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
		dynamic ? cLayerMoving : cLayerNonMoving);
	settings.mFriction = m_Friction;
	settings.mRestitution = m_Restitution;
	if (dynamic)
	{
		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		settings.mMassPropertiesOverride.mMass = std::max(m_Mass, 0.001f);
	}

	JPH::BodyInterface &bodies = system->GetBodyInterface();
	JPH::BodyID id = bodies.CreateAndAddBody(settings,
		dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

	if (id.IsInvalid())
	{
		FURYE << "BodySetup: body creation failed for '" << node->GetName() << "' (body limit?).";
		return;
	}

	m_BodyID = id.GetIndexAndSequenceNumber();

	// Seed interpolation state so the first SyncFromPhysics is a no-op.
	const JPH::RVec3 pos = bodies.GetPosition(id);
	const JPH::Quat rot = bodies.GetRotation(id);
	m_PrevPos = m_CurrPos = Vector4(static_cast<float>(pos.GetX()),
		static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ()), 1.0f);
	m_PrevRot = m_CurrRot = Quaternion(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW());
}

void BodySetup::DestroyBody()
{
	if (!HasBody())
		return;

	if (PhysicsWorld::Exists() && PhysicsWorld::Instance()->GetSystem() != nullptr)
	{
		JPH::BodyInterface &bodies = PhysicsWorld::Instance()->GetSystem()->GetBodyInterface();
		const JPH::BodyID id(m_BodyID);
		bodies.RemoveBody(id);
		bodies.DestroyBody(id);
	}
	m_BodyID = kInvalidBodyID;
}

void BodySetup::CapturePhysicsState()
{
	if (!HasBody() || m_MotionType != MotionType::Dynamic)
		return;

	JPH::BodyInterface &bodies = PhysicsWorld::Instance()->GetSystem()->GetBodyInterface();
	const JPH::BodyID id(m_BodyID);

	m_PrevPos = m_CurrPos;
	m_PrevRot = m_CurrRot;

	const JPH::RVec3 pos = bodies.GetPosition(id);
	const JPH::Quat rot = bodies.GetRotation(id);
	m_CurrPos = Vector4(static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()),
		static_cast<float>(pos.GetZ()), 1.0f);
	m_CurrRot = Quaternion(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW());
}

void BodySetup::SyncFromPhysics(float alpha)
{
	if (!HasBody() || m_MotionType != MotionType::Dynamic)
		return;

	auto node = m_Owner.lock();
	if (!node)
		return;

	const Vector4 pos = m_PrevPos + (m_CurrPos - m_PrevPos) * alpha;
	const Quaternion rot = m_PrevRot.Slerp(m_CurrRot, alpha);

	// World to local via parent-world inverse + Decompose (scaled-ancestor
	// trap). The decomposed scale reflects the parent; keep the node's
	// authored local scale instead.
	Matrix4 bodyWorld;
	bodyWorld.Identity();
	bodyWorld.AppendTranslation(pos);
	bodyWorld.AppendRotation(rot);

	Matrix4 parentWorld;
	parentWorld.Identity();
	if (auto parent = node->GetParent())
		parentWorld = parent->GetWorldMatrix();

	const Matrix4 local = parentWorld.Inverse() * bodyWorld;

	Vector4 localPos, ignoredScale;
	Quaternion localRot;
	MathUtil::Decompose(local, localPos, localRot, ignoredScale);

	node->SetLocalPosition(localPos);
	node->SetLocalRoattion(localRot);
	node->Recompose(false);
}
