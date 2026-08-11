#ifndef _FURY_PRELIGHT_PIPELINE_H_
#define _FURY_PRELIGHT_PIPELINE_H_

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

	class FURY_API PrelightPipeline : public Pipeline
	{
	public:

		typedef std::shared_ptr<PrelightPipeline> Ptr;

		static Ptr Create(const std::string &name);

		PrelightPipeline(const std::string &name);

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) override;

	protected:

		// lightNode is only used by TRANSPARENT passes: nullptr draws
		// the ambient/emissive base, a light node draws that light's
		// additive contribution.
		void DrawUnit(const std::shared_ptr<Pass> &pass, const RenderUnit &unit,
			const std::shared_ptr<SceneNode> &lightNode = nullptr);

		void DrawPointLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawDirLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawSpotLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawQuad(const std::shared_ptr<Pass> &pass);

		// SKY draw mode: fills far-depth pixels from the scene's enabled
		// SkyAtmosphere (LUTs bound here); no-op when no sky is active.
		void DrawSky(const std::shared_ptr<Pass> &pass);

		// Run the active postprocess chain after the pass loop; the
		// final effect writes to the default FB / editor RenderTarget
		// with sRGB encode.
		void RunPostProcessChain();

		// Buffer debug views (SSAO_VIEW / SSR_VIEW switches): runs
		// the named effect's DEBUG_VIEW shader variant over the
		// current gbuffer + composite and stores the result in the
		// "debug_view" texture (presented by the editor viewport /
		// Profiler instead of the scene). The effect does NOT need to
		// be enabled in the chain -- the view recomputes it standalone.
		void DrawEffectDebugView(const std::string &effectName);

		// Chain/debug-view shared input: the lighting output texture
		// (hdr_composite / ldr_composite with legacy fallbacks).
		std::shared_ptr<Texture> GetLightingOutputTexture() const;
	};
}

#endif // _FURY_PRELIGHT_PIPELINE_H_