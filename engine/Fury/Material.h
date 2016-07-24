#ifndef _FURY_MATERIALS_H_
#define _FURY_MATERIALS_H_

#include <unordered_map>

#include "Fury/Entity.h"
#include "Fury/EnumUtil.h"
#include "Fury/Buffer.h"

namespace fury
{
	class UniformBase;

	class Texture;

	class Shader;

	class FURY_API Material : public Entity, public Buffer
	{
	public:

		friend class Shader;

		typedef std::shared_ptr<Material> Ptr;

		typedef std::unordered_map<std::string, std::shared_ptr<Texture>> TextureMap;

		typedef std::unordered_map<std::string, std::shared_ptr<UniformBase>> UniformMap;

		static const std::string DIFFUSE_TEXTURE;

		static const std::string SPECULAR_TEXTURE;

		static const std::string NORMAL_TEXTURE;

		static const std::string SHININESS;

		static const std::string TRANSPARENCY;

		static const std::string AMBIENT_FACTOR;

		static const std::string DIFFUSE_FACTOR;

		static const std::string SPECULAR_FACTOR;

		static const std::string EMISSIVE_FACTOR;

		static const std::string AMBIENT_COLOR;

		static const std::string DIFFUSE_COLOR;

		static const std::string SPECULAR_COLOR;

		static const std::string EMISSIVE_COLOR;

		static const std::string MATERIAL_ID;

		static Ptr Create(const std::string &name);

	private:

		static unsigned int m_GlobalID;

	protected:

		unsigned int GetMaterialID();

		TextureMap m_Textures;

		UniformMap m_Uniforms;

		std::vector<std::shared_ptr<Shader>> m_Shaders;

		unsigned int m_TextureFlags;

		bool m_Opaque;

		unsigned int m_ID;

	public:

		Material(const std::string &name);

		virtual ~Material();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		virtual void DeleteBuffer() override;

		unsigned int GetTextureFlags() const;

		std::shared_ptr<Texture> GetTexture(const std::string &name) const;

		void SetTexture(const std::string &name, const std::shared_ptr<Texture> &ptr);

		unsigned int GetTextureCount() const;

		void SetUniform(const std::string &name, const std::shared_ptr<UniformBase> &ptr);

		std::shared_ptr<UniformBase> GetUniform(const std::string &name);

		unsigned int GetUniformCount() const;

		void SetShaderForPass(unsigned int index, const std::shared_ptr<Shader> &shader);

		std::shared_ptr<Shader> GetShaderForPass(unsigned int index);

		bool GetOpaque() const;

		void SetOpaque(bool value);

		// get this material's unique identifier for rendering.
		unsigned int GetID() const;
	};
}

#endif // _FURY_MATERIALS_H_