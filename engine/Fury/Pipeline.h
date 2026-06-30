#ifndef _FURY_PIPELINE_H_
#define _FURY_PIPELINE_H_

#include <memory>
#include <unordered_map>
#include <string>
#include <bitset>

#include "Fury/Entity.h"
#include "Fury/Matrix4.h"

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

		std::shared_ptr<Mesh> m_CurrentMesh;

		std::shared_ptr<Pass> m_SharedPass;

		Matrix4 m_OffsetMatrix;

		// When non-null, the final (no-output) composite pass renders into
		// this offscreen target instead of the default framebuffer. Used by
		// the editor's dockable Viewport window. Null = render to the
		// default framebuffer (the historical behavior, used by non-editor
		// builds and any sample that calls Execute without an RT).
		RenderTarget* m_RenderTarget = nullptr;

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

		// Offscreen render-target override. When set, the final composite
		// pass (and Pipeline::DrawDebug) render into this target instead of
		// the default framebuffer. Non-owning; the caller (the editor) owns
		// the RenderTarget and must keep it alive while set here. Pass
		// nullptr to revert to default-framebuffer rendering.
		void SetRenderTarget(RenderTarget* target);

		RenderTarget* GetRenderTarget() const;

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