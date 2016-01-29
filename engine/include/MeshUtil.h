#ifndef _FURY_MESHUTIL_H_
#define _FURY_MESHUTIL_H_

#include <memory>
#include <unordered_map>

#include "Singleton.h"
#include "Matrix4.h"

namespace fury
{
	class Mesh;

	class FURY_API MeshUtil : public Singleton<MeshUtil>
	{
	private:

		std::unordered_map<std::string, std::shared_ptr<Mesh>> m_MeshMap;

	public:

		typedef std::shared_ptr<MeshUtil> Ptr;

		MeshUtil();

		std::shared_ptr<Mesh> GetUnitCube() const;

		std::shared_ptr<Mesh> GetUnitQuad() const;

		std::shared_ptr<Mesh> GetUnitSphere() const;

		std::shared_ptr<Mesh> GetUnitIcoSphere() const;

		std::shared_ptr<Mesh> GetUnitCylinder() const;

		std::shared_ptr<Mesh> GetUnitCone() const;

		std::shared_ptr<Mesh> CreateQuad(const std::string &name, Vector4 min, Vector4 max);

		std::shared_ptr<Mesh> CreateCube(const std::string &name, Vector4 min, Vector4 max);

		std::shared_ptr<Mesh> CreateIcoSphere(const std::string &name, float radius, int level);

		std::shared_ptr<Mesh> CreateSphere(const std::string &name, float radius, int segH, int segV);

		std::shared_ptr<Mesh> CreateCylinder(const std::string &name, float topR, float bottomR, float height, 
			int segH, int segV);

		void TransformMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 &matrix) const;

		// restruct mesh's data by finding & removing possible reapet vertices.
		void OptimizeMesh(const std::shared_ptr<Mesh> &mesh);

		void CalculateNormal(const std::shared_ptr<Mesh> &mesh) const;
	};
}

#endif // _FURY_MESHUTIL_H_