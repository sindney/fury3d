#include "Fury/OceanComponent.h"

#include <cmath>

#include "Fury/BoxBounds.h"
#include "Fury/Engine.h"
#include "Fury/Log.h"

#include "Fury/OceanWaves.h"
#include "Fury/SceneNode.h"
#include "Fury/WaveSampler.h"

namespace fury
{
	OceanComponent::Ptr OceanComponent::Create()
	{
		return std::make_shared<OceanComponent>();
	}

	OceanComponent::OceanComponent(const std::string &name)
		: m_Name(name)
	{
		m_TypeIndex = typeid(OceanComponent);
	}

	bool OceanComponent::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "OceanComponent: json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "OceanComponent")
		{
			FURYE << "OceanComponent: invalid type " << str;
			return false;
		}

		LoadMemberValue(wrapper, "name", m_Name);

		unsigned int u = 0;
		if (LoadMemberValue(wrapper, "mode", u)) m_Mode = (Mode)u;
		if (LoadMemberValue(wrapper, "waveSource", u)) m_WaveSource = (WaveSource)u;
		LoadMemberValue(wrapper, "waveAsset", m_WaveAssetPath);

		LoadMemberValue(wrapper, "seed", m_Seed);
		LoadMemberValue(wrapper, "windSpeed", m_WindSpeed);
		LoadMemberValue(wrapper, "windDirectionDeg", m_WindDirectionDeg);
		LoadMemberValue(wrapper, "fetchCm", m_FetchCm);
		LoadMemberValue(wrapper, "choppiness", m_Choppiness);
		LoadMemberValue(wrapper, "swellResolution", m_SwellResolution);
		LoadMemberValue(wrapper, "rippleResolution", m_RippleResolution);
		LoadMemberValue(wrapper, "swellTileCm", m_SwellTileCm);
		LoadMemberValue(wrapper, "rippleTileCm", m_RippleTileCm);
		LoadMemberValue(wrapper, "frameCount", m_FrameCount);
		LoadMemberValue(wrapper, "loopSeconds", m_LoopSeconds);

		LoadMemberValue(wrapper, "waterLevel", m_WaterLevel);

		LoadMemberValue(wrapper, "finiteSizeCm", m_FiniteSizeCm);
		LoadMemberValue(wrapper, "finiteResolution", m_FiniteResolution);
		LoadMemberValue(wrapper, "ringCellSizeCm", m_RingCellSizeCm);
		LoadMemberValue(wrapper, "ringCells", m_RingCells);
		LoadMemberValue(wrapper, "ringCount", m_RingCount);
		LoadMemberValue(wrapper, "skirtRadiusCm", m_SkirtRadiusCm);

		LoadMemberValue(wrapper, "absorbColor", m_AbsorbColor);
		LoadMemberValue(wrapper, "scatterColor", m_ScatterColor);
		LoadMemberValue(wrapper, "roughness", m_Roughness);
		LoadMemberValue(wrapper, "normalStrength", m_NormalStrength);
		LoadMemberValue(wrapper, "foamAmount", m_FoamAmount);
		LoadMemberValue(wrapper, "shoreFoamDepthCm", m_ShoreFoamDepthCm);
		LoadMemberValue(wrapper, "ssr", m_Ssr);

		LoadMemberValue(wrapper, "debugView", m_DebugView);

		m_Waves.reset();
		m_ResolvedSource = 0;
		m_ResolvedReason = "unresolved";
		m_MeshesDirty = true;
		return true;
	}

	void OceanComponent::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);

		SaveKey(wrapper, "type"); SaveValue(wrapper, "OceanComponent");
		SaveKey(wrapper, "name"); SaveValue(wrapper, m_Name);
		SaveKey(wrapper, "mode"); SaveValue(wrapper, (unsigned int)m_Mode);
		SaveKey(wrapper, "waveSource"); SaveValue(wrapper, (unsigned int)m_WaveSource);
		SaveKey(wrapper, "waveAsset"); SaveValue(wrapper, m_WaveAssetPath);

		SaveKey(wrapper, "seed"); SaveValue(wrapper, m_Seed);
		SaveKey(wrapper, "windSpeed"); SaveValue(wrapper, m_WindSpeed);
		SaveKey(wrapper, "windDirectionDeg"); SaveValue(wrapper, m_WindDirectionDeg);
		SaveKey(wrapper, "fetchCm"); SaveValue(wrapper, m_FetchCm);
		SaveKey(wrapper, "choppiness"); SaveValue(wrapper, m_Choppiness);
		SaveKey(wrapper, "swellResolution"); SaveValue(wrapper, m_SwellResolution);
		SaveKey(wrapper, "rippleResolution"); SaveValue(wrapper, m_RippleResolution);
		SaveKey(wrapper, "swellTileCm"); SaveValue(wrapper, m_SwellTileCm);
		SaveKey(wrapper, "rippleTileCm"); SaveValue(wrapper, m_RippleTileCm);
		SaveKey(wrapper, "frameCount"); SaveValue(wrapper, m_FrameCount);
		SaveKey(wrapper, "loopSeconds"); SaveValue(wrapper, m_LoopSeconds);

		SaveKey(wrapper, "waterLevel"); SaveValue(wrapper, m_WaterLevel);

		SaveKey(wrapper, "finiteSizeCm"); SaveValue(wrapper, m_FiniteSizeCm);
		SaveKey(wrapper, "finiteResolution"); SaveValue(wrapper, m_FiniteResolution);
		SaveKey(wrapper, "ringCellSizeCm"); SaveValue(wrapper, m_RingCellSizeCm);
		SaveKey(wrapper, "ringCells"); SaveValue(wrapper, m_RingCells);
		SaveKey(wrapper, "ringCount"); SaveValue(wrapper, m_RingCount);
		SaveKey(wrapper, "skirtRadiusCm"); SaveValue(wrapper, m_SkirtRadiusCm);

		SaveKey(wrapper, "absorbColor"); SaveValue(wrapper, m_AbsorbColor);
		SaveKey(wrapper, "scatterColor"); SaveValue(wrapper, m_ScatterColor);
		SaveKey(wrapper, "roughness"); SaveValue(wrapper, m_Roughness);
		SaveKey(wrapper, "normalStrength"); SaveValue(wrapper, m_NormalStrength);
		SaveKey(wrapper, "foamAmount"); SaveValue(wrapper, m_FoamAmount);
		SaveKey(wrapper, "shoreFoamDepthCm"); SaveValue(wrapper, m_ShoreFoamDepthCm);
		SaveKey(wrapper, "ssr"); SaveValue(wrapper, m_Ssr);

		SaveKey(wrapper, "debugView"); SaveValue(wrapper, m_DebugView);

		if (object) EndObject(wrapper);
	}

	Component::Ptr OceanComponent::Clone() const
	{
		auto clone = OceanComponent::Create();
		clone->m_Name = m_Name;
		clone->m_Mode = m_Mode;
		clone->m_WaveSource = m_WaveSource;
		clone->m_WaveAssetPath = m_WaveAssetPath;
		clone->m_Seed = m_Seed;
		clone->m_WindSpeed = m_WindSpeed;
		clone->m_WindDirectionDeg = m_WindDirectionDeg;
		clone->m_FetchCm = m_FetchCm;
		clone->m_Choppiness = m_Choppiness;
		clone->m_SwellResolution = m_SwellResolution;
		clone->m_RippleResolution = m_RippleResolution;
		clone->m_SwellTileCm = m_SwellTileCm;
		clone->m_RippleTileCm = m_RippleTileCm;
		clone->m_FrameCount = m_FrameCount;
		clone->m_LoopSeconds = m_LoopSeconds;
		clone->m_WaterLevel = m_WaterLevel;
		clone->m_FiniteSizeCm = m_FiniteSizeCm;
		clone->m_FiniteResolution = m_FiniteResolution;
		clone->m_RingCellSizeCm = m_RingCellSizeCm;
		clone->m_RingCells = m_RingCells;
		clone->m_RingCount = m_RingCount;
		clone->m_SkirtRadiusCm = m_SkirtRadiusCm;
		clone->m_AbsorbColor = m_AbsorbColor;
		clone->m_ScatterColor = m_ScatterColor;
		clone->m_Roughness = m_Roughness;
		clone->m_NormalStrength = m_NormalStrength;
		clone->m_FoamAmount = m_FoamAmount;
		clone->m_ShoreFoamDepthCm = m_ShoreFoamDepthCm;
		clone->m_Ssr = m_Ssr;
		clone->m_DebugView = m_DebugView;
		return clone;
	}

	void OceanComponent::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);

		if (m_Mode == Mode::Infinite)
		{
			BoxBounds aabb;
			aabb.SetInfinite(true);
			node->SetModelAABB(aabb);
		}
		else
		{
			float half = m_FiniteSizeCm * 0.5f;
			node->SetModelAABB(BoxBounds(
				Vector4(-half, -500.0f, -half), Vector4(half, 500.0f, half)));
		}

		auto self = std::static_pointer_cast<OceanComponent>(node->GetComponent(typeid(OceanComponent)));
		if (self && m_UpdateKey == 0)
			m_UpdateKey = Engine::OnUpdate->Connect(self, &OceanComponent::TickUpdate);

		m_MeshesDirty = true;
		EnsureWaves();
	}

	void OceanComponent::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		if (m_UpdateKey != 0)
		{
			Engine::OnUpdate->Disconnect(m_UpdateKey);
			m_UpdateKey = 0;
		}
		m_Waves.reset();
		Component::OnDetaching(node);
	}

	void OceanComponent::TickUpdate(float dt)
	{
		m_WaveTime += dt;
	}

	void OceanComponent::SetMode(Mode mode)
	{
		m_Mode = mode;
		m_MeshesDirty = true;
		if (auto node = m_Owner.lock())
		{
			if (mode == Mode::Infinite)
			{
				BoxBounds aabb;
				aabb.SetInfinite(true);
				node->SetModelAABB(aabb);
			}
			else
			{
				float half = m_FiniteSizeCm * 0.5f;
				node->SetModelAABB(BoxBounds(
					Vector4(-half, -500.0f, -half), Vector4(half, 500.0f, half)));
			}
		}
	}

	void OceanComponent::SetWaveSource(WaveSource src)
	{
		m_WaveSource = src;

	}

	void OceanComponent::SetWaveAssetPath(const std::string &path)
	{
		m_WaveAssetPath = path;

	}

	void OceanComponent::EnsureWaves()
	{
		if (m_Waves)
			return;

		m_Waves = OceanWaves::Resolve(m_WaveAssetPath);
		if (m_Waves)
		{
			m_ResolvedSource = 1;
			m_ResolvedReason = "baked asset";
		}
		else
		{
			m_ResolvedSource = 0;
			m_ResolvedReason = "flat (missing asset)";
		}
	}

	float OceanComponent::WaveHeightAtWorld(float x, float z) const
	{
		float base = m_WaterLevel;
		if (auto node = m_Owner.lock())
			base += node->GetWorldPosition().y;
		if (!m_Waves)
			return base;
		return base + WaveSampler::HeightChoppyCorrected(*m_Waves, x, z, m_WaveTime);
	}

	// ------------------------------------------------------------ meshes

	Mesh::Ptr OceanComponent::BuildGrid(float sizeCm, int cells) const
	{
		auto mesh = Mesh::Create("ocean_grid");
		auto &pos = mesh->Positions.Data;
		auto &idx = mesh->Indices.Data;

		float half = sizeCm * 0.5f;
		float step = sizeCm / cells;
		for (int i = 0; i <= cells; i++)
		{
			for (int j = 0; j <= cells; j++)
			{
				pos.push_back(-half + j * step);
				pos.push_back(0.0f);
				pos.push_back(-half + i * step);
			}
		}
		for (int i = 0; i < cells; i++)
		{
			for (int j = 0; j < cells; j++)
			{
				unsigned int a = i * (cells + 1) + j;
				unsigned int b = a + 1;
				unsigned int c = a + cells + 1;
				unsigned int d = c + 1;
				idx.insert(idx.end(), { a, c, b, b, c, d });
			}
		}

		mesh->CalculateAABB();
		// no UpdateBuffer here: builders run headless (exec/tests), the draw
		// path uploads on first use when a GL context is live
		return mesh;
	}

	Mesh::Ptr OceanComponent::BuildRingFrame(float innerR, float outerR, float cell) const
	{
		// 4 strips around the hole [-innerR,innerR]^2 inside [-outerR,outerR]^2
		auto mesh = Mesh::Create("ocean_ring");
		auto &pos = mesh->Positions.Data;
		auto &idx = mesh->Indices.Data;

		struct Strip { float x0, z0, x1, z1; };
		const Strip strips[4] = {
			{ -outerR, innerR,  outerR, outerR },  // far +z
			{ -outerR, -outerR, outerR, -innerR }, // near -z
			{ -outerR, -innerR, -innerR, innerR }, // left
			{ innerR, -innerR, outerR, innerR },   // right
		};

		for (const Strip &s : strips)
		{
			int cellsX = std::max(1, (int)std::round((s.x1 - s.x0) / cell));
			int cellsZ = std::max(1, (int)std::round((s.z1 - s.z0) / cell));
			unsigned int base = (unsigned int)(pos.size() / 3);
			for (int i = 0; i <= cellsZ; i++)
			{
				for (int j = 0; j <= cellsX; j++)
				{
					pos.push_back(s.x0 + (s.x1 - s.x0) * j / cellsX);
					pos.push_back(0.0f);
					pos.push_back(s.z0 + (s.z1 - s.z0) * i / cellsZ);
				}
			}
			for (int i = 0; i < cellsZ; i++)
			{
				for (int j = 0; j < cellsX; j++)
				{
					unsigned int a = base + i * (cellsX + 1) + j;
					unsigned int b = a + 1;
					unsigned int c = a + cellsX + 1;
					unsigned int d = c + 1;
					idx.insert(idx.end(), { a, c, b, b, c, d });
				}
			}
		}

		mesh->CalculateAABB();
		// no UpdateBuffer here: builders run headless (exec/tests), the draw
		// path uploads on first use when a GL context is live
		return mesh;
	}

	Mesh::Ptr OceanComponent::BuildSkirt(float innerR, float outerR) const
	{
		// coarse radial fan from the last ring out to the horizon
		auto mesh = Mesh::Create("ocean_skirt");
		auto &pos = mesh->Positions.Data;
		auto &idx = mesh->Indices.Data;

		const int segments = 64;
		const int rings = 4;
		for (int r = 0; r <= rings; r++)
		{
			// exponential spacing pushes detail inward
			float t = (float)r / rings;
			float radius = innerR * std::pow(outerR / innerR, t);
			for (int s = 0; s <= segments; s++)
			{
				float ang = (float)s / segments * 6.28318530718f;
				pos.push_back(radius * std::cos(ang));
				pos.push_back(0.0f);
				pos.push_back(radius * std::sin(ang));
			}
		}
		for (int r = 0; r < rings; r++)
		{
			for (int s = 0; s < segments; s++)
			{
				unsigned int a = r * (segments + 1) + s;
				unsigned int b = a + 1;
				unsigned int c = a + segments + 1;
				unsigned int d = c + 1;
				// radial fans wind DOWNWARD with the grid convention
				// ({a,c,b}) - flip so the top face survives backface
				// culling, or the skirt never renders and the horizon
				// shows the sky's below-horizon row through it
				idx.insert(idx.end(), { a, b, c, b, d, c });
			}
		}

		mesh->CalculateAABB();
		// no UpdateBuffer here: builders run headless (exec/tests), the draw
		// path uploads on first use when a GL context is live
		return mesh;
	}

	Mesh::Ptr OceanComponent::BuildStitchRing(float holeHalf, float coarseCell) const
	{
		// Transition band from the hole square [-holeHalf, holeHalf]^2 outward
		// by one coarseCell, stitching the fine inner edge (coarseCell/2
		// spacing) to the coarse outer edge (coarseCell spacing) with 3
		// triangles per coarse segment + 2 per corner fan. Without this the
		// pieces just abut: the fine edge's midpoint vertices displace with
		// the wave while the coarse edge chords straight past them - grazing
		// views show slivers. Every stitch vertex sits exactly on a neighbor
		// piece's edge, so the same world-space displacement coincides and
		// the surface is watertight.
		auto mesh = Mesh::Create("ocean_stitch");
		auto &pos = mesh->Positions.Data;
		auto &idx = mesh->Indices.Data;

		const float R = holeHalf;
		const float cc = coarseCell;
		const float fc = cc * 0.5f;
		const int segs = std::max(2, (int)std::round((2.0f * R) / cc)); // coarse segments per side
		// per-side frames: origin at each fine corner; u = walk dir; v = outward
		const float ox[4] = { -R,  R,  R, -R };  // fine corner origins (x,z)
		const float oz[4] = {  R,  R, -R, -R };
		const float ux[4] = { 1.0f, 0.0f, -1.0f, 0.0f };
		const float uz[4] = { 0.0f, -1.0f, 0.0f, 1.0f };
		const float vx[4] = { 0.0f, 1.0f, 0.0f, -1.0f }; // outward (x,z)
		const float vz[4] = { 1.0f, 0.0f, -1.0f, 0.0f };

		unsigned int fineBase[4];
		unsigned int coarseBase[4];
		for (int s = 0; s < 4; ++s)
		{
			fineBase[s] = (unsigned int)(pos.size() / 3);
			for (int j = 0; j <= 2 * segs; ++j) // fine edge, fc spacing
			{
				float t = (float)(j) * fc;
				pos.push_back(ox[s] + ux[s] * t);
				pos.push_back(0.0f);
				pos.push_back(oz[s] + uz[s] * t);
			}
			coarseBase[s] = (unsigned int)(pos.size() / 3);
			for (int j = 0; j <= segs; ++j) // coarse edge, cc spacing, one cell out
			{
				float t = (float)(j) * cc;
				pos.push_back(ox[s] + vx[s] * cc + ux[s] * t);
				pos.push_back(0.0f);
				pos.push_back(oz[s] + vz[s] * cc + uz[s] * t);
			}
		}
		for (int s = 0; s < 4; ++s)
		{
			for (int j = 0; j < segs; ++j)
			{
				unsigned int f0 = fineBase[s] + 2 * j;
				unsigned int c0 = coarseBase[s] + j;
				// 3-triangle stitch: (F0,C0,F1) (F1,C0,C1) (F1,C1,F2)
				idx.insert(idx.end(), { f0, c0, f0 + 1, f0 + 1, c0, c0 + 1, f0 + 1, c0 + 1, f0 + 2 });
			}
			// corner fan: fine corner, this side's coarse end, the diagonal
			// corner (appended), the next side's coarse start
			unsigned int fEnd = fineBase[s] + 2 * segs;
			unsigned int cEnd = coarseBase[s] + segs;
			unsigned int cNext = coarseBase[(s + 1) % 4];
			unsigned int diag = (unsigned int)(pos.size() / 3);
			int sn = (s + 1) % 4;
			float fx = ox[s] + ux[s] * (2.0f * R); // the fine corner position
			float fz = oz[s] + uz[s] * (2.0f * R);
			pos.push_back(fx + (vx[s] + vx[sn]) * cc);
			pos.push_back(0.0f);
			pos.push_back(fz + (vz[s] + vz[sn]) * cc);
			idx.insert(idx.end(), { fEnd, cEnd, diag, fEnd, diag, cNext });
		}

		mesh->CalculateAABB();
		return mesh;
	}

	void OceanComponent::BuildMeshes()
	{
		m_RingPieces.clear();
		m_FiniteMesh.reset();

		if (m_Mode == Mode::Finite)
		{
			m_FiniteMesh = BuildGrid(m_FiniteSizeCm, m_FiniteResolution);
			// no rings here: fade detail with view distance past the grid
			// extents so grazing far views don't alias
			m_FadeRanges = Vector4(m_FiniteSizeCm, m_FiniteSizeCm * 2.0f,
				m_FiniteSizeCm * 2.0f, m_FiniteSizeCm * 8.0f);
		}
		else
		{
			int n = m_RingCells;
			float cell = m_RingCellSizeCm;
			float radius = n * cell * 0.5f;

			// center grid
			RingPiece center;
			center.MeshPtr = BuildGrid(radius * 2.0f, n);
			center.CellSizeCm = cell;
			center.RadiusCm = radius;
			m_RingPieces.push_back(center);

			// concentric frames, doubling cell size per ring; a stitch ring
			// between each pair (the fine edge's midpoint vertices would
			// otherwise displace past the coarse edge's chord = grazing
			// slivers). No tucks: every shared edge coincides vertex-for-vertex.
			const float centerRadius = radius;
			for (int k = 0; k < m_RingCount; k++)
			{
				float coarse = cell * 2.0f;
				float outer = radius * 2.0f;
				RingPiece stitch;
				stitch.MeshPtr = BuildStitchRing(radius, coarse);
				stitch.CellSizeCm = cell;
				stitch.RadiusCm = radius + coarse;
				m_RingPieces.push_back(stitch);
				RingPiece ring;
				ring.MeshPtr = BuildRingFrame(radius + coarse, outer, coarse);
				ring.CellSizeCm = coarse;
				ring.RadiusCm = outer;
				m_RingPieces.push_back(ring);
				radius = outer;
				cell = coarse;
			}

			RingPiece skirt;
			// inner radius tucks 5% under the last ring's square footprint so
			// the circle/square join never opens (edge midpoints included)
			skirt.MeshPtr = BuildSkirt(radius * 0.95f, m_SkirtRadiusCm);
			skirt.CellSizeCm = radius;
			skirt.RadiusCm = m_SkirtRadiusCm;
			skirt.YOffset = -2.0f; // the only remaining tuck (the skirt overlaps)
			skirt.IsSkirt = true;
			m_RingPieces.push_back(skirt);

			// radial fade ranges from the ring radii: ripple is fully out AT
			// the ring-1 boundary (the 8 m band never straddles a
			// T-junction); swell fades to 0 across the last ring - the
			// skirt's ~250 m fan sampling aliases the 100 m swell into glint
			// dashes, and at 6 km the residual silhouette is subpixel anyway
			m_FadeRanges = Vector4(centerRadius * 0.5f, centerRadius,
				radius * 0.5f, radius);
		}

		m_MeshesDirty = false;
	}

	void OceanComponent::UpdateCameraFollow(const Vector4 &camPos)
	{
		if (m_MeshesDirty)
			BuildMeshes();

		if (m_Mode != Mode::Infinite)
			return;

		Vector4 nodePos(0.0f, 0.0f, 0.0f);
		if (auto node = m_Owner.lock())
			nodePos = node->GetWorldPosition();

		// snap each piece's origin to a whole multiple of the CENTER (finest)
		// cell: every ring cell is a multiple of it, so shared edges coincide
		// exactly. Snapping each piece to its own cell left the origins up to
		// half a coarse cell apart in world space - visible slits at grazing
		// angles. The wave field is world-pure, so nothing swims.
		for (auto &piece : m_RingPieces)
		{
			float ox = std::floor((camPos.x - nodePos.x) / m_RingCellSizeCm) * m_RingCellSizeCm + nodePos.x;
			float oz = std::floor((camPos.z - nodePos.z) / m_RingCellSizeCm) * m_RingCellSizeCm + nodePos.z;
			piece.Origin = Vector4(ox, nodePos.y, oz);
		}
	}

	const std::vector<OceanComponent::RingPiece> &OceanComponent::GetRingPieces()
	{
		if (m_MeshesDirty)
			BuildMeshes();
		return m_RingPieces;
	}

	Mesh::Ptr OceanComponent::GetFiniteMesh()
	{
		if (m_MeshesDirty)
			BuildMeshes();
		return m_FiniteMesh;
	}

	unsigned int OceanComponent::GetOceanVertexCount()
	{
		unsigned int total = 0;
		if (m_Mode == Mode::Finite)
		{
			if (auto mesh = GetFiniteMesh())
				total = (unsigned int)(mesh->Positions.Data.size() / 3);
		}
		else
		{
			for (const auto &piece : GetRingPieces())
				if (piece.MeshPtr)
					total += (unsigned int)(piece.MeshPtr->Positions.Data.size() / 3);
		}
		return total;
	}
}
