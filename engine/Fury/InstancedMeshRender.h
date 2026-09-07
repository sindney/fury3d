#ifndef _FURY_INSTANCED_MESH_RENDER_H_
#define _FURY_INSTANCED_MESH_RENDER_H_

#include "Fury/Component.h"
#include "Fury/Matrix4.h"
#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"

namespace fury
{
	class Material;

	class Mesh;

	class SceneNode;

	class Frustum;

	// ISM/HISM-style instanced static mesh rendering: one mesh (with an
	// optional LOD chain) + materials, drawn N times at static instance
	// transforms via glDrawElementsInstanced. HISM mode (default) buckets
	// visible instances per LOD tier each frame using MeshRender's
	// screen-coverage rule; ISM mode draws all visible instances at one
	// tier picked from the aggregate bounds.
	//
	// Instance transforms are local to the owning SceneNode; the node's
	// model AABB covers all instances (coarse octree culling), and the
	// pipeline frustum-culls per instance.
	class FURY_API InstancedMeshRender : public Component
	{
	public:

		typedef std::shared_ptr<InstancedMeshRender> Ptr;

		struct Instance
		{
			Vector4 Position = Vector4(0.0f, 0.0f, 0.0f);
			Quaternion Rotation;
			Vector4 Scale = Vector4(1.0f, 1.0f, 1.0f);
		};

		// One LOD tier's visible instance stream for the current frame.
		struct InstanceBatch
		{
			unsigned int LodTier = 0;
			bool Billboard = false;
			std::vector<Matrix4> WorldMatrices;
		};

		static Ptr Create(const std::shared_ptr<Mesh> &mesh, const std::shared_ptr<Material> &material);

	protected:

		std::vector<std::weak_ptr<Material>> m_Materials;

		std::weak_ptr<Mesh> m_Mesh;

		bool m_CastShadows = true;

		// Max draw distance in cm (0 = no cap). Grass and other small
		// clutter use this to drop out entirely past ~50-80 m instead of
		// paying for billboard instances nobody can see.
		float m_CullDistance = 0.0f;

		// true = HISM (per-instance LOD buckets); false = ISM (single
		// tier from the aggregate bounds).
		bool m_Hierarchical = true;

		std::vector<Instance> m_Instances;

		// Per-frame visible batches, rebuilt by BuildVisibleBatches.
		std::vector<InstanceBatch> m_Batches;

		// Cached per-instance world matrices + AABBs. Instances are static
		// by design, so the expensive compose/box-transform runs only when
		// the instance list or the owner node's world matrix changes --
		// BuildVisibleBatches then costs a frustum test + coverage per
		// instance per frame (45k instances: ~30ms -> few ms).
		std::vector<Matrix4> m_WorldMatrixCache;
		std::vector<BoxBounds> m_WorldAABBCache;
		Matrix4 m_CacheNodeWorld;      // owner world matrix the cache was built with
		bool m_MatrixCacheDirty = true;

		// All instance world matrices at the shadow LOD tier, rebuilt
		// lazily (shadow passes don't per-instance cull in v1).
		std::vector<Matrix4> m_ShadowMatrices;

		bool m_ShadowMatricesDirty = true;

	public:

		InstancedMeshRender(const std::shared_ptr<Mesh> &mesh, const std::shared_ptr<Material> &material);

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		Component::Ptr Clone() const override;

		void SetMesh(const std::shared_ptr<Mesh> &mesh);

		std::shared_ptr<Mesh> GetMesh() const;

		// if index exceeds materials.size() it appends.
		void SetMaterial(const std::shared_ptr<Material> &material, unsigned int index = 0);

		std::shared_ptr<Material> GetMaterial(unsigned int index = 0) const;

		unsigned int GetMaterialCount() const;

		bool GetRenderable() const;

		bool GetCastShadows() const;

		void SetCastShadows(bool state);

		float GetCullDistance() const { return m_CullDistance; }

		void SetCullDistance(float cm) { m_CullDistance = cm; }

		bool GetHierarchical() const { return m_Hierarchical; }

		void SetHierarchical(bool value) { m_Hierarchical = value; }

		// ---- instance list ----

		unsigned int GetInstanceCount() const { return static_cast<unsigned int>(m_Instances.size()); }

		const std::vector<Instance> &GetInstances() const { return m_Instances; }

		const Instance &GetInstance(unsigned int index) const { return m_Instances[index]; }

		void SetInstance(unsigned int index, const Instance &instance);

		void AddInstance(const Instance &instance);

		void RemoveInstance(unsigned int index);

		void ClearInstances();

		// Instance local matrix (TRS compose).
		static Matrix4 ComposeLocalMatrix(const Instance &instance);

		// Aggregate AABB of all instances in the node's model space
		// (pushed to the owner node for coarse octree culling).
		BoxBounds GetAggregateAABB() const;

		// ---- per-frame batch building (pipeline entry points) ----

		// Frustum-cull per instance and bucket into LOD tiers. HISM:
		// per-instance coverage via MeshRender::ComputeCoverageForBounds;
		// ISM: one tier from the aggregate bounds. Batches are sorted by
		// tier index; empty tiers are omitted.
		void BuildVisibleBatches(const Frustum &frustum, const std::shared_ptr<SceneNode> &cameraNode);

		const std::vector<InstanceBatch> &GetBatches() const { return m_Batches; }

		// Shadow casting: all instances at the deepest non-billboard
		// tier (billboards never cast). Lazily rebuilt.
		const std::vector<Matrix4> &GetShadowMatrices();

		// LOD tier the shadow matrices belong to.
		unsigned int GetShadowLodTier() const;

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		void RefreshOwnerAABB();
	};
}

#endif // _FURY_INSTANCED_MESH_RENDER_H_
