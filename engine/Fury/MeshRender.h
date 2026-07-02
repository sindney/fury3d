#ifndef _FURY_MESHRENDER_H_
#define _FURY_MESHRENDER_H_

#include "Fury/Component.h"

namespace fury
{
	class Material;

	class Mesh;

	class FURY_API MeshRender : public Component
	{
	public:

		typedef std::shared_ptr<MeshRender> Ptr;

		static Ptr Create(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh);

	protected:

		std::vector<std::weak_ptr<Material>> m_Materials;

		// Per-instance shadow-casting flag. See GetCastShadows() below.
		bool m_CastShadows = true;

		std::weak_ptr<Mesh> m_Mesh;

	public:

		MeshRender(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh);

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		Component::Ptr Clone() const override;

		void UpdateBuffer();

		// if index exceeds material.size().
		// it will append ur material to the end of the vector.
		void SetMaterial(const std::shared_ptr<Material> &material, unsigned int index = 0);

		std::shared_ptr<Material> GetMaterial(unsigned int index = 0) const;

		unsigned int GetMaterialCount() const;

		void SetMesh(const std::shared_ptr<Mesh> &mesh);

		std::shared_ptr<Mesh> GetMesh() const;

		bool GetRenderable() const;

		// Per-instance shadow-casting flag. Mesh is a shared resource
		// (multiple MeshRender instances can reference the same mesh),
		// so a flag on the mesh itself is shared across every tank
		// that uses it. Storing it on MeshRender makes "Cast Shadows"
		// genuinely per-instance — toggling it on one tank leaves the
		// other tanks using the same mesh unaffected.
		bool GetCastShadows() const;

		void SetCastShadows(bool state);

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;
	};
}

#endif // _FURY_MESHRENDER_H_