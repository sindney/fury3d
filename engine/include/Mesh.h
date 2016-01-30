#ifndef _FURY_MESH_H_
#define _FURY_MESH_H_

#include "Entity.h"
#include "ArrayBuffers.h"
#include "BoxBounds.h"
#include "Buffer.h"

namespace fury
{
	class FURY_API Mesh : public Entity, public Buffer
	{
	public:

		friend class Shader;

		typedef std::shared_ptr<Mesh> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		unsigned int m_VAO;

		BoxBounds m_AABB;

	public:

		ArrayBufferf Positions;

		ArrayBufferf Normals;

		ArrayBufferf Tangents;

		ArrayBufferf UVs;

		ArrayBufferui Indices;

		Mesh(const std::string &name);

		virtual ~Mesh();

		void UpdateBuffer();

		virtual void DeleteBuffer();

		void CalculateAABB(const Vector4& min, const Vector4& max);

		void CalculateAABB();

		BoxBounds GetAABB() const;
	};
}

#endif // _FURY_MESH_H_