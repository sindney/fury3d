#include <stack>

#include "Fury/Log.h"
#include "Fury/GLLoader.h"
#include "Fury/Mesh.h"
#include "Fury/SceneNode.h"
#include "Fury/Joint.h"

namespace fury
{
	// SubMesh class

	SubMesh::Ptr SubMesh::Create()
	{
		return std::make_shared<SubMesh>();
	}

	SubMesh::SubMesh() :
		m_VAO(0), Indices("vertex_index", GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW),
		m_TypeIndex(typeid(SubMesh))
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
		Indices("vertex_index", GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW),
		Positions("vertex_position", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		Normals("vertex_normal", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		Tangents("vertex_tangent", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		UVs("vertex_uv", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		IDs("bone_ids", GL_ARRAY_BUFFER, GL_STATIC_DRAW),
		Weights("bone_weights", GL_ARRAY_BUFFER, GL_STATIC_DRAW)
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

		// Per-vertex skin data — optional. Absent on static meshes; present (with the
		// joints array below) on skinned meshes. Each vertex carries 4 bone indices
		// and 3 explicit weights; the 4th weight is implicit (1 - sum of the first 3).
		LoadArray(wrapper, "bone_ids", IDs.Data);
		LoadArray(wrapper, "bone_weights", Weights.Data);

		// Joint tree — flat array; parent links rebuilt below from explicit indices.
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
			loaded_joints.push_back(joint);
			m_Joints.push_back(joint);
			m_JointMap[joint_name] = joint;
			return true;
		});
		// Second pass: wire parent → first_child / sibling links by index.
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
			// Build name→index map so joint.parent can be saved as an integer index
			// (matching the rebuild-on-load contract above).
			std::unordered_map<std::string, int> name_to_index;
			for (unsigned int i = 0; i < m_Joints.size(); ++i)
				name_to_index[m_Joints[i]->GetName()] = static_cast<int>(i);

			SaveKey(wrapper, "joints");
			SaveArray(wrapper, m_Joints.size(), [&](unsigned int index)
			{
				const auto &joint = m_Joints[index];
				StartObject(wrapper);
				SaveKey(wrapper, "name");
				SaveValue(wrapper, joint->GetName());
				SaveKey(wrapper, "local_matrix");
				SaveValue(wrapper, joint->GetLocalMatrix());
				SaveKey(wrapper, "offset_matrix");
				SaveValue(wrapper, joint->GetOffsetMatrix());
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
		SaveArray(wrapper, m_SubMeshes.size(), [&](unsigned int index)
		{
			SaveArray(wrapper, m_SubMeshes[index]->Indices.Data);
		});

		SaveKey(wrapper, "aabb");
		SaveValue(wrapper, m_AABB);

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
		return m_SubMeshes.size();
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
		return m_Joints.size();
	}

	std::shared_ptr<Joint> Mesh::GetRootJoint() const
	{
		return m_RootJoint;
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

		m_Dirty = Indices.GetDirty() || Positions.GetDirty();

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
			unsigned int numTriangles = Indices.Data.size() / 3;

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
		else
		{
			unsigned int triangleCount = Positions.Data.size() / 3;
			for (unsigned int i = 0; i < triangleCount; i++)
			{
				unsigned int index = i * 3;
				m_AABB.Encapsulate(Vector4(Positions.Data[index], Positions.Data[index + 1], 
					Positions.Data[index + 2], 1.0f));
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
}