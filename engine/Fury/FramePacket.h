#ifndef _FURY_FRAME_PACKET_H_
#define _FURY_FRAME_PACKET_H_

// FramePacket: the per-frame render snapshot handed from game thread to
// render thread (Unity-style render-thread split). The game thread gathers
// the packet at Pipeline::Execute time; the render thread executes it
// (batching, passes, postfx, ImGui GL, present) WITHOUT dereferencing
// SceneNode/Component objects -- every value the passes read is copied here,
// and object references (Mesh/Material/Texture/Shader) are quasi-immutable
// shared resources whose GL faces are render-thread-owned.
//
// Threading-off mode runs the identical packet path inline on the main
// thread, so there is exactly one rendering implementation.

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Fury/BoxBounds.h"
#include "Fury/Color.h"
#include "Fury/EnumUtil.h"
#include "Fury/Frustum.h"
#include "Fury/Matrix4.h"
#include "Fury/ParticleModules.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/Vector4.h"

namespace fury
{
	class Material;

	class Mesh;

	class Texture;

	class Shader;

	class PostProcessEffect;

	class UniformBase;

	class RenderTarget;

	class Pipeline;

	class OceanWaves;

	class SkyAtmosphere;

	// Everything pass code reads from the camera node.
	struct PacketCamera
	{
		Matrix4 worldMatrix;
		Matrix4 invertWorldMatrix;   // the VIEW matrix (engine naming)
		Matrix4 projectionMatrix;
		Vector4 worldPos;
		float nearClip = 0.0f;
		float farClip = 0.0f;
		float fov = 0.0f;
		float shadowFar = 0.0f;
		Frustum frustum;
		BoxBounds shadowBounds;      // Camera::GetShadowBounds(false)
		bool hasShadowBounds = false;
		bool valid = false;
	};

	// One resolved opaque/transparent draw: LOD + billboard already selected
	// at gather time. Skinned units carry a copied joint palette.
	struct PacketUnit
	{
		std::shared_ptr<Mesh> mesh;

		std::shared_ptr<Material> material;

		int subMesh = -1;

		int lodIndex = 0;

		bool billboard = false;

		// Opaque cache key (the source node's id); never dereferenced.
		std::uint64_t nodeKey = 0;

		Matrix4 worldMatrix;

		BoxBounds worldBounds;

		Vector4 worldPos;

		// Skinned units: copied joint final matrices (empty for static).
		std::vector<Matrix4> skinPalette;
	};

	// One shadow caster: static/skinned mesh form, or an indirection into
	// the packet's instanced list (instancedIndex >= 0).
	struct PacketCaster
	{
		std::shared_ptr<Mesh> mesh;   // shadow-LOD mesh

		std::vector<std::shared_ptr<Material>> materials;

		Matrix4 worldMatrix;

		BoxBounds worldBounds;

		bool skinned = false;

		std::vector<Matrix4> skinPalette;

		int instancedIndex = -1;
	};

	// One LOD tier's visible instance stream (moved out of
	// InstancedMeshRender so packets and components share the shape).
	struct InstanceBatch
	{
		unsigned int LodTier = 0;

		bool Billboard = false;

		std::vector<Matrix4> WorldMatrices;
	};

	// Instanced (ISM/HISM) render caches: the big per-instance arrays,
	// rebuilt by the component on dirty and shared immutably afterwards.
	struct InstancedRenderCaches
	{
		std::vector<Matrix4> worlds;   // per-instance world matrices

		std::vector<BoxBounds> aabbs;  // per-instance world AABBs
	};

	// Instanced (ISM/HISM) component snapshot. The per-instance arrays are
	// a shared_ptr-const block the component rebuilds on dirty only, so
	// gather copies ~nothing for static vegetation. Shadow draws reuse
	// caches->worlds at shadowLodTier.
	struct PacketInstanced
	{
		std::uint64_t nodeKey = 0;

		std::shared_ptr<Mesh> mesh;   // LOD chain root

		std::vector<std::shared_ptr<Material>> materials;

		bool castShadows = true;

		float cullDistance = 0.0f;

		bool hierarchical = true;

		unsigned int shadowLodTier = 0;

		BoxBounds aggregateAABB;

		std::shared_ptr<const InstancedRenderCaches> caches;

		// Render-side output: visible batches built during packet execution.
		std::vector<InstanceBatch> batches;
	};

	// Per-light copy + game-thread-computed shadow plan. Shadow MAP
	// allocation/drawing is render-side; the plan is pure CPU data.
	struct PacketLight
	{
		std::uint64_t nodeKey = 0;

		std::string name;   // FURY_SHADOW_DEBUG dumps only

		LightType type = LightType::POINT;

		Color color = Color::White;

		float intensity = 1.0f;

		float innerAngle = 0.0f;

		float outterAngle = 0.0f;

		float falloff = 0.0f;

		float radius = 0.0f;

		float effectiveRadius = 0.0f;

		bool castShadows = false;

		Matrix4 worldMatrix;

		Matrix4 invertWorldMatrix;

		Vector4 worldPos;

		Vector4 worldDir;   // world * (0,-1,0,0), normalized

		std::shared_ptr<Mesh> volumeMesh;

		// Shadow plan (gather-computed; all matrices in light space):
		Matrix4 lightMatrix;                 // rot(x,90deg) * invertWorld

		std::vector<PacketCaster> casters;   // dir-single / point / spot

		Matrix4 singleProj;                  // dir-single / spot crop matrix

		std::array<Matrix4, 4> csmProj;      // CSM crop matrices per split

		std::array<std::vector<PacketCaster>, 4> csmCasters;

		std::array<float, 4> csmSplits = { 0.0f, 0.0f, 0.0f, 0.0f };

		int csmMapSize = 1024;

		std::array<Matrix4, 6> cubeViews;    // point: per-face view matrices
	};

	// Baked CPU-billboard particle batch. The component double-buffers its
	// dynamic mesh by frame parity: gather bakes buffer[N%2] while the
	// render thread uploads/draws buffer[(N-1)%2] -- no channel copies.
	struct PacketParticles
	{
		std::uint64_t nodeKey = 0;

		std::string name;

		Vector4 worldPos;

		Matrix4 worldMatrix;

		ParticleBlend blendMode = ParticleBlend::ALPHA;

		// The parity mesh buffer baked at gather (positions/uvs/indices).
		std::shared_ptr<Mesh> mesh;

		// Resolved at gather (system RendererModule material lookup).
		std::shared_ptr<Texture> diffuse;

		// Average of the first live particle colors (u_Tint).
		Color tint = Color::White;

		bool receiveShadows = false;

		unsigned int aliveCount = 0;
	};

	struct PacketOceanPiece
	{
		std::shared_ptr<Mesh> mesh;

		Vector4 origin;

		float yOffset = 0.0f;

		bool isSkirt = false;
	};

	struct PacketOcean
	{
		std::uint64_t nodeKey = 0;

		Vector4 nodePos;

		float waterLevel = 0.0f;

		float waveTime = 0.0f;

		float roughness = 0.0f;

		float normalStrength = 1.0f;

		float foamAmount = 0.0f;

		float shoreFoamDepthCm = 0.0f;

		float windSpeed = 0.0f;

		float skirtRadiusCm = 0.0f;

		Color absorb = Color::White;

		Color scatter = Color::White;

		Vector4 fadeRanges;

		unsigned int debugView = 0;

		bool finite = false;

		std::shared_ptr<OceanWaves> waves;

		std::vector<PacketOceanPiece> pieces;   // infinite mode

		std::shared_ptr<Mesh> finiteMesh;       // finite mode
	};

	// Sky snapshot: full param + runtime-state copy so the render thread
	// never reads SkyAtmosphere fields that the game thread's TickUpdate
	// writes. GL resources (LUT passes/shaders/textures) stay
	// render-thread-owned on the component.
	struct PacketSky
	{
		bool valid = false;   // no active sky this frame

		bool enabled = false;

		// Full parameter/runtime snapshot (SkyAtmosphere::SnapshotParams,
		// with viewHeightKm filled from the gather camera).
		SkyParams params;

		std::shared_ptr<Texture> transmittanceLut;

		std::shared_ptr<Texture> multiScatterLut;

		std::shared_ptr<Texture> skyViewLut;

		std::shared_ptr<Texture> cameraVolume;

		std::shared_ptr<Texture> cloudTarget;

		std::shared_ptr<Texture> moonTexture;

		// The component itself, for the render-side EnsureLutsRender entry
		// only (touches GL-resource members exclusively). Null when invalid.
		std::shared_ptr<SkyAtmosphere> owner;
	};

	// Debug-draw snapshot (bounds/grid/buoyancy), gathered only when the
	// matching pipeline switch is on.
	struct PacketDebug
	{
		std::vector<BoxBounds> meshBounds;      // MESH_BOUNDS

		std::vector<BoxBounds> customBoxes;     // CUSTOM_BOUNDS

		std::vector<Frustum> customFrusta;

		std::vector<std::pair<BoxBounds, unsigned int>> octreeBounds;   // OCTREE_BOUNDS (bounds, depth)

		struct BuoyMark
		{
			Vector4 center;

			float radius = 0.0f;

			Color color = Color::White;
		};

		std::vector<BuoyMark> buoyMarks;        // BUOYANCY_DEBUG
	};

	// The frame. Produced by PrelightPipeline::GatherFrame on the game
	// thread; consumed by PrelightPipeline::ExecutePacket on the GL thread.
	struct FramePacket
	{
		std::uint64_t frameIndex = 0;

		std::shared_ptr<Pipeline> pipeline;

		PacketCamera camera;

		std::vector<PacketUnit> opaqueUnits;

		std::vector<PacketUnit> transparentUnits;

		std::vector<PacketLight> lights;

		std::vector<PacketInstanced> instanced;

		std::vector<PacketParticles> particles;

		std::vector<PacketOcean> oceans;

		PacketSky sky;

		PacketDebug debug;

		std::bitset<32> switches;

		bool hdrMode = false;

		// Resolved postprocess chain snapshot (ApplyRenderSettings output).
		std::vector<std::shared_ptr<PostProcessEffect>> chain;

		std::vector<std::unordered_map<std::string, std::shared_ptr<UniformBase>>> chainOverrides;

		// RenderSettings fields the passes read directly.
		Vector4 windParams;

		std::array<float, 4> csmSplits = { 0.0f, 0.0f, 0.0f, 0.0f };

		int csmMapSize = 1024;

		float engineTime = 0.0f;   // WIND u_time

		int windowW = 0;

		int windowH = 0;

		// Editor viewport target (null in the player / when rendering to
		// the default framebuffer). Non-owning: the editor owns it and
		// guarantees lifetime (load/teardown flushes cover swaps).
		RenderTarget *renderTarget = nullptr;

		// Post-scene overlay jobs (editor selection viz, pick id pass),
		// executed on the render thread after the passes.
		std::vector<std::function<void()>> overlayJobs;

		// ImGui draw-data snapshot, attached at loop tail (GuiFrameData is
		// Gui-internal; opaque here).
		std::shared_ptr<void> guiFrame;

		// Render-side intra-frame state: shadow results per light index,
		// filled by the light pass, read by transparent/ocean/particles.
		struct ShadowResult
		{
			std::shared_ptr<Texture> texture;

			Matrix4 single;

			std::vector<Matrix4> csm;

			Vector4 shadowFar;
		};

		std::vector<ShadowResult> shadowResults;

		std::vector<std::shared_ptr<Texture>> frameShadowTemps;

		void Reset();
	};

	// Builds the packet camera from the live camera node (game thread only).
	PacketCamera BuildPacketCamera(const std::shared_ptr<SceneNode> &camNode);

	// Shadow-pass LOD policy: billboard-terminated chains (kraut trees)
	// demote to the deepest non-billboard tier -- their LOD 0 is the
	// expensive full-detail mesh and the deep tier shadows fine.
	// Plain LOD chains (terrain, meshopt props) keep LOD 0: their
	// coarse tiers deviate from the rendered surface by meters, which
	// reads as self-shadow blotches on open slopes.
	FURY_API std::shared_ptr<Mesh> PickShadowLodMesh(const std::shared_ptr<Mesh> &base);

	// Per-instance frustum culling + LOD bucketing over packet data. Runs on
	// the render thread during packet execution (and on the game thread for
	// the component's legacy BuildVisibleBatches entry).
	void BuildInstancedBatches(const PacketInstanced &pk, const Frustum &frustum,
		const PacketCamera &cam, std::vector<InstanceBatch> &outBatches);
}

#endif // _FURY_FRAME_PACKET_H_
