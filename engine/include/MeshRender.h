#ifndef _FURY_MESHRENDER_H_
#define _FURY_MESHRENDER_H_

#include "Component.h"

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

		std::weak_ptr<Material> m_Material;

		std::weak_ptr<Mesh> m_Mesh;

	public:

		MeshRender(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh);

		Component::Ptr Clone() const override;

		void UpdateBuffer();

		void SetMaterial(const std::shared_ptr<Material> &material);

		std::shared_ptr<Material> GetMaterial() const;

		void SetMesh(const std::shared_ptr<Mesh> &mesh);

		std::shared_ptr<Mesh> GetMesh() const;

		bool GetRenderable() const;

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;
	};
}

#endif // _FURY_MESHRENDER_H_