#ifndef _FURY_MESH_H_
#define _FURY_MESH_H_

#include <vector>

#include "Entity.h"
#include "ArrayBuffers.h"
#include "BoxBounds.h"
#include "Buffer.h"

namespace fury
{
	class FURY_API SubMesh final : public Buffer, public TypeComparable
	{
	public:

		friend class Shader;

		typedef std::shared_ptr<SubMesh> Ptr;

		static Ptr Create();

	protected:

		std::type_index m_TypeIndex;

		unsigned int m_VAO;

	public:

		ArrayBufferui Indices;

		SubMesh();

		~SubMesh();

		void UpdateBuffer();

		virtual void DeleteBuffer() override;

		// if ur mesh is static, and won't change after import.
		// call this to free the memory allocated for vertex data.
		void DeleteRawData();

		virtual std::type_index GetTypeIndex() const override;
	};

	class FURY_API Mesh : public Entity, public Buffer
	{
	public:

		friend class Shader;

		typedef std::shared_ptr<Mesh> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		unsigned int m_VAO;

		BoxBounds m_AABB;

		std::vector<SubMesh::Ptr> m_SubMeshes;

	public:

		ArrayBufferf Positions;

		ArrayBufferf Normals;

		ArrayBufferf Tangents;

		ArrayBufferf UVs;

		ArrayBufferui Indices;

		Mesh(const std::string &name);

		virtual ~Mesh();

		void AddSubMesh(const SubMesh::Ptr &subMesh);

		SubMesh::Ptr GetSubMeshAt(unsigned int index) const;

		unsigned int GetSubMeshCount() const;

		void UpdateBuffer();

		virtual void DeleteBuffer() override;

		// if ur mesh is static, and won't change after import.
		// call this to free the memory allocated for vertex data.
		void DeleteRawData();

		void CalculateAABB(const Vector4& min, const Vector4& max);

		void CalculateAABB();

		BoxBounds GetAABB() const;
	};
}

#endif // _FURY_MESH_H_