#ifndef _FURY_PIPELINE_H_
#define _FURY_PIPELINE_H_

#include <memory>
#include <unordered_map>
#include <string>
#include <bitset>

#include "Fury/Entity.h"
#include "Fury/Matrix4.h"
#include "Fury/Vector4.h"

namespace fury
{
	class BoxBounds;

	class Collidable;

	class Frustum;

	class Material;

	class Mesh;

	class Pass;

	class SceneNode;

	class SceneManager;

	class EntityManager;

	class Texture;

	class Shader;

	class SphereBounds;

	class RenderQuery;

	class RenderTarget;

	enum class PipelineSwitch : unsigned int
	{
		CASCADED_SHADOW_MAP = 0,
		MESH_BOUNDS,
		LIGHT_BOUNDS,
		CUSTOM_BOUNDS,
		OCTREE_BOUNDS,
		LOD_DEBUG_COLORS,
		EDITOR_GRID,
		// Fullscreen buffer debug views (viewport toolbar "View SSAO"
		// / "View SSR"). When on, Execute renders the effect's
		// DEBUG_VIEW shader variant into the debug-view texture; the
		// viewport presents that instead of the scene RT and the
		// Profiler's GBuffer tab lists it. Mutually exclusive in the
		// UI; SSAO wins if both are set.
		SSAO_VIEW,
		SSR_VIEW,
		LENGTH
	};

	class FURY_API Pipeline : public Entity
	{
		friend class FileUtil;

	public:

		typedef std::shared_ptr<Pipeline> Ptr;

		static Ptr Active;

	protected:

		std::shared_ptr<EntityManager> m_EntityManager;

		std::vector<std::string> m_SortedPasses;

		std::bitset<(size_t)PipelineSwitch::LENGTH> m_Switches;

		// HDR mode: float lighting targets; the chain is forced to
		// include a tonemap. Defaults to false.
		bool m_HDRMode = false;

		// Resolved postprocess chain, rebuilt per-frame from the
		// scene's renderSettings. Empty = legacy final-pass path.
		std::vector<std::shared_ptr<class PostProcessEffect>> m_ActiveChain;

		// Per-entry uniform overrides, index-aligned with m_ActiveChain
		// (empty map = descriptor defaults). Populated from
		// RenderChainEntry::uniformOverrides by ApplyRenderSettings.
		std::vector<std::unordered_map<std::string, std::shared_ptr<class UniformBase>>> m_ActiveChainOverrides;

		// rendering

		std::shared_ptr<SceneNode> m_CurrentCamera;

		std::shared_ptr<Shader> m_CurrentShader;

		std::shared_ptr<Material> m_CurrentMateral;

		std::shared_ptr<Mesh> m_CurrentMesh;

		std::shared_ptr<Pass> m_SharedPass;

		Matrix4 m_OffsetMatrix;

		// When non-null, the final (no-output) composite pass renders into
		// this offscreen target instead of the default framebuffer. Used by
		// the editor's dockable Viewport window. Null = render to the
		// default framebuffer (the historical behavior, used by non-editor
		// builds and any sample that calls Execute without an RT).
		RenderTarget* m_RenderTarget = nullptr;

		// Buffer debug-view output (SSAO_VIEW / SSR_VIEW switches).
		// Allocated on demand by PrelightPipeline::DrawEffectDebugView,
		// registered as the "debug_view" texture; the editor presents it
		// in place of the scene RT while a debug view is active.
		std::shared_ptr<Texture> m_DebugViewTexture;

		// end rendering

		// debug

		std::vector<BoxBounds> m_DebugBoxBounds;

		std::vector<Frustum> m_DebugFrustum;

		// Per-frame, per-light shadow texture cache for editor debug views.
		// Keyed by the light's SceneNode* raw pointer (lifetime = scene
		// lifetime; cleared at the top of each Execute). Value is the
		// shadow texture returned by Draw{Dir,Point,Spot,Cascaded}LightShadowMap.
		// Only populated when a shadow map was actually drawn this frame;
		// the editor's Profiler -> Shadows tab reads this to render one
		// section per shadow-casting light.
		std::unordered_map<SceneNode*, std::shared_ptr<Texture>> m_LastShadowTextures;

		// Per-light cached shadow data, same lifetime as
		// m_LastShadowTextures (cleared at the top of each Execute).
		// The transparent pass's shadow-receive path needs the
		// matrices the deferred light draws already composed, in the
		// conventions their shaders expect:
		//   - Directional single-map / spot: maps view -> shadow UV
		//     (one Matrix4 in `single`).
		//   - CSM: per-cascade view -> shadow UV matrices + the
		//     split (shadow_far) used by the CSM shader to pick a
		//     cascade on linear view depth.
		// Stored for the dir/spot 2D paths (CSM/spot, and dir when
		// CSM is off) so the transparent pass and the per-emitter
		// particle block can find the bind inputs.
		struct ShadowData
		{
			Matrix4 single;
			std::vector<Matrix4> csm;       // size 4 when populated
			Vector4 shadowFar = Vector4(0,0,0,0);
		};
		std::unordered_map<SceneNode*, ShadowData> m_LastShadowMatrices;

		// Shadow-map temporaries held until the END of Execute.
		// ReleaseTemporary returns the map to a spec-keyed pool for
		// immediate reuse, so releasing at light-draw time let a second
		// same-spec caster's shadow draw pop and overwrite the first
		// light's map before pass_transparent sampled it.
		std::vector<std::shared_ptr<Texture>> m_FrameShadowTemps;

		// end debug

	public:

		Pipeline(const std::string &name);

		virtual ~Pipeline();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) = 0;
		
		// basiclly saves all pipeline && pass's textures, shaders
		std::shared_ptr<EntityManager> GetEntityManager() const;

		void SetSwitch(PipelineSwitch key, bool value);

		bool IsSwitchOn(PipelineSwitch key);

		bool IsSwitchOn(std::initializer_list<PipelineSwitch> list, bool any = true);

		// HDR mode accessors (runtime side; persisted via Scene's renderSettings).
		void SetHDRMode(bool value);

		bool IsHDRMode() const;

		// Set the active postprocess chain (editor edits, or the
		// per-frame rebuild from renderSettings).
		void SetActiveChain(const std::vector<std::shared_ptr<class PostProcessEffect>> &chain);

		const std::vector<std::shared_ptr<class PostProcessEffect>> &GetActiveChain() const;

		// Seed HDR mode, CSM switch, and the resolved chain from
		// RenderSettings. The chain runs in the engine-owned canonical
		// order (stage, order, name — saved entry order is ignored),
		// and tonemapping follows the HDR switch: exactly one
		// tonemap-stage effect (ACES) is auto-injected in HDR and all
		// tonemap entries are stripped in LDR. Unresolved names are
		// skipped with a warning.
		void ApplyRenderSettings(const class RenderSettings &settings);

		// True iff this pipeline declares the hdr_composite target
		// the chain reads in HDR mode.
		bool HasHDRComposite() const;

		void ClearDebugCollidables();

		void AddDebugCollidable(const BoxBounds &bounds);

		void AddDebugCollidable(const Frustum &bounds);

		std::shared_ptr<Pass> GetPassByName(const std::string &name);

		std::shared_ptr<Texture> GetTextureByName(const std::string &name) const;

		std::shared_ptr<Shader> GetShaderByName(const std::string &name);

		// Returns the shadow texture most recently drawn for the given light
		// during this frame, or nullptr if no shadow map was rendered for it
		// (e.g. CastShadows is false, or the light was culled). Cleared at the
		// top of each Execute(); consumers in the editor must read this
		// AFTER Pipeline::Execute and before the next Execute.
		std::shared_ptr<Texture> GetLastShadowTexture(const SceneNode &lightNode) const;

		std::shared_ptr<SceneNode> GetCurrentCamera() const;

		void SetCurrentCamera(const std::shared_ptr<SceneNode> &ptr);

		// Offscreen render-target override. When set, the final composite
		// pass (and Pipeline::DrawDebug) render into this target instead of
		// the default framebuffer. Non-owning; the caller (the editor) owns
		// the RenderTarget and must keep it alive while set here. Pass
		// nullptr to revert to default-framebuffer rendering.
		void SetRenderTarget(RenderTarget* target);

		RenderTarget* GetRenderTarget() const;

		// Buffer debug-view texture (see m_DebugViewTexture). Null when
		// no debug view is active — the editor presents the scene RT.
		void SetDebugViewTexture(const std::shared_ptr<Texture> &ptr);

		std::shared_ptr<Texture> GetDebugViewTexture() const;

		// begin shaodw mapping

		void FilterNodes(const Collidable &collider, std::vector<std::shared_ptr<SceneNode>> &possibles, std::vector<std::shared_ptr<SceneNode>> &collisions);

		Matrix4 GetCropMatrix(Matrix4 lightMatrix, Frustum frustum, std::vector<std::shared_ptr<SceneNode>> &casters);

		std::pair<std::shared_ptr<Texture>, std::vector<Matrix4>> DrawCascadedShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawDirLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawPointLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawSpotLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		// end shaodw mapping

	protected:

		void DrawDebug(const std::shared_ptr<RenderQuery> &query);

		// Screen-space reference grid (EDITOR_GRID switch).
		void DrawEditorGrid();

		void SortPassByIndex();
	};
}

#endif // _FURY_PIPELINE_H_