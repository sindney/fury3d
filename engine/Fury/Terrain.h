#ifndef _FURY_TERRAIN_H_
#define _FURY_TERRAIN_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/Component.h"
#include "Fury/Vector4.h"

namespace fury
{
	class Heightmap;

	class Material;

	class Mesh;

	class SceneNode;

	// Unity-Terrain-style heightmap terrain (change add-sky-atmosphere-terrain,
	// design D8/D9): a 16-bit .r16 heightmap + JSON sidecar, rendered as a
	// chunkCount^2 grid of geomipmap LOD chunk meshes (built at load, flagged
	// editorOnly so they never serialize), shaded by a 4-layer splat in the
	// gbuffer pass. Collision comes from a sibling BodySetup with the
	// HeightField shape type reading this component's heights.
	class FURY_API Terrain : public Component
	{
	public:

		struct Layer
		{
			std::string Name;
			std::string TexturePath;
			float TilingCm = 800.0f;
		};

		typedef std::shared_ptr<Terrain> Ptr;

		static Ptr Create();

		Terrain();

		virtual ~Terrain();

		virtual Component::Ptr Clone() const override;

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		// Heightmap asset reference (EntityManager name; the path-based
		// setters keep working - name == path for file-backed assets).
		const std::string &GetHeightmapName() const { return m_HeightmapName; }
		void SetHeightmapName(const std::string &name) { m_HeightmapName = name; m_Heightmap = nullptr; }

		const std::string &GetHeightmapPath() const { return m_HeightmapName; }
		void SetHeightmapPath(const std::string &path) { SetHeightmapName(path); }

		// The resolved (and loaded) heightmap asset, or null.
		std::shared_ptr<Heightmap> ResolveHeightmap();

		const std::string &GetSplatmapPath() const { return m_SplatmapPath; }
		void SetSplatmapPath(const std::string &path) { m_SplatmapPath = path; }

		int GetChunkCount() const { return m_ChunkCount; }
		void SetChunkCount(int v) { m_ChunkCount = v; }

		int GetLodCount() const { return m_LodCount; }
		void SetLodCount(int v) { m_LodCount = v; }

		Layer &GetLayer(int index) { return m_Layers[index & 3]; }
		void SetLayer(int index, const Layer &layer) { m_Layers[index & 3] = layer; }

		// (Re)loads the heightmap if needed and rebuilds all chunk meshes.
		void Rebuild();

		// Reloads only the splat/layer textures + material (editor picks).
		void ReloadTextures();

		std::shared_ptr<Material> GetMaterial() const { return m_Material; }

		// Bilinear world-space height query (cm). Clamps at the edges.
		float GetHeight(float worldX, float worldZ) const;

		bool HasHeights() const;

		int GetResolution() const;

		float GetWorldSizeX() const;
		float GetWorldSizeZ() const;
		float GetHeightScale() const;

		// heights in cm (decoded from the .r16), row-major [z * N + x]
		const std::vector<float> &GetHeights() const;

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		void DestroyChunks();

		std::shared_ptr<Mesh> BuildChunkMesh(int cx, int cz, int stride);

		Vector4 SampleNormal(float fx, float fz) const;

		std::string m_HeightmapName;

		std::string m_SplatmapPath;

		Layer m_Layers[4];

		int m_ChunkCount = 8;

		int m_LodCount = 3;

		// resolved asset (heights live on it)
		std::shared_ptr<Heightmap> m_Heightmap;

		// runtime-built chunk state (never serialized)
		bool m_Built = false;

		std::weak_ptr<SceneNode> m_ChunkContainer;

		std::vector<std::shared_ptr<Mesh>> m_ChunkMeshes;

		std::shared_ptr<Material> m_Material;
	};
}

#endif // _FURY_TERRAIN_H_
