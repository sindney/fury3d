#ifndef _FURY_PRELIGHT_PIPELINE_H_
#define _FURY_PRELIGHT_PIPELINE_H_

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <tuple>

#include "Pipeline.h"
#include "Matrix4.h"

namespace fury
{
	class Texture;

	class Shader;

	class Pass;

	class RenderQuery;

	class FURY_API PrelightPipeline : public Pipeline
	{
	public:

		typedef std::shared_ptr<PrelightPipeline> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		std::shared_ptr<SceneNode> m_CurrentCamera;

		std::shared_ptr<Shader> m_CurrentShader;

		std::shared_ptr<Pass> m_DrawDepthPass;

		std::shared_ptr<Pass> m_DrawShadowPass;

	public:

		PrelightPipeline(const std::string &name);

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) override;

	protected:

		bool PointInCone(Vector4 coneCenter, Vector4 coneDir, float height, float theta, Vector4 point);

		void DrawRenderable(const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawLight(const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawQuad(const std::shared_ptr<Pass> &pass);

		void DrawShadow(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawDebug(std::unordered_map<std::string, std::shared_ptr<RenderQuery>> &queries);
	};
}

#endif // _FURY_PRELIGHT_PIPELINE_H_