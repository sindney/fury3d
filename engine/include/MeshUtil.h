#ifndef _FURY_MESHUTIL_H_
#define _FURY_MESHUTIL_H_

#include <memory>
#include <unordered_map>

#include "Macros.h"
#include "Matrix4.h"

namespace fury
{
	class Mesh;

	class FURY_API MeshUtil final 
	{
		friend class Engine;

	private:

		static std::shared_ptr<Mesh> m_UnitCube;

		static std::shared_ptr<Mesh> m_UnitQuad;

		static std::shared_ptr<Mesh> m_UnitSphere;

		static std::shared_ptr<Mesh> m_UnitIcoSphere;

		static std::shared_ptr<Mesh> m_UnitCylinder;

		static std::shared_ptr<Mesh> m_UnitCone;

	public:

		static std::shared_ptr<Mesh> GetUnitCube();

		static std::shared_ptr<Mesh> GetUnitQuad();

		static std::shared_ptr<Mesh> GetUnitSphere();

		static std::shared_ptr<Mesh> GetUnitIcoSphere();

		static std::shared_ptr<Mesh> GetUnitCylinder();

		static std::shared_ptr<Mesh> GetUnitCone();

		static std::shared_ptr<Mesh> CreateQuad(const std::string &name, Vector4 min, Vector4 max);

		static std::shared_ptr<Mesh> CreateCube(const std::string &name, Vector4 min, Vector4 max);

		static std::shared_ptr<Mesh> CreateIcoSphere(const std::string &name, float radius, int level);

		static std::shared_ptr<Mesh> CreateSphere(const std::string &name, float radius, int segH, int segV);

		static std::shared_ptr<Mesh> CreateCylinder(const std::string &name, float topR, float bottomR, float height,
			int segH, int segV);

		static void TransformMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 &matrix, bool updateBuffer = false);

		// restruct mesh's data by finding & removing possible reapet vertices.
		static void OptimizeMesh(const std::shared_ptr<Mesh> &mesh);

		// you should calculate normal first, then optimize ur mesh.
		static void CalculateNormal(const std::shared_ptr<Mesh> &mesh);

		// TODO: test
		// you should calculate normal first, then calculate tangent.
		static void CalculateTangent(const std::shared_ptr<Mesh> &mesh);
	};
}

#endif // _FURY_MESHUTIL_H_