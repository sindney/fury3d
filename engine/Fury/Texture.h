#ifndef _FURY_TEXTURE_H_
#define _FURY_TEXTURE_H_

#include <stack>
#include <unordered_map>

#include "Fury/Buffer.h"
#include "Fury/Color.h"
#include "Fury/Entity.h"
#include "Fury/EnumUtil.h"
#include "Fury/Serializable.h"

namespace fury
{
	// note that if you don't create texture from Texture's static creators.
	// then the new texture is not added to BufferManager, add that texture if you need.
	class FURY_API Texture : public Entity, public Buffer
	{
	protected:

		static std::unordered_map<std::string, std::stack<std::shared_ptr<Texture>>> m_TexturePool;

		static std::string GetKeyFromParams(int width, int height, int depth, TextureFormat format, TextureType type);

		static std::string GetKeyFromPtr(const std::shared_ptr<Texture> &ptr);

	public:

		typedef std::shared_ptr<Texture> Ptr;

		static Ptr Create(const std::string &name);

		// create new or reuse texture from pool. textures are named like 512x512xrgba8x2d.
		static Ptr GetTemporary(int width, int height, int depth, TextureFormat format, TextureType type = TextureType::TEXTURE_2D);

		// collect texture to pool for reuse.
		static void ReleaseTemporary(const std::shared_ptr<Texture> &ptr);

		// delete and release all textures from pool.
		static void ReleaseTempories();

	protected:

		TextureFormat m_Format = TextureFormat::UNKNOW;

		TextureType m_Type = TextureType::TEXTURE_2D;

		unsigned int m_TypeUint = 0;

		FilterMode m_FilterMode = FilterMode::LINEAR;

		WrapMode m_WrapMode = WrapMode::REPEAT;

		Color m_BorderColor;

		bool m_Mipmap = false;

		int m_Width = 0;

		int m_Height = 0;

		int m_Depth = 0;

		unsigned int m_ID = 0;

		std::string m_FilePath;

	public:

		Texture(const std::string &name);

		virtual ~Texture();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		void CreateFromImage(const std::string &filePath, bool srgb, bool mipMap);

		void CreateEmpty(int width, int height, int depth, TextureFormat format = TextureFormat::RGBA8, TextureType type = TextureType::TEXTURE_2D, bool mipMap = false);

		void SetPixels(const void* pixels);

		virtual void UpdateBuffer() override;

		virtual void DeleteBuffer() override;

		bool IsSRGB() const;

		TextureFormat GetFormat() const;

		TextureType GetType() const;

		unsigned int GetTypeUint() const;

		FilterMode GetFilterMode() const;

		void SetFilterMode(FilterMode mode);

		WrapMode GetWrapMode() const;

		void SetWrapMode(WrapMode mode);

		Color GetBorderColor() const;

		void SetBorderColor(Color color);

		void GenerateMipMap();

		bool GetMipmap() const;

		int GetWidth() const;

		int GetHeight() const;

		int GetDepth() const;

		unsigned int GetID() const;

		std::string GetFilePath() const;

	protected:

		void IncreaseMemory();

		void DecreaseMemory();
	};
}

#endif // _FURY_TEXTURE_H_