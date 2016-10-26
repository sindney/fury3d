#ifndef _FURY_PIPELINE_H_
#define _FURY_PIPELINE_H_

#include <memory>
#include <unordered_map>
#include <string>
#include <bitset>

#include "Fury/Entity.h"

namespace fury
{
	class BoxBounds;

	class Collidable;

	class Frustum;

	class Material;

	class Pass;

	class SceneNode;

	class SceneManager;

	class EntityManager;

	class Texture;

	class Shader;

	class SphereBounds;

	class RenderQuery;

	enum class PipelineSwitch : unsigned int
	{
		CASCADED_SHADOW_MAP = 0, 
		MESH_BOUNDS, 
		LIGHT_BOUNDS, 
		CUSTOM_BOUNDS, 
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

		// rendering

		std::shared_ptr<SceneNode> m_CurrentCamera;

		std::shared_ptr<Shader> m_CurrentShader;

		std::shared_ptr<Material> m_CurrentMateral;

		std::shared_ptr<Pass> m_SharedPass;

		Matrix4 m_OffsetMatrix;

		// end rendering

		// debug

		std::vector<BoxBounds> m_DebugBoxBounds;

		std::vector<Frustum> m_DebugFrustum;

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

		void ClearDebugCollidables();

		void AddDebugCollidable(const BoxBounds &bounds);

		void AddDebugCollidable(const Frustum &bounds);

		std::shared_ptr<Pass> GetPassByName(const std::string &name);

		std::shared_ptr<Texture> GetTextureByName(const std::string &name);

		std::shared_ptr<Shader> GetShaderByName(const std::string &name);

		std::shared_ptr<SceneNode> GetCurrentCamera() const;

		void SetCurrentCamera(const std::shared_ptr<SceneNode> &ptr);

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

		void SortPassByIndex();
	};
}

#endif // _FURY_PIPELINE_H_