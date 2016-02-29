#include "Debug.h"
#include "GLLoader.h"
#include "Mesh.h"
#include "SceneNode.h"

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
		LOGD << "SubMesh Destoried!";
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
			LOGW << "Failed to glGenVertexArrays!";
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
		UVs("vertex_uv", GL_ARRAY_BUFFER, GL_STATIC_DRAW)
	{
		m_TypeIndex = typeid(Mesh);
	};

	Mesh::~Mesh()
	{
		DeleteBuffer();
		LOGD << "Mesh: " << m_Name << " Destoried!";
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

	void Mesh::UpdateBuffer()
	{
		Positions.UpdateBuffer();
		Normals.UpdateBuffer();
		Tangents.UpdateBuffer();
		UVs.UpdateBuffer();
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
			LOGW << "Failed to glGenVertexArrays!";
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
		Indices.DeleteBuffer();

		for (auto subMesh : m_SubMeshes)
			if (subMesh != nullptr)
				subMesh->DeleteBuffer();
	}

	void Mesh::DeleteRawData()
	{
		Positions.Data.clear();
		Normals.Data.clear();
		Tangents.Data.clear();
		UVs.Data.clear();
		Indices.Data.clear();

		for (auto subMesh : m_SubMeshes)
			if (subMesh != nullptr)
				subMesh->DeleteRawData();
	}

	void Mesh::CalculateAABB(const Vector4& min, const Vector4& max)
	{
		m_AABB.SetMinMax(min, max);
	}

	void Mesh::CalculateAABB()
	{
		Vector4 max, min, tmp;

		m_AABB.SetDirty(true);

		int triangleCount = Positions.Data.size() / 3;
		for (int i = 0; i < triangleCount; i++)
		{
			int index = i * 3;
			tmp.x = Positions.Data[index];
			tmp.y = Positions.Data[index + 1];
			tmp.z = Positions.Data[index + 2];
			m_AABB.Encapsulate(tmp);
		}
	}

	BoxBounds Mesh::GetAABB() const
	{
		return m_AABB;
	}
}