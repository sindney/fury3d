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

	struct RenderUnit;

	class FURY_API PrelightPipeline : public Pipeline
	{
	public:

		typedef std::shared_ptr<PrelightPipeline> Ptr;

		static Ptr Create(const std::string &name);

		PrelightPipeline(const std::string &name);

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) override;

	protected:

		void DrawUnit(const std::shared_ptr<Pass> &pass, const RenderUnit &unit);

		void DrawPointLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawDirLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawSpotLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawQuad(const std::shared_ptr<Pass> &pass);
	};
}

#endif // _FURY_PRELIGHT_PIPELINE_H_