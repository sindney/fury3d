#ifndef _FURY_PIPELINE_H_
#define _FURY_PIPELINE_H_

#include <memory>
#include <unordered_map>
#include <string>

#include "Entity.h"
#include "Serializable.h"

namespace fury
{
	class BoxBounds;

	class Collidable;

	class Frustum;

	class Pass;

	class SceneNode;

	class SceneManager;

	class Texture;

	class Shader;

	class SphereBounds;

	class RenderQuery;

	union PipelineOption
	{
		int intValue;

		float floatValue;

		bool boolValue;
	};

	class FURY_API Pipeline : public Entity, public Serializable
	{
		friend class FileUtil;

	public:

		typedef std::shared_ptr<Pipeline> Ptr;

		static const std::string OPT_CASCADED_SHADOW_MAP;

		static const std::string OPT_MESH_BOUNDS;

		static const std::string OPT_LIGHT_BOUNDS;

		static const std::string OPT_CUSTOM_BOUNDS;

	protected:

		std::unordered_map<std::string, std::shared_ptr<Texture>> m_TextureMap;

		std::unordered_map<std::string, std::shared_ptr<Pass>> m_PassMap;

		std::unordered_map<std::string, std::shared_ptr<Shader>> m_ShaderMap;

		std::vector<std::string> m_SortedPasses;

		// rendering

		std::shared_ptr<SceneNode> m_CurrentCamera;

		std::shared_ptr<Shader> m_CurrentShader;

		std::shared_ptr<Pass> m_SharedPass;

		Matrix4 m_OffsetMatrix;

		// end rendering

		std::unordered_map<std::string, PipelineOption> m_Options;

		// debug

		std::vector<BoxBounds> m_DebugBoxBounds;

		std::vector<Frustum> m_DebugFrustum;

		// end debug

	public:

		Pipeline(const std::string &name);

		virtual ~Pipeline();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual bool Save(void* wrapper, bool object = true) override;

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) = 0;
		
		void SetOption(const std::string &name, bool value);

		void SetOption(const std::string &name, float value);

		void SetOption(const std::string &name, int value);

		std::pair<bool, PipelineOption> GetOption(const std::string &name);

		void ClearDebugCollidables();

		void AddDebugCollidable(const BoxBounds &bounds);

		void AddDebugCollidable(const Frustum &bounds);

		std::shared_ptr<Pass> GetPassByName(const std::string &name);

		std::shared_ptr<Texture> GetTextureByName(const std::string &name);

		std::shared_ptr<Shader> GetShaderByName(const std::string &name);

		// begin shaodw mapping

		void FilterNodes(const Collidable &collider, std::vector<std::shared_ptr<SceneNode>> &possibles, std::vector<std::shared_ptr<SceneNode>> &collisions);

		Matrix4 GetCropMatrix(Matrix4 lightMatrix, Frustum frustum, std::vector<std::shared_ptr<SceneNode>> &casters);

		std::pair<std::shared_ptr<Texture>, std::vector<Matrix4>> DrawCascadedShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawDirLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawPointLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawSpotLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		// en shaodw mapping

		void DrawDebug(std::unordered_map<std::string, std::shared_ptr<RenderQuery>> &queries);

	protected: 

		void SortPassByIndex();
	};
}

#endif // _FURY_PIPELINE_H_