#ifndef _FURY_TEXTURE_H_
#define _FURY_TEXTURE_H_

#include <unordered_map>

#include "Buffer.h"
#include "Entity.h"
#include "EnumUtil.h"
#include "Serializable.h"

namespace fury
{
	class FURY_API Texture : public Entity, public Buffer, public Serializable
	{
	public:

		typedef std::shared_ptr<Texture> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		TextureFormat m_Format = TextureFormat::UNKNOW;

		FilterMode m_FilterMode = FilterMode::LINEAR;

		WrapMode m_WrapMode = WrapMode::REPEAT;

		bool m_Mipmap = false;

		int m_Width = 0;

		int m_Height = 0;

		unsigned int m_ID = 0;

		std::string m_FilePath;

	public:

		Texture(const std::string &name);

		virtual ~Texture();

		virtual bool Load(const void* wrapper) override;

		virtual bool Save(void* wrapper) override;

		void CreateFromImage(std::string filePath, bool mipMap = false);

		void CreateEmpty(int width, int height, TextureFormat format = TextureFormat::RGBA8, bool mipMap = false);

		virtual void DeleteBuffer() override;

		TextureFormat GetFormat() const;

		FilterMode GetFilterMode() const;

		void SetFilterMode(FilterMode mode);

		WrapMode GetWrapMode() const;

		void SetWrapMode(WrapMode mode);

		bool GetMipmap() const;

		int GetWidth() const;

		int GetHeight() const;

		unsigned int GetID() const;

		std::string GetFilePath() const;
	};
}

#endif // _FURY_TEXTURE_H_