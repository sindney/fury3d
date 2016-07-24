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

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;
	};
}

#endif // _FURY_MESHRENDER_H_