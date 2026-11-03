#include <cstdio>

#include "Fury/Log.h"
#include "Fury/Archive.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/RenderThread.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Joint.h"
#include "Fury/EntityManager.h"
#include "Fury/Serializable.h"

namespace fury
{
	// SubMesh class

	SubMesh::Ptr SubMesh::Create()
	{
		return std::make_shared<SubMesh>();
	}

	SubMesh::SubMesh() :
		m_TypeIndex(typeid(SubMesh)),
		Indices("vertex_index", RHIBufferType::INDEX, RHIBufferUsage::STATIC)
	{

	}

	SubMesh::~SubMesh()
	{
		DeleteBuffer();
		FURYD << "SubMesh Destoried!";
	}

	void SubMesh::UpdateBuffer()
	{
		Indices.UpdateBuffer();

		m_Dirty = Indices.GetDirty();
	}

	void SubMesh::DeleteBuffer()
	{
		m_Dirty = true;

		Indices.DeleteBuffer();
	}

	void SubMesh::DeleteRawData()
	{
		Indices.Data.clear();
	}

	std::type_index SubMesh::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	// Mesh class

	Mesh::Ptr Mesh::Create(const std::string &name)
	{
		return std::make_shared<Mesh>(name);
	}

	Mesh::Mesh(const std::string &name) : Entity(name),
		Positions("vertex_position", RHIBufferType::VERTEX, RHIBufferUsage::STATIC),
		Normals("vertex_normal", RHIBufferType::VERTEX, RHIBufferUsage::STATIC),
		Tangents("vertex_tangent", RHIBufferType::VERTEX, RHIBufferUsage::STATIC),
		UVs("vertex_uv", RHIBufferType::VERTEX, RHIBufferUsage::STATIC),
		Colors("vertex_color", RHIBufferType::VERTEX, RHIBufferUsage::STATIC),
		Weights("bone_weights", RHIBufferType::VERTEX, RHIBufferUsage::STATIC),
		IDs("bone_ids", RHIBufferType::VERTEX, RHIBufferUsage::STATIC),
		Indices("vertex_index", RHIBufferType::INDEX, RHIBufferUsage::STATIC)
	{
		m_TypeIndex = typeid(Mesh);
	};

	Mesh::~Mesh()
	{
		DeleteBuffer();
		FURYD << "Mesh: " << m_Name << " Destoried!";
	}

	void Mesh::Serialize(Archive& ar)
	{
		Entity::SerializeFields(ar);

		ar << Optional("cast_shadows", m_CastShadows, false);

		ar << Required("positions", Positions.Data);
		ar << Conditional("normals", Normals.Data, !Normals.Data.empty());
		ar << Conditional("tangents", Tangents.Data, !Tangents.Data.empty());
		ar << Conditional("uvs", UVs.Data, !UVs.Data.empty());
		ar << Conditional("colors", Colors.Data, !Colors.Data.empty());
		ar << Conditional("bone_ids", IDs.Data, !IDs.Data.empty());
		ar << Conditional("bone_weights", Weights.Data, !Weights.Data.empty());

		// Joint tree: flat array; parent links rebuilt from explicit
		// indices after all joints exist.
		if (ar.EnterArray("joints", ArchiveFieldPolicy::Optional, !m_Joints.empty()))
		{
			if (ar.IsLoading())
			{
				struct JointRecord
				{
					std::string name;
					Matrix4 local;
					Matrix4 offset;
					std::string uuid;
					int parent = -1;
				};
				std::vector<JointRecord> records;
				std::size_t count = ar.GetArraySize();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.SetArrayIndex(i);
					if (ar.EnterObject())
					{
						JointRecord record;
						ar << Required("name", record.name);
						ar << Optional("local_matrix", record.local);
						ar << Optional("offset_matrix", record.offset);
						std::string uuid;
						ar << Optional("scene_node_uuid", uuid);
						if (!uuid.empty())
							record.uuid = uuid;
						ar << Optional("parent", record.parent);
						records.push_back(record);
						ar.LeaveObject();
					}
				}

				std::vector<Joint::Ptr> loadedJoints;
				for (auto &record : records)
				{
					auto joint = Joint::Create(record.name, nullptr);
					joint->SetLocalMatrix(record.local);
					joint->SetOffsetMatrix(record.offset);
					if (!record.uuid.empty())
						joint->SetSceneNodeUUID(record.uuid);
					loadedJoints.push_back(joint);
					m_Joints.push_back(joint);
					m_JointMap[record.name] = joint;
				}
				for (std::size_t i = 0; i < records.size(); ++i)
				{
					int parentIndex = records[i].parent;
					if (parentIndex >= 0 && parentIndex < static_cast<int>(loadedJoints.size()))
					{
						auto child = loadedJoints[i];
						auto parent = loadedJoints[parentIndex];
						child->SetParent(parent);
						auto existing = parent->GetFirstChild();
						child->SetSibling(existing);
						parent->SetFirstChild(child);
					}
				}
			}
			else
			{
				std::unordered_map<std::string, int> nameToIndex;
				for (unsigned int i = 0; i < m_Joints.size(); ++i)
					nameToIndex[m_Joints[i]->GetName()] = static_cast<int>(i);

				std::size_t count = m_Joints.size();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.EnterObject();
					const auto &joint = m_Joints[i];
					std::string name = joint->GetName();
					Matrix4 local = joint->GetLocalMatrix();
					Matrix4 offset = joint->GetOffsetMatrix();
					std::string uuid = joint->GetSceneNodeUUID();
					int parentIndex = -1;
					if (auto parent = joint->GetParent())
					{
						auto it = nameToIndex.find(parent->GetName());
						if (it != nameToIndex.end())
							parentIndex = it->second;
					}
					ar << Required("name", name);
					ar << Required("local_matrix", local);
					ar << Required("offset_matrix", offset);
					ar << Conditional("scene_node_uuid", uuid, !uuid.empty());
					ar << Required("parent", parentIndex);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}

		std::string rootJoint = m_RootJoint ? m_RootJoint->GetName() : "";
		ar << Conditional("root_joint", rootJoint, m_RootJoint != nullptr);
		if (ar.IsLoading() && !rootJoint.empty())
		{
			auto it = m_JointMap.find(rootJoint);
			if (it != m_JointMap.end())
				m_RootJoint = it->second;
		}

		ar << Required("indices", Indices.Data);

		ar << Optional("aabb", m_AABB);

		// submeshes: array of index arrays. Required -- always emitted.
		if (ar.EnterArray("submeshes", ArchiveFieldPolicy::Required))
		{
			if (ar.IsLoading())
			{
				std::size_t count = ar.GetArraySize();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.SetArrayIndex(i);
					auto subMesh = SubMesh::Create();
					ar.Transfer(nullptr, subMesh->Indices.Data, ArchiveFieldPolicy::Required, true);
					AddSubMesh(subMesh);
				}
			}
			else
			{
				std::size_t count = m_SubMeshes.size();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.Transfer(nullptr, m_SubMeshes[i]->Indices.Data,
						ArchiveFieldPolicy::Required, true);
				}
			}
			ar.LeaveArray();
		}

		// LOD chain (LOD 1..N): inline full Mesh objects, optional.
		ar << Conditional("lod_thresholds", m_LodThresholds, !m_LodMeshes.empty());
		if (ar.EnterArray("lod_billboard_flags", ArchiveFieldPolicy::Optional,
			!m_LodBillboardFlags.empty()))
		{
			if (ar.IsLoading())
			{
				m_LodBillboardFlags.clear();
				std::size_t count = ar.GetArraySize();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.SetArrayIndex(i);
					bool flag = false;
					ar.Transfer(nullptr, flag, ArchiveFieldPolicy::Required, true);
					m_LodBillboardFlags.push_back(flag);
				}
			}
			else
			{
				for (std::size_t i = 0; i < m_LodBillboardFlags.size(); ++i)
				{
					bool flag = m_LodBillboardFlags[i];
					ar.Transfer(nullptr, flag, ArchiveFieldPolicy::Required, true);
				}
			}
			ar.LeaveArray();
		}
		std::string billboardMat;
		if (ar.IsSaving())
		{
			if (auto bbMat = m_BillboardMaterial.lock())
				billboardMat = bbMat->GetName();
		}
		ar << Conditional("lod_billboard_material", billboardMat, !billboardMat.empty());
		if (ar.EnterArray("lod_meshes", ArchiveFieldPolicy::Optional, !m_LodMeshes.empty()))
		{
			if (ar.IsLoading())
			{
				std::size_t count = ar.GetArraySize();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.SetArrayIndex(i);
					auto lod = Mesh::Create("");
					if (ar.EnterObject())
					{
						lod->Serialize(ar);
						ar.LeaveObject();
					}
					m_LodMeshes.push_back(lod);
				}
			}
			else
			{
				for (auto &lod : m_LodMeshes)
				{
					ar.EnterObject();
					lod->Serialize(ar);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}

		if (ar.IsLoading())
		{
			if (ar.IsValid() && !m_LodBillboardFlags.empty()
				&& m_LodBillboardFlags.size() != m_LodMeshes.size())
			{
				FURYE << "Mesh: lod_billboard_flags size != lod_meshes size, dropping flags!";
				m_LodBillboardFlags.clear();
			}
			if (!billboardMat.empty() && Scene::Active && Scene::Manager())
			{
				m_BillboardMaterial = Scene::Manager()->Get<Material>(billboardMat);
				if (m_BillboardMaterial.expired())
					FURYW << "Mesh: billboard material '" << billboardMat << "' not found";
			}
		}
	}

	void Mesh::AddSubMesh(const SubMesh::Ptr &subMesh)
	{
		m_SubMeshes.push_back(subMesh);
	}

	SubMesh::Ptr Mesh::GetSubMeshAt(unsigned int index) const
	{
		if (index < m_SubMeshes.size())
			return m_SubMeshes[index];
		else
			return nullptr;
	}

	unsigned int Mesh::GetSubMeshCount() const
	{
		return static_cast<int>(m_SubMeshes.size());
	}

	bool Mesh::IsSkinnedMesh() const
	{
		return m_Joints.size() > 0 && m_RootJoint != nullptr;
	}

	std::shared_ptr<Joint> Mesh::GetJoint(const std::string &name) const
	{
		auto it = m_JointMap.find(name);
		if (it != m_JointMap.end())
			return it->second;
		else
			return nullptr;
	}

	std::shared_ptr<Joint> Mesh::GetJointAt(unsigned int index) const
	{
		if (index > m_Joints.size() - 1)
			return nullptr;
		return m_Joints[index];
	}

	unsigned int Mesh::GetJointCount() const
	{
		return static_cast<int>(m_Joints.size());
	}

	std::shared_ptr<Joint> Mesh::GetRootJoint() const
	{
		return m_RootJoint;
	}

	void Mesh::SetJointTree(const std::vector<std::shared_ptr<Joint>> &joints,
		const std::shared_ptr<Joint> &root_joint)
	{
		m_Joints = joints;
		m_JointMap.clear();
		for (const auto &j : joints)
			if (j) m_JointMap[j->GetName()] = j;
		m_RootJoint = root_joint;
	}

	void Mesh::UpdateBuffer()
	{
		if (!RenderThread::Get().MayUseGL())
		{
			DispatchRHI(this, [this]() { UpdateBuffer(); });
			return;
		}
		FURY_RHI_THREAD_GUARD();
		Positions.UpdateBuffer();
		Normals.UpdateBuffer();
		Tangents.UpdateBuffer();
		UVs.UpdateBuffer();
		Colors.UpdateBuffer();
		Weights.UpdateBuffer();
		IDs.UpdateBuffer();
		Indices.UpdateBuffer();

		// For meshes with submeshes, the parent Indices buffer is
		// unused -- each submesh carries its own index data. Flipping
		// the mesh dirty just because the (empty) parent Indices is
		// dirty would make Shader::BindMesh / BindSubMesh early-return
		// and the LOD preview / submeshed mesh would render nothing.
		if (m_SubMeshes.empty())
			m_Dirty = Indices.GetDirty() || Positions.GetDirty();
		else
			m_Dirty = Positions.GetDirty();

		for (auto subMesh : m_SubMeshes)
			if (subMesh != nullptr)
				subMesh->UpdateBuffer();
	}

	void Mesh::DeleteBuffer()
	{
		m_Dirty = true;

		Positions.DeleteBuffer();
		Normals.DeleteBuffer();
		Tangents.DeleteBuffer();
		UVs.DeleteBuffer();
		Colors.DeleteBuffer();
		Weights.DeleteBuffer();
		IDs.DeleteBuffer();
		Indices.DeleteBuffer();

		for (auto subMesh : m_SubMeshes)
			if (subMesh != nullptr)
				subMesh->DeleteBuffer();
	}

	void Mesh::CalculateAABB(const Vector4& min, const Vector4& max)
	{
		m_AABB.SetMinMax(min, max);
	}

	void Mesh::CalculateAABB()
	{
		m_AABB.SetDirty(true);

		if (IsSkinnedMesh())
		{
			unsigned int numTriangles = static_cast<unsigned int>(Indices.Data.size() / 3);

			for (unsigned int i = 0; i < numTriangles; i++)
			{
				unsigned int triIndex = i * 3;
				for (unsigned int j = 0; j < 3; j++)
				{
					unsigned int current = Indices.Data[triIndex + j];
					unsigned int current3 = current * 3;
					unsigned int current4 = current * 4;

					Vector4 pos = Vector4(Positions.Data[current3], Positions.Data[current3 + 1],
						Positions.Data[current3 + 2], 1.0f);

					unsigned int ids[] = { IDs.Data[current4], IDs.Data[current4 + 1],
						IDs.Data[current4 + 2], IDs.Data[current4 + 3] };

					float weights[] = { Weights.Data[current3], Weights.Data[current3 + 1],
						Weights.Data[current3 + 2], 0.0f };
					weights[3] = 1.0f - weights[0] - weights[1] - weights[2];

					Vector4 blended = m_Joints[ids[0]]->GetFinalMatrix().Multiply(pos) * weights[0] +
						m_Joints[ids[1]]->GetFinalMatrix().Multiply(pos) * weights[1] +
						m_Joints[ids[2]]->GetFinalMatrix().Multiply(pos) * weights[2] +
						m_Joints[ids[3]]->GetFinalMatrix().Multiply(pos) * weights[3];

					m_AABB.Encapsulate(blended);
				}
			}
		}
		// Walk per-submesh for static meshes (LOD chain puts indices in submeshes,
		// leaving the parent Indices empty); fall back to the parent buffer
		// when there are no submeshes.
		const unsigned int sub_count = GetSubMeshCount();
		if (sub_count > 0)
		{
			for (unsigned int s = 0; s < sub_count; ++s)
			{
				auto sm = GetSubMeshAt(s);
				if (!sm) continue;
				const auto &idx = sm->Indices.Data;
				const size_t num_triangles = idx.size() / 3;
				for (size_t i = 0; i < num_triangles; ++i)
				{
					for (unsigned int j = 0; j < 3; ++j)
					{
						const unsigned int v = idx[i * 3 + j];
						const size_t v3 = static_cast<size_t>(v) * 3;
						if (v3 + 2 >= Positions.Data.size()) continue;
						m_AABB.Encapsulate(Vector4(Positions.Data[v3],
							Positions.Data[v3 + 1], Positions.Data[v3 + 2], 1.0f));
					}
				}
			}
		}
		else
		{
			// No submeshes - parent Indices must be populated.
			const size_t num_triangles = Indices.Data.size() / 3;
			for (size_t i = 0; i < num_triangles; ++i)
			{
				for (unsigned int j = 0; j < 3; ++j)
				{
					const unsigned int v = Indices.Data[i * 3 + j];
					const size_t v3 = static_cast<size_t>(v) * 3;
					if (v3 + 2 >= Positions.Data.size()) continue;
					m_AABB.Encapsulate(Vector4(Positions.Data[v3],
						Positions.Data[v3 + 1], Positions.Data[v3 + 2], 1.0f));
				}
			}
		}
	}

	BoxBounds Mesh::GetAABB() const
	{
		return m_AABB;
	}

	bool Mesh::GetCastShadows() const
	{
		return m_CastShadows;
	}

	void Mesh::SetCastShadows(bool state)
	{
		m_CastShadows = state;
	}

	// LOD chain accessors. LOD 0 is the source mesh itself; LOD
	// 1..N are stored in m_LodMeshes. The threshold for LOD 0 is
	// always 1.0 (highest detail is active whenever the model is
	// on-screen).
	unsigned int Mesh::GetLodCount() const
	{
		return static_cast<unsigned int>(m_LodMeshes.size()) + 1;
	}

	std::shared_ptr<Mesh> Mesh::GetLodMesh(unsigned int i) const
	{
		if (i == 0) return std::const_pointer_cast<Mesh>(shared_from_this());
		if (i <= m_LodMeshes.size()) return m_LodMeshes[i - 1];
		return nullptr;
	}

	float Mesh::GetLodThreshold(unsigned int i) const
	{
		if (i == 0) return 1.0f;
		if (i <= m_LodThresholds.size()) return m_LodThresholds[i - 1];
		return 0.0f;
	}

	void Mesh::SetLodMeshes(const std::vector<std::shared_ptr<Mesh>> &meshes,
							 const std::vector<float> &thresholds,
							 const std::vector<bool> &billboardFlags)
	{
		if (meshes.size() != thresholds.size())
		{
			FURYE << "Mesh::SetLodMeshes: mesh count (" << meshes.size()
				  << ") != threshold count (" << thresholds.size() << ")";
			return;
		}
		if (!billboardFlags.empty() && billboardFlags.size() != meshes.size())
		{
			FURYE << "Mesh::SetLodMeshes: billboard flag count (" << billboardFlags.size()
				  << ") != mesh count (" << meshes.size() << ")";
			return;
		}
		for (size_t i = 1; i < thresholds.size(); ++i)
		{
			if (thresholds[i] > thresholds[i - 1])
			{
				FURYE << "Mesh::SetLodMeshes: thresholds out of order at index "
					  << i << " (" << thresholds[i - 1] << " > " << thresholds[i] << ")";
				return;
			}
		}
		m_LodMeshes = meshes;
		m_LodThresholds = thresholds;
		m_LodBillboardFlags = billboardFlags;
	}

	bool Mesh::IsLodBillboard(unsigned int i) const
	{
		if (i == 0 || m_LodBillboardFlags.empty()) return false;
		if (i <= m_LodBillboardFlags.size()) return m_LodBillboardFlags[i - 1];
		return false;
	}

	void Mesh::SetLodBillboardFlags(const std::vector<bool> &flags)
	{
		if (!flags.empty() && flags.size() != m_LodMeshes.size())
		{
			FURYE << "Mesh::SetLodBillboardFlags: flag count (" << flags.size()
				  << ") != lod mesh count (" << m_LodMeshes.size() << ")";
			return;
		}
		m_LodBillboardFlags = flags;
	}

	void Mesh::SetBillboardMaterial(const std::shared_ptr<Material> &material)
	{
		m_BillboardMaterial = material;
	}

	std::shared_ptr<Material> Mesh::GetBillboardMaterial() const
	{
		return m_BillboardMaterial.lock();
	}

	void Mesh::ClearLodChain()
	{
		m_LodMeshes.clear();
		m_LodThresholds.clear();
		m_LodBillboardFlags.clear();
	}

	// LodGroup class

	LodGroup::LodGroup(const std::vector<std::shared_ptr<Mesh>> &meshes,
					   const std::vector<float> &thresholds)
	{
		Set(meshes, thresholds);
	}

	unsigned int LodGroup::GetLodCount() const
	{
		return static_cast<unsigned int>(m_Meshes.size());
	}

	std::shared_ptr<Mesh> LodGroup::GetMesh(unsigned int i) const
	{
		if (i < m_Meshes.size()) return m_Meshes[i];
		return nullptr;
	}

	float LodGroup::GetThreshold(unsigned int i) const
	{
		if (i < m_Thresholds.size()) return m_Thresholds[i];
		return 0.0f;
	}

	bool LodGroup::IsEmpty() const
	{
		return m_Meshes.empty();
	}

	void LodGroup::Set(const std::vector<std::shared_ptr<Mesh>> &meshes,
					   const std::vector<float> &thresholds)
	{
		if (meshes.size() != thresholds.size())
		{
			FURYE << "LodGroup::Set: mesh count (" << meshes.size()
				  << ") != threshold count (" << thresholds.size() << ")";
			return;
		}
		for (size_t i = 1; i < thresholds.size(); ++i)
		{
			if (thresholds[i] > thresholds[i - 1])
			{
				FURYE << "LodGroup::Set: thresholds out of order at index "
					  << i << " (" << thresholds[i - 1] << " > " << thresholds[i]
					  << "); keeping previous state";
				return;
			}
		}
		m_Meshes = meshes;
		m_Thresholds = thresholds;
	}

	// MeshContentHash -- FNV-1a 64-bit over positions + top-level indices
	// + each submesh's indices. See header for rationale. Returns 0 on
	// a null mesh so callers can short-circuit without dereferencing.
	// We iterate the underlying std::vector<byte> rather than the typed
	// ArrayBuffer wrappers to avoid any chance of touching GPU state
	// (this is safe to call on a worker thread).
	namespace
	{
		// FNV-1a 64-bit constants. The offset basis is the official
		// FNV-1a 64-bit starting value; the prime is the official
		// 64-bit prime.
		const uint64_t kFnv1aOffset = 0xcbf29ce484222325ULL;
		const uint64_t kFnv1aPrime  = 0x100000001b3ULL;

		inline void Fnv1aAbsorbBytes(uint64_t &state, const void *data, size_t n)
		{
			const unsigned char *bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < n; ++i)
			{
				state ^= static_cast<uint64_t>(bytes[i]);
				state *= kFnv1aPrime;
			}
		}
	}

	uint64_t MeshContentHash(const Mesh* mesh)
	{
		if (!mesh) return 0;
		uint64_t h = kFnv1aOffset;
		// Positions: float3 stride, so size() * sizeof(float) bytes.
		Fnv1aAbsorbBytes(h, mesh->Positions.Data.data(),
						 mesh->Positions.Data.size() * sizeof(float));
		// Top-level indices (one shared index buffer for non-submeshes).
		Fnv1aAbsorbBytes(h, mesh->Indices.Data.data(),
						 mesh->Indices.Data.size() * sizeof(unsigned int));
		// Per-submesh indices, in submesh order, so re-ordering
		// submeshes changes the hash (matches "content fingerprint"
		// intent).
		const unsigned int n = mesh->GetSubMeshCount();
		for (unsigned int i = 0; i < n; ++i)
		{
			auto sm = mesh->GetSubMeshAt(i);
			if (!sm) continue;
			Fnv1aAbsorbBytes(h, sm->Indices.Data.data(),
							 sm->Indices.Data.size() * sizeof(unsigned int));
		}
		return h;
	}

	std::string FormatHashHex(uint64_t hash)
	{
		// 16 lowercase hex chars, no leading 0x. Use snprintf rather
		// than std::format to keep header-light and to match the rest
		// of the engine's C-style string conventions.
		char buf[17];
		std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
		return std::string(buf);
	}
}