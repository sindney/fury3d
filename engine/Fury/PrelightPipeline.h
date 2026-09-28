#ifndef _FURY_PRELIGHT_PIPELINE_H_
#define _FURY_PRELIGHT_PIPELINE_H_

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <initializer_list>

#include "Fury/Pipeline.h"
#include "Fury/Matrix4.h"

namespace fury
{
	class Texture;

	class Shader;

	class Pass;

	struct RenderUnit;

	struct FramePacket;

	struct PacketUnit;

	struct PacketLight;

	// Depth-only shader variant set for the vegetation pre-z pre-phase:
	// the prez_depth_* entries (shadow variants with LINEAR_DEPTH so the
	// EQUAL reuse matches the gbuffer's linear gl_FragDepth). Any variant
	// may be null (legacy pipeline JSONs lack them) -- Pick falls back.
	struct PreZDepthShaders
	{
		std::shared_ptr<Shader> Plain;
		std::shared_ptr<Shader> Skin;
		std::shared_ptr<Shader> AlphaTest;
		std::shared_ptr<Shader> AlphaTestWind;
		std::shared_ptr<Shader> Wind;
		std::shared_ptr<Shader> Inst;
		std::shared_ptr<Shader> InstAlphaTest;
		std::shared_ptr<Shader> InstAlphaTestWind;
		std::shared_ptr<Shader> InstWind;

		std::shared_ptr<Shader> Pick(bool skinned, bool alphaTest, bool wind) const;

		// Null when no instanced variant exists -- the caller skips the
		// batch then (a non-instanced shader would draw all instances at
		// one transform).
		std::shared_ptr<Shader> PickInstanced(bool alphaTest, bool wind) const;
	};

	// Which materials an instanced draw loop draws. The pre-z pre-phase
	// uses Flagged (with the depth variant set), the main gbuffer phase
	// uses Unflagged, then an EQUAL-reuse pass uses Flagged again with
	// the normal gbuffer shaders.
	enum class PreZFilter
	{
		None,
		Flagged,
		Unflagged
	};

	// Render-thread draw-command cache entry (UE mesh-draw-command lite):
	// everything DrawUnit/DrawInstancedUnits resolves per draw, cached per
	// (unit, pass, LOD). Values that change per frame (world matrix, wind
	// time, LOD debug tint) are patched through cached uniform locations.
	struct DrawCommand
	{
		std::shared_ptr<Shader> shader;

		std::shared_ptr<Material> material;

		std::shared_ptr<Mesh> mesh;

		unsigned int materialVersion = 0;

		int subMesh = -1;

		bool cullOff = false;

		bool wind = false;

		float alphaCutoff = -1.0f;

		int passBoundCount = 0;   // valid pass-texture binds (unit cursor base)

		struct TexBind { int location; std::shared_ptr<Texture> texture; };

		std::vector<TexBind> textureBinds;      // material textures, bind order

		struct UniBind { int location; std::shared_ptr<UniformBase> uniform; };

		std::vector<UniBind> uniformBinds;      // material uniforms

		std::vector<TexBind> passTextureBinds;  // pass input textures

		int worldMatrixLoc = -1;

		int alphaCutoffLoc = -1;

		int timeLoc = -1;

		int windParamsLoc = -1;

		int lodDebugLoc = -1;

		int lightTypeLoc = -1;
	};

	class FURY_API PrelightPipeline : public Pipeline
	{
	public:

		typedef std::shared_ptr<PrelightPipeline> Ptr;

		static Ptr Create(const std::string &name);

		PrelightPipeline(const std::string &name);

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		// Game-thread entry (called from Lua on_update): gathers the frame
		// packet and stages it with the RenderThread. Execution happens at
		// the loop tail -- on the render thread, or inline on the main
		// thread when threading is off (identical code path).
		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) override;

		// Game thread: snapshot everything the passes read into the packet.
		void GatherFrame(const std::shared_ptr<SceneManager> &sceneManager, FramePacket &packet);

		// GL thread: execute a gathered packet (all passes + postfx +
		// debug). Reads no scene state.
		virtual void ExecutePacket(FramePacket &packet) override;

	protected:

		// lightIndex is only used by TRANSPARENT passes: -1 draws the
		// ambient/emissive base, otherwise that packet light's additive
		// contribution. preZShaders switches DrawUnit into the depth-only
		// pre-phase (flagged materials, depth shader variants).
		void DrawUnit(const std::shared_ptr<Pass> &pass, const PacketUnit &unit,
			FramePacket &packet, int lightIndex = -1,
			const PreZDepthShaders *preZShaders = nullptr);

		void DrawPointLight(const std::shared_ptr<Pass> &pass, FramePacket &packet, int lightIndex);

		void DrawDirLight(const std::shared_ptr<Pass> &pass, FramePacket &packet, int lightIndex);

		void DrawSpotLight(const std::shared_ptr<Pass> &pass, FramePacket &packet, int lightIndex);

		void DrawQuad(const std::shared_ptr<Pass> &pass, const FramePacket &packet);

		// SKY draw mode: fills far-depth pixels from the scene's enabled
		// SkyAtmosphere (LUTs bound here); no-op when no sky is active.
		void DrawSky(const std::shared_ptr<Pass> &pass, FramePacket &packet);

		// pass_ocean: displaced wave grids, alpha-blended into hdr_composite,
		// writing depth + gbuffer_normal so SSR sees the water surface.
		void DrawOcean(const std::shared_ptr<Pass> &pass, FramePacket &packet);

		// Instanced (ISM/HISM) draw for the OPAQUE pass: one
		// glDrawElementsInstanced per (component, LOD tier, submesh).
		// Batches are built from packet data at the top of ExecutePacket.
		// filter splits the loop for the veg pre-z phases; preZShaders
		// (Flagged only) draws them with the depth variants.
		void DrawInstancedUnits(const std::shared_ptr<Pass> &pass, FramePacket &packet,
			PreZFilter filter = PreZFilter::None,
			const PreZDepthShaders *preZShaders = nullptr);

		// Run the active postprocess chain after the pass loop; the
		// final effect writes to the default FB / editor RenderTarget
		// with sRGB encode.
		void RunPostProcessChain(FramePacket &packet);

		// Buffer debug views (SSAO_VIEW / SSR_VIEW switches): runs
		// the named effect's DEBUG_VIEW shader variant over the
		// current gbuffer + composite and stores the result in the
		// "debug_view" texture (presented by the editor viewport /
		// Profiler instead of the scene). The effect does NOT need to
		// be enabled in the chain -- the view recomputes it standalone.
		void DrawEffectDebugView(const std::string &effectName, FramePacket &packet);

		// Chain/debug-view shared input: the lighting output texture
		// (hdr_composite / ldr_composite with legacy fallbacks).
		std::shared_ptr<Texture> GetLightingOutputTexture(bool hdrMode) const;

		// Gather helpers (game thread): resolve one octree unit into a
		// PacketUnit (LOD + billboard + skin palette), and one light node's
		// shadow plan (caster queries + crop matrices).
		PacketUnit ResolveUnit(const RenderUnit &unit);
		void GatherLightShadowPlan(const std::shared_ptr<SceneManager> &sceneManager,
			const std::shared_ptr<SceneNode> &lightNode, PacketLight &out, FramePacket &packet);

		// Draw-command cache (render thread): opaque + transparent-base
		// static draws. Skinned units and per-light additive draws bypass.
		// The pre-z pre-phase caches in its own map (same key shape) so
		// depth-shader entries never collide with the gbuffer entries.
		void DrawUnitCached(const std::shared_ptr<Pass> &pass, const PacketUnit &unit,
			FramePacket &packet, const PreZDepthShaders *preZShaders = nullptr);

		// Depth-only pre-phase draw for skinned PreZ units (the cache
		// path covers static meshes only).
		void DrawUnitPreZ(const std::shared_ptr<Pass> &pass, const PacketUnit &unit,
			FramePacket &packet, const PreZDepthShaders &shaders);

		void ReplayDrawCommand(DrawCommand &cmd, const std::shared_ptr<Pass> &pass,
			const Matrix4 *worldMatrix, FramePacket &packet);

		std::unordered_map<std::uint64_t, DrawCommand> m_DrawCommandCache;

		std::unordered_map<std::uint64_t, DrawCommand> m_PreZDrawCommandCache;

		unsigned int m_CacheHits = 0;

		unsigned int m_CacheRebuilds = 0;
	};
}

#endif // _FURY_PRELIGHT_PIPELINE_H_