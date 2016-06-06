#ifndef _FURY_MESH_H_
#define _FURY_MESH_H_

#include <vector>
#include <unordered_map>

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

	class Joint;

	class FURY_API Mesh : public Entity, public Buffer
	{
	public:

		friend class Shader;

		friend class FbxParser;

		typedef std::shared_ptr<Mesh> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		unsigned int m_VAO;

		BoxBounds m_AABB;

		std::vector<SubMesh::Ptr> m_SubMeshes;

		std::unordered_map<std::string, std::shared_ptr<Joint>> m_JointMap;

		std::vector<std::shared_ptr<Joint>> m_Joints;

		std::shared_ptr<Joint> m_RootJoint;

		bool m_CastShadows = false;

	public:

		ArrayBufferf Positions;

		ArrayBufferf Normals;

		ArrayBufferf Tangents;

		ArrayBufferf UVs;

		ArrayBufferf Weights;

		ArrayBufferui IDs;

		ArrayBufferui Indices;

		Mesh(const std::string &name);

		virtual ~Mesh();

		void AddSubMesh(const SubMesh::Ptr &subMesh);

		SubMesh::Ptr GetSubMeshAt(unsigned int index) const;

		unsigned int GetSubMeshCount() const;

		bool IsSkinnedMesh() const;

		std::shared_ptr<Joint> GetJoint(const std::string &name) const;

		std::shared_ptr<Joint> GetJointAt(unsigned int index) const;

		// this returns the count of joints that influences mesh's vertices.
		unsigned int GetJointCount() const;

		std::shared_ptr<Joint> GetRootJoint() const;

		void UpdateBuffer();

		virtual void DeleteBuffer() override;

		void CalculateAABB(const Vector4& min, const Vector4& max);

		void CalculateAABB();

		BoxBounds GetAABB() const;

		bool GetCastShadows() const;

		void SetCastShadows(bool state);
	};
}

#endif // _FURY_MESH_H_