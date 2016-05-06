#ifndef _FURY_PIPELINE_H_
#define _FURY_PIPELINE_H_

#include <memory>
#include <unordered_map>
#include <string>

#include "Entity.h"
#include "Serializable.h"

namespace fury
{
	class Pass;

	class SceneNode;

	class SceneManager;

	class Texture;

	class Shader;

	enum class ShadowType : int
	{
		DISABLE = 0,
		NORMAL_SHADOW_MAP = 1,
		VARIANCE_SHADOW_MAP = 2
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

		bool m_DrawOpaqueBounds = true;

		bool m_DrawLightBounds = true;

		ShadowType m_ShadowType = ShadowType::VARIANCE_SHADOW_MAP;

	public:

		Pipeline(const std::string &name) : Entity(name) 
		{
			m_TypeIndex = typeid(Pipeline);
		}

		virtual ~Pipeline();

		virtual bool Load(const void* wrapper);

		virtual bool Save(void* wrapper);

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) = 0;
		
		void SetShadowType(ShadowType type);

		ShadowType GetShadowType();

		void SetDebugParams(bool drawOpaqueBounds, bool drawLightBounds);

		std::shared_ptr<Pass> GetPassByName(const std::string &name);

		std::shared_ptr<Texture> GetTextureByName(const std::string &name);

		std::shared_ptr<Shader> GetShaderByName(const std::string &name);

	protected: 

		void SortPassByIndex();
	};
}

#endif // _FURY_PIPELINE_H_