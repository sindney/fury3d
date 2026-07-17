#ifndef _FURY_MESH_H_
#define _FURY_MESH_H_

#include <vector>
#include <unordered_map>

#include "Fury/Entity.h"
#include "Fury/ArrayBuffers.h"
#include "Fury/BoxBounds.h"
#include "Fury/Buffer.h"
#include "Fury/Matrix4.h"

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

		virtual void UpdateBuffer() override;

		virtual void DeleteBuffer() override;

		// if ur mesh is static, and won't change after import.
		// call this to free the memory allocated for vertex data.
		void DeleteRawData();

		virtual std::type_index GetTypeIndex() const override;
	};

	// Ordered chain of Mesh::Ptr LODs with per-LOD screen-coverage
	// transition thresholds. LOD 0 is the highest detail. Not an
	// Entity — lives as a value type on MeshRender. Empty groups are
	// invalid and never assigned.
	//
	// Inherits from Serializable so the protected save/load helpers
	// (StartObject, SaveArray, ...) are accessible. The inherited
	// virtual Save/Load are stubs because the EntityManager-relative
	// mesh-name resolution needs a manager handle — use the
	// Save/Load overloads below instead.
	class FURY_API LodGroup : public Serializable
	{
	public:

		LodGroup() = default;

		// Constructs a group from parallel arrays. The number of
		// meshes MUST equal the number of thresholds. Thresholds MUST
		// be in non-increasing order (LOD 0 >= LOD 1 >= ...); out-of-
		// order input is rejected with a FURYE and the group keeps its
		// default (empty) state.
		LodGroup(const std::vector<std::shared_ptr<class Mesh>> &meshes,
				 const std::vector<float> &thresholds);

		// Serializable stubs — use the manager-aware overloads below
		// for the actual persistence path. The base virtuals are
		// declared here so LodGroup is concrete.
		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;

		unsigned int GetLodCount() const;

		// Returns the mesh at LOD index `i`, or nullptr if out of range.
		std::shared_ptr<class Mesh> GetMesh(unsigned int i) const;

		// Returns the screen-coverage threshold at LOD index `i`, or
		// 0.0f if out of range.
		float GetThreshold(unsigned int i) const;

		bool IsEmpty() const;

		// Replaces the group's content. Validation: mesh and threshold
		// sizes MUST match and thresholds MUST be non-increasing. On
		// failure logs FURYE and the previous group state is preserved.
		void Set(const std::vector<std::shared_ptr<class Mesh>> &meshes,
				 const std::vector<float> &thresholds);

		// Persist as {thresholds:[...], meshes:["name", ...]}. `wrapper`
		// matches Serializable's rapidjson contract.
		void SaveMeshData(void* wrapper, bool object = true) const;

		// Read a `lod_group` object previously written by Save. Mesh
		// names are resolved through `manager`; any unresolved name
		// causes the load to fail (return false) and logs FURYE.
		bool LoadFromManager(const void* wrapper, const std::shared_ptr<class EntityManager> &manager, bool object = true);

	private:

		std::vector<std::shared_ptr<class Mesh>> m_Meshes;

		std::vector<float> m_Thresholds;
	};

	class Joint;

	// TODO: Add read only property
	class FURY_API Mesh : public Entity, public Buffer,
						 public std::enable_shared_from_this<Mesh>
	{
	public:

		friend class Shader;

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

		// Additional LOD meshes (LOD 1..N). LOD 0 is this mesh itself.
		// Owned by this mesh and serialized inline — they are not
		// registered as separate entities in the EntityManager.
		std::vector<std::shared_ptr<class Mesh>> m_LodMeshes;

		// Per-LOD screen-coverage threshold for LOD 1..N. Same length
		// as m_LodMeshes; non-increasing (LOD 1 >= LOD 2 >= ...).
		std::vector<float> m_LodThresholds;

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

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		void AddSubMesh(const SubMesh::Ptr &subMesh);

		SubMesh::Ptr GetSubMeshAt(unsigned int index) const;

		unsigned int GetSubMeshCount() const;

		bool IsSkinnedMesh() const;

		std::shared_ptr<Joint> GetJoint(const std::string &name) const;

		std::shared_ptr<Joint> GetJointAt(unsigned int index) const;

		// this returns the count of joints that influences mesh's vertices.
		unsigned int GetJointCount() const;

		std::shared_ptr<Joint> GetRootJoint() const;

		// Replace this mesh's joint registry. Used by GltfImporter and any
		// other importer that needs to attach a freshly-built skeleton. The
		// rebuilt m_JointMap is keyed by joint name (matching what the
		// runtime Mesh::Load path expects).
		void SetJointTree(const std::vector<std::shared_ptr<Joint>> &joints,
			const std::shared_ptr<Joint> &root_joint);

		virtual void UpdateBuffer() override;

		virtual void DeleteBuffer() override;

		void CalculateAABB(const Vector4& min, const Vector4& max);

		void CalculateAABB();

		BoxBounds GetAABB() const;

		bool GetCastShadows() const;

		void SetCastShadows(bool state);

		// LOD chain. LOD 0 is this mesh itself (highest detail). LODs
		// 1..N are additional Mesh objects owned by this mesh — they
		// are saved inline with this mesh's JSON (not as separate
		// entities in the EntityManager) so a "loded mesh" is one
		// asset. Thresholds are screen-coverage values, one per
		// additional LOD, in non-increasing order (LOD 1's threshold
		// is the screen-coverage below which LOD 1 becomes the
		// active level).
		//
		// GetLodCount() returns 1 + m_LodMeshes.size() (the source
		// plus the additional levels). GetLodMesh(0) returns this
		// mesh; GetLodMesh(i) for i > 0 returns m_LodMeshes[i-1].
		// GetLodThreshold(0) is always 1.0 (LOD 0 is the highest
		// detail and is active whenever the model is on-screen).
		unsigned int GetLodCount() const;
		std::shared_ptr<Mesh> GetLodMesh(unsigned int i) const;
		float GetLodThreshold(unsigned int i) const;
		// Read-only access to the full chain (LOD 1..N; LOD 0 is the
		// mesh itself and isn't in this list). Useful for the editor's
		// threshold editor.
		const std::vector<std::shared_ptr<Mesh>> &GetLodMeshes() const { return m_LodMeshes; }
		void SetLodMeshes(const std::vector<std::shared_ptr<Mesh>> &meshes,
						  const std::vector<float> &thresholds);
		void ClearLodChain();
	};

	// 64-bit FNV-1a content fingerprint over a mesh's Positions.Data
	// (raw float bytes) + top-level Indices.Data (raw uint bytes) +
	// each submesh's Indices.Data (in submesh order). Used by the
	// editor's thumbnail disk cache to key cached PNGs on the actual
	// vertex/index content, not on BufferId (which is identity-only —
	// in-place vertex edits do not change it). No allocations beyond
	// the 64-bit accumulator; safe to call from worker threads on a
	// shared_ptr-stabilized mesh.
	unsigned int FURY_API MeshContentHash(const class Mesh* mesh);

	// 16-character lowercase hexadecimal representation of a 64-bit
	// hash, e.g. 0x1a2b3c4d5e6f7a8b → "1a2b3c4d5e6f7a8b". Used to
	// build the on-disk thumbnail filename `furye_<hex>.png`.
	std::string FURY_API FormatHashHex(unsigned int hash);
}

#endif // _FURY_MESH_H_