#ifndef _FURY_SCENE_H_
#define _FURY_SCENE_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/Entity.h"
#include "Fury/Vector4.h"

namespace fury
{
	class EntityManager;

	class SceneNode;

	class SceneManager;

	class Material;

	class Mesh;

	class RenderSettings;

	class FURY_API Scene : public Entity
	{
	public:

		typedef std::shared_ptr<Scene> Ptr;

		static Ptr Active;

		// Scene file format version; absent in files = 1.
		static constexpr int kFormatVersion = 3;

		static std::string Path(const std::string &path);

		static std::shared_ptr<EntityManager> Manager();

		static Ptr Create(const std::string &name, const std::string &workingDir, const std::shared_ptr<SceneManager> &sceneManager = nullptr);

	protected:

		std::shared_ptr<SceneNode> m_RootNode;

		std::shared_ptr<SceneManager> m_SceneManager;

		std::shared_ptr<EntityManager> m_EntityManager;

		std::string m_WorkingDir;

		// Per-scene render settings (pipeline path, HDR, CSM,
		// postprocess chain). Always non-null.
		std::shared_ptr<RenderSettings> m_RenderSettings;

		// Optional per-scene physics settings ("physics" block). Only
		// serialized when the block was present at load or set at runtime.
		Vector4 m_PhysicsGravity = Vector4(0.0f, -981.0f, 0.0f, 0.0f);

		bool m_HasPhysicsSettings = false;

	public:

		Scene(const std::string &name, const std::string &workingDir, const std::shared_ptr<SceneManager> &sceneManager = nullptr);

		~Scene();

		void Clear();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		std::shared_ptr<SceneNode> GetRootNode() const;

		std::shared_ptr<SceneManager> GetSceneManager() const;

		std::shared_ptr<EntityManager> GetEntityManager() const;

		// extra resources prepends this to filepath when loading.
		std::string GetWorkingDir() const;

		void SetWorkingDir(const std::string &path);

		// Per-scene render settings; the editor writes through this.
		// Mark the scene dirty after edits.
		std::shared_ptr<RenderSettings> GetRenderSettings() const;

		// Per-scene physics gravity (cm/s^2, default (0,-981,0)). Setting it
		// marks the physics block for serialization and applies it to the
		// PhysicsWorld when one exists.
		Vector4 GetPhysicsGravity() const { return m_PhysicsGravity; }
		void SetPhysicsGravity(const Vector4 &gravity);
		bool HasPhysicsSettings() const { return m_HasPhysicsSettings; }
	};
}

#endif // _FURY_SCENE_H_