#include "Debug.h"
#include "GLLoader.h"
#include "Mesh.h"
#include "SceneNode.h"

namespace fury
{
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
			m_Dirty = true;
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