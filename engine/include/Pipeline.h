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

		unsigned int m_DrawCall = 0;

	public:

		Pipeline(const std::string &name) : Entity(name) 
		{
			m_TypeIndex = typeid(Pipeline);
		}

		virtual bool Load(const void* wrapper);

		virtual bool Save(void* wrapper);

		virtual void Execute(const std::shared_ptr<SceneManager> &sceneManager) = 0;
		
		unsigned int GetDrawCall() const
		{
			return m_DrawCall;
		}

	protected: 

		void SortPassByIndex();
	};
}

#endif // _FURY_PIPELINE_H_