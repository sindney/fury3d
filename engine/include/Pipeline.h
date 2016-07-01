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

	// assume int takes 16bits
	enum PipelineDebugFlags : unsigned int
	{
		MESH_BOUNDS = 0x0001,
		LIGHT_BOUNDS = 0x0002,
		CUSTOM_BOUNDS = 0x0004
	};

	class FURY_API Pipeline : public Entity, public Serializable
	{
		friend class FileUtil;

	public:

		typedef std::shared_ptr<Pipeline> Ptr;

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

		// debug

		std::vector<BoxBounds> m_DebugBoxBounds;

		std::vector<Frustum> m_DebugFrustum;

		unsigned int m_DebugFlags = 0;

		// end debug

	public:

		Pipeline(const std::string &name);

		virtual ~Pipeline();

		virtual bool Load(const void* wrapper);

		virtual bool Save(void* wrapper);

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) = 0;
		
		void SetDebugFlags(unsigned int flags);

		void ClearDebugCollidables();

		void AddDebugCollidable(const BoxBounds &bounds);

		void AddDebugCollidable(const Frustum &bounds);

		std::shared_ptr<Pass> GetPassByName(const std::string &name);

		std::shared_ptr<Texture> GetTextureByName(const std::string &name);

		std::shared_ptr<Shader> GetShaderByName(const std::string &name);

		// begin shaodw mapping

		std::pair<std::shared_ptr<Texture>, std::vector<Matrix4>> DrawCascadedShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawDirLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawPointLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		std::pair<std::shared_ptr<Texture>, Matrix4> DrawSpotLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node);

		void DrawDebug(std::unordered_map<std::string, std::shared_ptr<RenderQuery>> &queries);

		// en shaodw mapping

	protected: 

		void SortPassByIndex();
	};
}

#endif // _FURY_PIPELINE_H_