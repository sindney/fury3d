#ifndef _FURY_PRELIGHT_PIPELINE_H_
#define _FURY_PRELIGHT_PIPELINE_H_

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <initializer_list>

#include "Pipeline.h"
#include "Matrix4.h"

namespace fury
{
	class Texture;

	class Shader;

	class Pass;

	class RenderQuery;

	struct RenderUnit;

	class FURY_API PrelightPipeline : public Pipeline
	{
	public:

		typedef std::shared_ptr<PrelightPipeline> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		std::shared_ptr<SceneNode> m_CurrentCamera;

		std::shared_ptr<Shader> m_CurrentShader;

		std::shared_ptr<Pass> m_SharedPass;
		
		Matrix4 m_OffsetMatrix;

	public:

		PrelightPipeline(const std::string &name);

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) override;

	protected:

		void DrawUnit(const std::shared_ptr<Pass> &pass, const RenderUnit &unit);

		void DrawLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawQuad(const std::shared_ptr<Pass> &pass);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawDirLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawPointLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawSpotLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawDebug(std::unordered_map<std::string, std::shared_ptr<RenderQuery>> &queries);
	};
}

#endif // _FURY_PRELIGHT_PIPELINE_H_