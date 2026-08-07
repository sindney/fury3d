#include <stack>
#include <cstdio>

#include "Fury/Log.h"
#include "Fury/GLLoader.h"
#include "Fury/Mesh.h"
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
		m_TypeIndex(typeid(SubMesh)), m_VAO(0),
		Indices("vertex_index", GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW)
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

		if (m_VAO != 0)
		{
			glDeleteVertexArrays(1, &m_VAO);
			m_VAO = 0;
		}

		glGenVertexArrays(1, &m_VAO);
		if (m_VAO == 0)
		{
			m_Dirty = true;
			FURYW << "Failed to glGenVertexArrays!";
		}
	}

	void SubMesh::DeleteBuffer()
	{
		m_Dirty = true;

		if (m_VAO != 0)
		{
			glDeleteVertexArrays(1, &m_VAO);
			m_VAO = 0;
		}

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

	Mesh::Mesh(const std::string &name) : Entity(name), m_VAO(0),
		Positions("vertex_position", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		Normals("vertex_normal", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		Tangents("vertex_tangent", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		UVs("vertex_uv", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		Weights("bone_weights", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		IDs("bone_ids", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		Indices("vertex_index", GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW)
	{
		m_TypeIndex = typeid(Mesh);
	};

	Mesh::~Mesh()
	{
		DeleteBuffer();
		FURYD << "Mesh: " << m_Name << " Destoried!";
	}

	bool Mesh::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		if (!LoadArray(wrapper, "positions", Positions.Data))
		{
			FURYE << "positions not found!";
			return false;
		}

		LoadArray(wrapper, "normals", Normals.Data);
		LoadArray(wrapper, "tangents", Tangents.Data);
		LoadArray(wrapper, "uvs", UVs.Data);

		// Per-vertex skin data -- optional. Absent on static meshes; present (with the
		// joints array below) on skinned meshes. Each vertex carries 4 bone indices
		// and 3 explicit weights; the 4th weight is implicit (1 - sum of the first 3).
		LoadArray(wrapper, "bone_ids", IDs.Data);
		LoadArray(wrapper, "bone_weights", Weights.Data);

		// Joint tree -- flat array; parent links rebuilt below from explicit indices.
		// Tree shape (first-child / sibling pointers) is reconstructed from parent indices.
		std::vector<Joint::Ptr> loaded_joints;
		LoadArray(wrapper, "joints", [&](const void* node) -> bool
		{
			std::string joint_name;
			Matrix4 local, offset;
			if (!LoadMemberValue(node, "name", joint_name)) return false;
			LoadMemberValue(node, "local_matrix", local);
			LoadMemberValue(node, "offset_matrix", offset);
			auto joint = Joint::Create(joint_name, nullptr);
			joint->SetLocalMatrix(local);
			joint->SetOffsetMatrix(offset);
			std::string scene_node_uuid;
			if (LoadMemberValue(node, "scene_node_uuid", scene_node_uuid) && !scene_node_uuid.empty())
				joint->SetSceneNodeUUID(scene_node_uuid);
			loaded_joints.push_back(joint);
			m_Joints.push_back(joint);
			m_JointMap[joint_name] = joint;
			return true;
		});
		// Second pass: wire parent -> first_child / sibling links by index.
		// LoadArray<callback> doesn't expose the index, so we re-scan in parallel with
		// loaded_joints to read each entry's parent and link it back.
		{
			unsigned int idx = 0;
			LoadArray(wrapper, "joints", [&](const void* node) -> bool
			{
				int parent_index = -1;
				LoadMemberValue(node, "parent", parent_index);
				if (parent_index >= 0 && parent_index < static_cast<int>(loaded_joints.size()))
				{
					auto child = loaded_joints[idx];
					auto parent = loaded_joints[parent_index];
					child->SetParent(parent);
					auto existing = parent->GetFirstChild();
					child->SetSibling(existing);
					parent->SetFirstChild(child);
				}
				++idx;
				return true;
			});
		}
		std::string root_joint_name;
		if (LoadMemberValue(wrapper, "root_joint", root_joint_name))
		{
			auto it = m_JointMap.find(root_joint_name);
			if (it != m_JointMap.end()) m_RootJoint = it->second;
		}

		if (!LoadArray(wrapper, "indices", Indices.Data))
		{
			FURYE << "indices not found!";
			return false;
		}

		LoadMemberValue(wrapper, "cast_shadows", m_CastShadows);

		// model aabb
		LoadMemberValue(wrapper, "aabb", m_AABB);

		// subMeshes
		if (!LoadArray(wrapper, "submeshes", [&](const void* node) -> bool
		{
			auto subMesh = SubMesh::Create();
			if (LoadArray(node, subMesh->Indices.Data))
			{
				AddSubMesh(subMesh);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			return false;
		}

		// LOD chain (LOD 1..N). Optional -- pre-LOD scene files don't
		// have it. Each entry in `lod_meshes` is a full Mesh JSON
		// object; `lod_thresholds` is the parallel screen-coverage
		// array.
		LoadArray(wrapper, "lod_thresholds", m_LodThresholds);
		LoadArray(wrapper, "lod_meshes", [&](const void* node) -> bool
		{
			auto lod = Mesh::Create("");
			if (lod->Load(node, false))
			{
				m_LodMeshes.push_back(lod);
				return true;
			}
			FURYE << "Mesh::Load: failed to load an entry in 'lod_meshes'";
			return false;
		});

		return true;
	}

	void Mesh::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "cast_shadows");
		SaveValue(wrapper, m_CastShadows);

		SaveKey(wrapper, "positions");
		SaveArray(wrapper, Positions.Data);

		if (Normals.Data.size() > 0)
		{
			SaveKey(wrapper, "normals");
			SaveArray(wrapper, Normals.Data);
		}

		if (Tangents.Data.size() > 0)
		{
			SaveKey(wrapper, "tangents");
			SaveArray(wrapper, Tangents.Data);
		}

		if (UVs.Data.size() > 0)
		{
			SaveKey(wrapper, "uvs");
			SaveArray(wrapper, UVs.Data);
		}

		// Per-vertex skin data + joint tree. Emitted only when present so static-mesh
		// scene files remain byte-identical to pre-skin-roundtrip output.
		if (IDs.Data.size() > 0)
		{
			SaveKey(wrapper, "bone_ids");
			SaveArray(wrapper, IDs.Data);
		}
		if (Weights.Data.size() > 0)
		{
			SaveKey(wrapper, "bone_weights");
			SaveArray(wrapper, Weights.Data);
		}
		if (!m_Joints.empty())
		{
			// Build name->index map so joint.parent can be saved as an integer index
			// (matching the rebuild-on-load contract above).
			std::unordered_map<std::string, int> name_to_index;
			for (unsigned int i = 0; i < m_Joints.size(); ++i)
				name_to_index[m_Joints[i]->GetName()] = static_cast<int>(i);

			SaveKey(wrapper, "joints");
			SaveArray(wrapper, static_cast<unsigned int>(m_Joints.size()), [&](unsigned int index)
			{
				const auto &joint = m_Joints[index];
				StartObject(wrapper);
				SaveKey(wrapper, "name");
				SaveValue(wrapper, joint->GetName());
			SaveKey(wrapper, "local_matrix");
			SaveValue(wrapper, joint->GetLocalMatrix());
			SaveKey(wrapper, "offset_matrix");
			SaveValue(wrapper, joint->GetOffsetMatrix());
			// Persist linked SceneNode UUID for unambiguous re-link on load.
			const auto &uuid = joint->GetSceneNodeUUID();
			if (!uuid.empty())
			{
				SaveKey(wrapper, "scene_node_uuid");
				SaveValue(wrapper, uuid);
			}
			int parent_index = -1;
			auto parent = joint->GetParent();
			if (parent)
			{
				auto it = name_to_index.find(parent->GetName());
				if (it != name_to_index.end()) parent_index = it->second;
			}
			SaveKey(wrapper, "parent");
			SaveValue(wrapper, parent_index);
			EndObject(wrapper);
		});

			if (m_RootJoint)
			{
				SaveKey(wrapper, "root_joint");
				SaveValue(wrapper, m_RootJoint->GetName());
			}
		}

		SaveKey(wrapper, "indices");
		SaveArray(wrapper, Indices.Data);

		SaveKey(wrapper, "submeshes");
		SaveArray(wrapper, static_cast<unsigned int>(m_SubMeshes.size()), [&](unsigned int index)
		{
			SaveArray(wrapper, m_SubMeshes[index]->Indices.Data);
		});

		SaveKey(wrapper, "aabb");
		SaveValue(wrapper, m_AABB);

		// LOD chain (LOD 1..N). Written inline as a sub-object so a
		// "loded mesh" is a single asset on disk. Absent when empty
		// (pre-LOD scene files load unchanged).
		if (!m_LodMeshes.empty())
		{
			SaveKey(wrapper, "lod_meshes");
			SaveArray(wrapper, static_cast<unsigned int>(m_LodMeshes.size()), [&](unsigned int index)
			{
				m_LodMeshes[index]->Save(wrapper, true);
			});
			SaveKey(wrapper, "lod_thresholds");
			SaveArray(wrapper, m_LodThresholds);
		}

		if (object)
			EndObject(wrapper);
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
		Positions.UpdateBuffer();
		Normals.UpdateBuffer();
		Tangents.UpdateBuffer();
		UVs.UpdateBuffer();
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

		if (m_VAO != 0)
		{
			glDeleteVertexArrays(1, &m_VAO);
			m_VAO = 0;
		}

		glGenVertexArrays(1, &m_VAO);
		if (m_VAO == 0)
		{
			m_Dirty = true;
			FURYW << "Failed to glGenVertexArrays!";
		}
		else
		{
			for (auto subMesh : m_SubMeshes)
				if (subMesh != nullptr)
					subMesh->UpdateBuffer();
		}
	}

	void Mesh::DeleteBuffer()
	{
		m_Dirty = true;

		if (m_VAO != 0)
		{
			glDeleteVertexArrays(1, &m_VAO);
			m_VAO = 0;
		}
		Positions.DeleteBuffer();
		Normals.DeleteBuffer();
		Tangents.DeleteBuffer();
		UVs.DeleteBuffer();
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
							 const std::vector<float> &thresholds)
	{
		if (meshes.size() != thresholds.size())
		{
			FURYE << "Mesh::SetLodMeshes: mesh count (" << meshes.size()
				  << ") != threshold count (" << thresholds.size() << ")";
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
	}

	void Mesh::ClearLodChain()
	{
		m_LodMeshes.clear();
		m_LodThresholds.clear();
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

	void LodGroup::Save(void* wrapper, bool object)
	{
		// Serializable stub -- the MeshRender Save path calls
		// SaveMeshData (non-virtual) directly to write the
		// sub-object. This virtual exists only to make LodGroup
		// concrete; it should not be invoked in production paths.
		(void)wrapper; (void)object;
	}

	bool LodGroup::Load(const void* wrapper, bool object)
	{
		// Same as Save -- exists only to satisfy the Serializable
		// abstract. Use LoadFromManager from MeshRender::Load.
		(void)wrapper; (void)object;
		FURYE << "LodGroup::Load called without a manager; use LoadFromManager";
		return false;
	}

	void LodGroup::SaveMeshData(void* wrapper, bool object) const
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "thresholds");
		// Serializable::SaveArray takes a non-const vector ref; copy
		// out of the const member for the call.
		std::vector<float> thresholds = m_Thresholds;
		Serializable::SaveArray(wrapper, thresholds);
		SaveKey(wrapper, "meshes");
		StartArray(wrapper);
		for (const auto &m : m_Meshes)
		{
			if (m) SaveValue(wrapper, m->GetName());
			else SaveValue(wrapper, std::string{});
		}
		EndArray(wrapper);
		if (object) EndObject(wrapper);
	}

	bool LodGroup::LoadFromManager(const void* wrapper, const std::shared_ptr<EntityManager> &manager, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "LodGroup::LoadFromManager: node is not an object";
			return false;
		}

		std::vector<float> thresholds;
		std::vector<std::string> names;

		if (!LoadArray(wrapper, "thresholds", thresholds))
		{
			FURYE << "LodGroup::LoadFromManager: thresholds not found";
			return false;
		}
		if (!LoadArray(wrapper, "meshes", names))
		{
			FURYE << "LodGroup::LoadFromManager: meshes not found";
			return false;
		}
		if (thresholds.size() != names.size())
		{
			FURYE << "LodGroup::LoadFromManager: mesh count (" << names.size()
				  << ") != threshold count (" << thresholds.size() << ")";
			return false;
		}

		std::vector<std::shared_ptr<Mesh>> meshes;
		meshes.reserve(names.size());
		for (const auto &n : names)
		{
			auto m = manager->Get<Mesh>(n);
			if (!m)
			{
				FURYE << "LodGroup::LoadFromManager: mesh '" << n << "' not found";
				return false;
			}
			meshes.push_back(m);
		}

		m_Meshes = std::move(meshes);
		m_Thresholds = std::move(thresholds);
		return true;
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