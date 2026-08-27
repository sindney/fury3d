#ifndef _FURY_OCEAN_COMPONENT_H_
#define _FURY_OCEAN_COMPONENT_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/Color.h"
#include "Fury/Component.h"
#include "Fury/Mesh.h"
#include "Fury/Vector4.h"

namespace fury
{
	class OceanWaves;

	// FFT ocean surface (change: add-fft-ocean). Replays looping baked (or
	// GPU-generated, GL 4.3+) wave bands onto either a finite grid (inland
	// water) or an infinite camera-centered set of static-density rings.
	// Geometry is displaced entirely in the vertex shader; this component
	// owns the meshes, the wave asset, and the wave clock. Buoyancy reads
	// the same data through WaveSampler via WaveHeightAtWorld.
	class FURY_API OceanComponent : public Component
	{
	public:

		enum class Mode : unsigned int
		{
			Finite = 0,   // bounded grid on the node transform (lakes)
			Infinite = 1, // camera-following ring LOD + horizon skirt
		};

		enum class WaveSource : unsigned int
		{
			// GPU generation was removed (change: remove-gpu-ocean-generation).
			// The Auto value deserializes legacy .bin files and is treated
			// as Baked - the component always resolves the baked asset.
			Auto = 0,  // legacy: equivalent to Baked now
			Baked = 1,
		};

		// One drawable piece of the infinite mode: the dense center grid,
		// each concentric ring frame, or the far skirt. Vertices are local
		// to Origin (all pieces snap to the same finest-cell grid, so shared
		// edges coincide exactly - no tuck; a vertical step only glints).
		// Band fades are NOT per piece: the vertex shader applies a smooth
		// camera-radial fade (GetFadeRanges) - per-piece constants read as
		// V-shaped shading seams at the square ring boundaries.
		struct RingPiece
		{
			Mesh::Ptr MeshPtr;
			float CellSizeCm = 100.0f;
			float RadiusCm = 0.0f;  // outer half-extent
			float YOffset = 0.0f;
			Vector4 Origin = Vector4(0.0f, 0.0f, 0.0f);
			bool IsSkirt = false;
		};

		typedef std::shared_ptr<OceanComponent> Ptr;

		static Ptr Create();

		OceanComponent(const std::string &name = "OceanComponent");

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
		Component::Ptr Clone() const override;

		const std::string &GetName() const { return m_Name; }
		void SetName(const std::string &name) { m_Name = name; }

		Mode GetMode() const { return m_Mode; }
		void SetMode(Mode mode);

		WaveSource GetWaveSource() const { return m_WaveSource; }
		void SetWaveSource(WaveSource src);

		const std::string &GetWaveAssetPath() const { return m_WaveAssetPath; }
		void SetWaveAssetPath(const std::string &path);

		float GetWaterLevel() const { return m_WaterLevel; }
		void SetWaterLevel(float v) { m_WaterLevel = v; }

		// spectrum params (GPU generation path; documented in ocean.json)
		int GetSeed() const { return m_Seed; }
		void SetSeed(int v) { m_Seed = v; }
		float GetWindSpeed() const { return m_WindSpeed; }
		void SetWindSpeed(float v) { m_WindSpeed = v; }
		float GetWindDirectionDeg() const { return m_WindDirectionDeg; }
		void SetWindDirectionDeg(float v) { m_WindDirectionDeg = v; }
		float GetFetchCm() const { return m_FetchCm; }
		void SetFetchCm(float v) { m_FetchCm = v; }
		float GetChoppiness() const { return m_Choppiness; }
		void SetChoppiness(float v) { m_Choppiness = v; }
		int GetSwellResolution() const { return m_SwellResolution; }
		void SetSwellResolution(int v) { m_SwellResolution = v; }
		int GetRippleResolution() const { return m_RippleResolution; }
		void SetRippleResolution(int v) { m_RippleResolution = v; }
		float GetSwellTileCm() const { return m_SwellTileCm; }
		void SetSwellTileCm(float v) { m_SwellTileCm = v; }
		float GetRippleTileCm() const { return m_RippleTileCm; }
		void SetRippleTileCm(float v) { m_RippleTileCm = v; }
		int GetFrameCount() const { return m_FrameCount; }
		void SetFrameCount(int v) { m_FrameCount = v; }
		float GetLoopSeconds() const { return m_LoopSeconds; }
		void SetLoopSeconds(float v) { m_LoopSeconds = v; }

		// geometry params
		float GetFiniteSizeCm() const { return m_FiniteSizeCm; }
		void SetFiniteSizeCm(float v) { m_FiniteSizeCm = v; m_MeshesDirty = true; }
		int GetFiniteResolution() const { return m_FiniteResolution; }
		void SetFiniteResolution(int v) { m_FiniteResolution = v; m_MeshesDirty = true; }
		float GetRingCellSizeCm() const { return m_RingCellSizeCm; }
		void SetRingCellSizeCm(float v) { m_RingCellSizeCm = v; m_MeshesDirty = true; }
		int GetRingCells() const { return m_RingCells; }
		void SetRingCells(int v) { m_RingCells = v; m_MeshesDirty = true; }
		int GetRingCount() const { return m_RingCount; }
		void SetRingCount(int v) { m_RingCount = v; m_MeshesDirty = true; }
		float GetSkirtRadiusCm() const { return m_SkirtRadiusCm; }
		void SetSkirtRadiusCm(float v) { m_SkirtRadiusCm = v; m_MeshesDirty = true; }

		// shading params
		const Color &GetAbsorbColor() const { return m_AbsorbColor; }
		void SetAbsorbColor(const Color &c) { m_AbsorbColor = c; }
		const Color &GetScatterColor() const { return m_ScatterColor; }
		void SetScatterColor(const Color &c) { m_ScatterColor = c; }
		float GetRoughness() const { return m_Roughness; }
		void SetRoughness(float v) { m_Roughness = v; }
		float GetNormalStrength() const { return m_NormalStrength; }
		void SetNormalStrength(float v) { m_NormalStrength = v; }
		float GetFoamAmount() const { return m_FoamAmount; }
		void SetFoamAmount(float v) { m_FoamAmount = v; }
		float GetShoreFoamDepthCm() const { return m_ShoreFoamDepthCm; }
		void SetShoreFoamDepthCm(float v) { m_ShoreFoamDepthCm = v; }
		bool GetSsrEnabled() const { return m_Ssr; }
		void SetSsrEnabled(bool v) { m_Ssr = v; }

		// 0 off, 1 foam mask, 2 displacement heatmap, 3 ring wireframe
		unsigned int GetDebugView() const { return m_DebugView; }
		void SetDebugView(unsigned int v) { m_DebugView = v; }

		// Resolved wave source for the editor status line: 0 none/flat,
		// 1 baked asset, 2 GPU compute. GetResolvedReason explains why.
		int GetResolvedSource() const { return m_ResolvedSource; }
		const std::string &GetResolvedReason() const { return m_ResolvedReason; }

		std::shared_ptr<OceanWaves> GetWaves() const
		{
			// lazy re-resolve on path change: without this the picker left the
			// ocean flat until the next scene reload
			const_cast<OceanComponent*>(this)->EnsureWaves();
			return m_Waves;
		}

		// Wave clock. Advances on Engine::OnUpdate while attached.
		float GetWaveTime() const { return m_WaveTime; }
		void SetWaveTime(float t) { m_WaveTime = t; }

		// World-space water height incl. node transform + water level.
		// Choppy-corrected; this is the buoyancy/gameplay query.
		float WaveHeightAtWorld(float x, float z) const;

		// Meshes for the pipeline draw. UpdateCameraFollow must run first
		// (infinite mode): snaps ring origins to whole cells of camPos.
		void UpdateCameraFollow(const Vector4 &camPos);
		const std::vector<RingPiece> &GetRingPieces();
		Mesh::Ptr GetFiniteMesh();
		unsigned int GetOceanVertexCount();

		// Camera-radial band fade ranges in cm, derived from the ring radii
		// in BuildMeshes: x/y ripple start/end, z/w swell start/end. The
		// vertex shader smoothsteps displacement + band normals with these.
		const Vector4 &GetFadeRanges() const { return m_FadeRanges; }

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;
		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		void TickUpdate(float dt);

		// Load baked waves / trigger GPU generation per waveSource +
		// capability. Sets m_ResolvedSource/m_ResolvedReason.
		void EnsureWaves();

		void BuildMeshes();
		Mesh::Ptr BuildGrid(float sizeCm, int cells) const;
		Mesh::Ptr BuildRingFrame(float innerRadiusCm, float outerRadiusCm, float cellSizeCm) const;
		Mesh::Ptr BuildStitchRing(float holeHalfCm, float coarseCellCm) const;
		Mesh::Ptr BuildSkirt(float innerRadiusCm, float outerRadiusCm) const;

		std::string m_Name = "OceanComponent";

		Mode m_Mode = Mode::Infinite;
		WaveSource m_WaveSource = WaveSource::Auto;
		std::string m_WaveAssetPath = "Engine/Ocean/ocean.json"; // engine baseline bake

		int m_Seed = 7;
		float m_WindSpeed = 800.0f;      // cm/s
		float m_WindDirectionDeg = 35.0f;
		float m_FetchCm = 100000.0f;
		float m_Choppiness = 1.0f;
		int m_SwellResolution = 128;
		int m_RippleResolution = 64;
		float m_SwellTileCm = 10000.0f;
		float m_RippleTileCm = 800.0f;
		int m_FrameCount = 32;
		float m_LoopSeconds = 12.0f;

		float m_WaterLevel = 0.0f;

		float m_FiniteSizeCm = 10000.0f;
		int m_FiniteResolution = 128;

		float m_RingCellSizeCm = 100.0f;
		int m_RingCells = 64;
		int m_RingCount = 3;
		float m_SkirtRadiusCm = 600000.0f; // past the 5 km AP range: fully fogged

		Color m_AbsorbColor = Color(0.006f, 0.04f, 0.08f, 1.0f);
		Color m_ScatterColor = Color(0.025f, 0.11f, 0.15f, 1.0f);
		float m_Roughness = 0.12f;
		float m_NormalStrength = 1.0f;
		float m_FoamAmount = 0.6f;
		float m_ShoreFoamDepthCm = 300.0f;
		bool m_Ssr = true;

		unsigned int m_DebugView = 0;

		float m_WaveTime = 0.0f;
		size_t m_UpdateKey = 0;

		std::shared_ptr<OceanWaves> m_Waves;
		int m_ResolvedSource = 0;
		std::string m_ResolvedReason = "unresolved";

		bool m_MeshesDirty = true;
		Mesh::Ptr m_FiniteMesh;
		std::vector<RingPiece> m_RingPieces;
		Vector4 m_FadeRanges = Vector4(1600.0f, 3200.0f, 12800.0f, 25600.0f);
	};
}

#endif // _FURY_OCEAN_COMPONENT_H_
