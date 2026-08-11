#include "Fury/Terrain.h"

#include <cmath>
#include <fstream>

#include <rapidjson/document.h>

#include "Fury/EntityManager.h"
#include "Fury/FileUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Heightmap.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"

namespace fury
{
	namespace
	{
		// load-or-reuse a project texture and register it in the scene's
		// EntityManager (name == path convention) so the editor's texture
		// picker can offer it back
		std::shared_ptr<Texture> LoadTerrainTexture(const std::string &path, bool srgb)
		{
			if (path.empty() || Scene::Active == nullptr)
				return nullptr;
			auto em = Scene::Active->GetEntityManager();
			if (em)
			{
				if (auto existing = em->Get<Texture>(path))
					if (existing->GetFilePath() == path)
						return existing;
			}
			auto tex = Texture::Create(path);
			tex->CreateFromImage(path, srgb, true);
			// failed load: return null so callers bind a dummy instead of a
			// dirty texture (which silently aliases unit 0's texture)
			if (tex->GetID() == 0)
				return nullptr;
			// terrain textures minify to the horizon; trilinear or they shimmer
			tex->SetFilterMode(FilterMode::LINEAR_MIPMAP_LINEAR);
			if (em)
				em->Add(tex);
			return tex;
		}
			// resolve (or register-then-resolve) the named heightmap asset;
		// file-backed names are the .r16 path itself, so a scene that only
		// names a path self-registers its asset on first use
		std::shared_ptr<Heightmap> ResolveHeightmapAsset(const std::string &name)
		{
			if (name.empty() || Scene::Active == nullptr)
				return nullptr;
			auto em = Scene::Active->GetEntityManager();
			if (!em)
				return nullptr;
			if (auto existing = em->Get<Heightmap>(name))
			{
				existing->LoadHeights();
				return existing->HasHeights() ? existing : nullptr;
			}
			auto hm = Heightmap::Create(name);
			hm->SetFilePath(name);
			if (!hm->LoadHeights())
				return nullptr;
			em->Add(hm);
			return hm;
		}
	}

	Terrain::Ptr Terrain::Create()
	{
		return std::make_shared<Terrain>();
	}

	Terrain::Terrain()
	{
		m_TypeIndex = typeid(Terrain);
		// channel convention: R grass, G rock, B mud, A snow
		m_Layers[0].Name = "grass";
		m_Layers[1].Name = "rock";
		m_Layers[2].Name = "mud";
		m_Layers[3].Name = "snow";
	}

	Terrain::~Terrain()
	{
	}

	Component::Ptr Terrain::Clone() const
	{
		auto ptr = Create();
		*ptr = *this;
		ptr->m_Built = false;
		ptr->m_ChunkContainer.reset();
		ptr->m_ChunkMeshes.clear();
		ptr->m_Material = nullptr;
		return ptr;
	}

	bool Terrain::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Terrain: json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "Terrain")
		{
			FURYE << "Terrain: invalid type " << str << "!";
			return false;
		}

		LoadMemberValue(wrapper, "heightmap", m_HeightmapName);
		LoadMemberValue(wrapper, "splatmap", m_SplatmapPath);
		LoadMemberValue(wrapper, "chunk_count", m_ChunkCount);
		LoadMemberValue(wrapper, "lod_count", m_LodCount);

		for (int i = 0; i < 4; i++)
		{
			std::string base = "layer" + std::to_string(i) + "_";
			LoadMemberValue(wrapper, base + "name", m_Layers[i].Name);
			LoadMemberValue(wrapper, base + "texture", m_Layers[i].TexturePath);
			LoadMemberValue(wrapper, base + "tiling", m_Layers[i].TilingCm);
		}

		// chunks are runtime-built (never serialized), so a loaded terrain
		// must rebuild them - this is also what restores them in play mode,
		// whose temp scene strips editorOnly nodes
		Rebuild();
		return true;
	}

	void Terrain::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		SaveKey(wrapper, "type");
		SaveValue(wrapper, "Terrain");

		SaveKey(wrapper, "heightmap");   SaveValue(wrapper, m_HeightmapName);
		SaveKey(wrapper, "splatmap");    SaveValue(wrapper, m_SplatmapPath);
		SaveKey(wrapper, "chunk_count"); SaveValue(wrapper, m_ChunkCount);
		SaveKey(wrapper, "lod_count");   SaveValue(wrapper, m_LodCount);

		for (int i = 0; i < 4; i++)
		{
			std::string base = "layer" + std::to_string(i) + "_";
			SaveKey(wrapper, base + "name");    SaveValue(wrapper, m_Layers[i].Name);
			SaveKey(wrapper, base + "texture"); SaveValue(wrapper, m_Layers[i].TexturePath);
			SaveKey(wrapper, base + "tiling");  SaveValue(wrapper, m_Layers[i].TilingCm);
		}

		if (object)
			EndObject(wrapper);
	}

	void Terrain::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		if (!m_HeightmapName.empty() && !m_Built)
			Rebuild();
	}

	void Terrain::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		DestroyChunks();
		Component::OnDetaching(node);
	}

	std::shared_ptr<Heightmap> Terrain::ResolveHeightmap()
	{
		if (!m_Heightmap)
			m_Heightmap = ResolveHeightmapAsset(m_HeightmapName);
		return m_Heightmap;
	}

	bool Terrain::HasHeights() const
	{
		return m_Heightmap && m_Heightmap->HasHeights();
	}

	int Terrain::GetResolution() const
	{
		return m_Heightmap ? m_Heightmap->GetResolution() : 0;
	}

	float Terrain::GetWorldSizeX() const
	{
		return m_Heightmap ? m_Heightmap->GetWorldSizeX() : 0.0f;
	}

	float Terrain::GetWorldSizeZ() const
	{
		return m_Heightmap ? m_Heightmap->GetWorldSizeZ() : 0.0f;
	}

	float Terrain::GetHeightScale() const
	{
		return m_Heightmap ? m_Heightmap->GetHeightScale() : 0.0f;
	}

	const std::vector<float> &Terrain::GetHeights() const
	{
		static const std::vector<float> s_Empty;
		return m_Heightmap ? m_Heightmap->GetHeights() : s_Empty;
	}

	void Terrain::DestroyChunks()
	{
		if (auto container = m_ChunkContainer.lock())
			container->RemoveFromParent();
		m_ChunkContainer.reset();
		m_ChunkMeshes.clear();
		m_Material = nullptr;
		m_Built = false;
	}

	Vector4 Terrain::SampleNormal(float fx, float fz) const
	{
		const int N = m_Heightmap->GetResolution();
		const auto &heights = m_Heightmap->GetHeights();
		const float worldSizeX = m_Heightmap->GetWorldSizeX();
		const float worldSizeZ = m_Heightmap->GetWorldSizeZ();
		auto heightAt = [&](int x, int z) -> float
		{
			x = x < 0 ? 0 : (x >= N ? N - 1 : x);
			z = z < 0 ? 0 : (z >= N ? N - 1 : z);
			return heights[static_cast<size_t>(z) * N + x];
		};

		const int x = static_cast<int>(std::round(fx));
		const int z = static_cast<int>(std::round(fz));
		const float cellX = worldSizeX / (N - 1);
		const float cellZ = worldSizeZ / (N - 1);
		// +-2 kernel: one-texel terrace risers alias into striped lighting
		// with a central difference
		const float dhdx = (heightAt(x + 2, z) - heightAt(x - 2, z)) / (4.0f * cellX);
		const float dhdz = (heightAt(x, z + 2) - heightAt(x, z - 2)) / (4.0f * cellZ);

		Vector4 n(-dhdx, 1.0f, -dhdz, 0.0f);
		n.Normalize();
		return n;
	}

	std::shared_ptr<Mesh> Terrain::BuildChunkMesh(int cx, int cz, int stride)
	{
		const int N = m_Heightmap->GetResolution();
		const auto &heights = m_Heightmap->GetHeights();
		const float worldSizeX = m_Heightmap->GetWorldSizeX();
		const float worldSizeZ = m_Heightmap->GetWorldSizeZ();
		const float heightScale = m_Heightmap->GetHeightScale();
		const int quadsPerChunk = (N - 1) / m_ChunkCount;
		const int V = quadsPerChunk / stride + 1;   // verts per side
		const int ox = cx * quadsPerChunk;          // origin sample
		const int oz = cz * quadsPerChunk;
		const float cellX = worldSizeX / (N - 1);
		const float cellZ = worldSizeZ / (N - 1);
		const float skirtDepth = heightScale * 0.02f + 1.0f;

		auto mesh = Mesh::Create("terrain_chunk");

		// grid verts + a dropped skirt ring (4 per side, corners duplicated)
		const int gridVerts = V * V;
		mesh->Positions.Data.reserve(static_cast<size_t>(gridVerts + V * 4) * 3);
		mesh->Normals.Data.reserve(static_cast<size_t>(gridVerts + V * 4) * 3);
		mesh->UVs.Data.reserve(static_cast<size_t>(gridVerts + V * 4) * 2);

		auto pushVert = [&](int sx, int sz, bool skirt)
		{
			const float px = -worldSizeX * 0.5f + sx * cellX;
			const float pz = -worldSizeZ * 0.5f + sz * cellZ;
			float py = heights[static_cast<size_t>(sz) * N + sx];
			if (skirt)
				py -= skirtDepth;
			mesh->Positions.Data.push_back(px);
			mesh->Positions.Data.push_back(py);
			mesh->Positions.Data.push_back(pz);

			Vector4 n = SampleNormal(static_cast<float>(sx), static_cast<float>(sz));
			mesh->Normals.Data.push_back(n.x);
			mesh->Normals.Data.push_back(n.y);
			mesh->Normals.Data.push_back(n.z);

			mesh->UVs.Data.push_back(static_cast<float>(sx) / (N - 1));
			mesh->UVs.Data.push_back(static_cast<float>(sz) / (N - 1));
		};

		for (int j = 0; j < V; j++)
			for (int i = 0; i < V; i++)
				pushVert(ox + i * stride, oz + j * stride, false);

		auto &idx = mesh->Indices.Data;
		for (int j = 0; j + 1 < V; j++)
		{
			for (int i = 0; i + 1 < V; i++)
			{
				const unsigned int i0 = j * V + i;
				const unsigned int i1 = i0 + 1;
				const unsigned int i2 = i0 + V;
				const unsigned int i3 = i2 + 1;
				// x right, z forward, CCW from +Y
				idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
				idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
			}
		}

		// skirt ring: 4 edges, each V verts dropped straight down
		auto skirtEdge = [&](bool varyI, int fixed, bool flip)
		{
			const unsigned int ringStart = static_cast<unsigned int>(mesh->Positions.Data.size() / 3);
			for (int k = 0; k < V; k++)
			{
				const int i = varyI ? k : fixed;
				const int j = varyI ? fixed : k;
				pushVert(ox + i * stride, oz + j * stride, true);
			}
			for (int k = 0; k + 1 < V; k++)
			{
				const unsigned int g0 = (varyI ? fixed * V + k : k * V + fixed);
				const unsigned int g1 = (varyI ? fixed * V + k + 1 : (k + 1) * V + fixed);
				const unsigned int s0 = ringStart + k;
				const unsigned int s1 = ringStart + k + 1;
				if (flip)
				{
					idx.push_back(g0); idx.push_back(s0); idx.push_back(g1);
					idx.push_back(g1); idx.push_back(s0); idx.push_back(s1);
				}
				else
				{
					idx.push_back(g0); idx.push_back(g1); idx.push_back(s0);
					idx.push_back(g1); idx.push_back(s1); idx.push_back(s0);
				}
			}
		};
		skirtEdge(true, 0, false);          // north (j = 0)
		skirtEdge(true, V - 1, true);       // south
		skirtEdge(false, 0, true);          // west (i = 0)
		skirtEdge(false, V - 1, false);     // east

		mesh->CalculateAABB();
		return mesh;
	}

	void Terrain::ReloadTextures()
	{
		if (_ptrc_glGenTextures == nullptr)
			return;   // headless: no GL, no material needed

		if (!m_Material)
			m_Material = Material::Create("terrain_mat");

		// gbuffer shader override (pass render index 0 = gbuffer in both
		// stock pipelines); only compiled once
		if (m_Material->GetShaderForPass(0) == nullptr)
		{
			auto shader = Shader::Create("terrain_gbuffer", ShaderType::STATIC_MESH);
			if (shader->LoadAndCompile(FileUtil::GetAbsPath() + "Resource/Shader/Terrain/DrawTerrain.glsl"))
				m_Material->SetShaderForPass(0, shader);
			else
				FURYE << "Terrain: DrawTerrain.glsl failed to compile";
		}

		if (auto splat = LoadTerrainTexture(m_SplatmapPath, false))
			m_Material->SetTexture("u_splat_map", splat);
		else
			m_Material->SetTexture("u_splat_map", GetDummyTexture2D());

		float tilings[4];
		for (int i = 0; i < 4; i++)
		{
			auto tex = LoadTerrainTexture(m_Layers[i].TexturePath, true);
			// every sampler bound every draw (core GL target-mismatch trap)
			m_Material->SetTexture("u_layer" + std::to_string(i),
				tex ? tex : GetDummyTexture2D());
			tilings[i] = m_Layers[i].TilingCm;
		}
		m_Material->SetUniform("u_layer_tiling", Uniform4f::Create({ tilings[0], tilings[1], tilings[2], tilings[3] }));
	}

	void Terrain::Rebuild()
	{
		DestroyChunks();

		if (!ResolveHeightmap())
			return;

		auto node = m_Owner.lock();
		if (!node)
			return;

		// chunk count must divide the quad count evenly
		const int quads = m_Heightmap->GetResolution() - 1;
		while (m_ChunkCount > 1 && quads % m_ChunkCount != 0)
			m_ChunkCount--;
		m_ChunkCount = std::max(1, std::min(m_ChunkCount, quads));
		m_LodCount = std::max(1, std::min(m_LodCount, 5));

		// shared terrain material (splat + 4 layers + gbuffer shader
		// override). Headless (fury exec): no GL, no material - chunks get a
		// null material and are never rendered or saved there.
		ReloadTextures();

		auto container = SceneNode::Create("__terrain_chunks");
		// runtime-built: keep scene files (and play-mode temp saves) small;
		// Terrain::Load rebuilds them on every load
		container->SetEditorOnly(true);
		node->AddChild(container);

		m_ChunkMeshes.reserve(static_cast<size_t>(m_ChunkCount) * m_ChunkCount);
		// screen-coverage thresholds: keep finer LODs out to mid distance so
		// silhouettes stay smooth on 1m-grid terrain
		static const float kLodThresholds[4] = { 0.35f, 0.12f, 0.04f, 0.015f };
		std::vector<float> thresholds;
		for (int l = 1; l < m_LodCount; l++)
			thresholds.push_back(kLodThresholds[std::min(l - 1, 3)]);

		for (int cz = 0; cz < m_ChunkCount; cz++)
		{
			for (int cx = 0; cx < m_ChunkCount; cx++)
			{
				auto base = BuildChunkMesh(cx, cz, 1);
				base->SetName("terrain_chunk_" + std::to_string(cx) + "_" + std::to_string(cz));

				std::vector<std::shared_ptr<Mesh>> lods;
				for (int l = 1; l < m_LodCount; l++)
					lods.push_back(BuildChunkMesh(cx, cz, 1 << l));
				if (!lods.empty())
					base->SetLodMeshes(lods, thresholds);

				auto chunkNode = SceneNode::Create(base->GetName());
				chunkNode->AddComponent(MeshRender::Create(m_Material, base));
				container->AddChild(chunkNode);
				m_ChunkMeshes.push_back(base);
			}
		}

		m_ChunkContainer = container;
		m_Built = true;

		FURYD << "Terrain: built " << m_ChunkCount << "x" << m_ChunkCount
			<< " chunks x" << m_LodCount << " LODs";
	}

	float Terrain::GetHeight(float worldX, float worldZ) const
	{
		if (!HasHeights())
			return 0.0f;

		auto node = m_Owner.lock();
		if (!node)
			return 0.0f;

		// world -> terrain-local via the full matrix (scaled-ancestor trap:
		// never piecewise GetWorld* getters)
		Matrix4 invWorld = node->GetWorldMatrix().Inverse();
		Vector4 local = invWorld.Multiply(Vector4(worldX, 0.0f, worldZ, 1.0f));

		const int N = m_Heightmap->GetResolution();
		const float worldSizeX = m_Heightmap->GetWorldSizeX();
		const float worldSizeZ = m_Heightmap->GetWorldSizeZ();
		const auto &heights = m_Heightmap->GetHeights();
		const float gx = (local.x + worldSizeX * 0.5f) / worldSizeX * (N - 1);
		const float gz = (local.z + worldSizeZ * 0.5f) / worldSizeZ * (N - 1);
		const float cx = std::min(std::max(gx, 0.0f), static_cast<float>(N - 1));
		const float cz = std::min(std::max(gz, 0.0f), static_cast<float>(N - 1));

		const int x0 = static_cast<int>(cx);
		const int z0 = static_cast<int>(cz);
		const int x1 = std::min(x0 + 1, N - 1);
		const int z1 = std::min(z0 + 1, N - 1);
		const float tx = cx - x0;
		const float tz = cz - z0;

		const float h00 = heights[static_cast<size_t>(z0) * N + x0];
		const float h10 = heights[static_cast<size_t>(z0) * N + x1];
		const float h01 = heights[static_cast<size_t>(z1) * N + x0];
		const float h11 = heights[static_cast<size_t>(z1) * N + x1];
		const float h = (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;

		Vector4 world = node->GetWorldMatrix().Multiply(Vector4(local.x, h, local.z, 1.0f));
		return world.y;
	}
}
