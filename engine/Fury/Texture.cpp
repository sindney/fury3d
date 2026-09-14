#include <array>
#include <cassert>
#include <sstream>

#include "Fury/BufferManager.h"
#include "Fury/Log.h"
#include "Fury/GLLoader.h"
#include "Fury/EntityUtil.h"
#include "Fury/FileUtil.h"
#include "Fury/Scene.h"
#include "Fury/Texture.h"
#include "Fury/RenderThread.h"
#include "Fury/EnumUtil.h"

// stb_image.h is a single-header library: every inline definition is parsed in
// the surrounding translation unit, so /external:W0 + /external:I don't reach
// it (template-instantiation context, not header parse). Silence the specific
// level-4 warnings it emits locally; the rest of the engine stays at /W4.
#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4100)  // unreferenced formal parameter
#pragma warning(disable: 4312)  // conversion from int to larger pointer
#pragma warning(disable: 4456)  // declaration hides previous local
#pragma warning(disable: 4505)  // unreferenced local function
#endif
#include "stb_image.h"
#if PLATFORM_WINDOWS
#pragma warning(pop)
#endif

namespace fury
{
	std::unordered_map<std::string, std::stack<std::shared_ptr<Texture>>> Texture::m_TexturePool;

	Texture::Ptr Texture::Create(const std::string &name)
	{
		auto ptr = std::make_shared<Texture>(name);
		BufferManager::Instance()->Add(ptr);
		return ptr;
	}

	Texture::Ptr Texture::GetTemporary(int width, int height, int depth, TextureFormat format, TextureType type)
	{
		FURY_GL_THREAD_GUARD();
		auto key = GetKeyFromParams(width, height, depth, format, type);
		auto it = m_TexturePool.find(key);
		if (it != m_TexturePool.end())
		{
			if (!it->second.empty())
			{
				auto texture = it->second.top();
				it->second.pop();
				return texture;
			}
		}

		auto texture = Texture::Create(key);
		texture->CreateEmpty(width, height, depth, format, type);
		BufferManager::Instance()->Add(texture);
		return texture;
	}

	void Texture::ReleaseTemporary(const std::shared_ptr<Texture> &ptr)
	{
		FURY_GL_THREAD_GUARD();
		auto key = Texture::GetKeyFromPtr(ptr);
		auto it = m_TexturePool.find(key);
		if (it == m_TexturePool.end())
			it = m_TexturePool.emplace(key, std::stack<Texture::Ptr>()).first;
		
		it->second.push(ptr);
	}

	void Texture::ReleaseTempories()
	{
		for (auto pair : m_TexturePool)
		{
			auto &stack = pair.second;
			while (!stack.empty())
			{
				BufferManager::Instance()->Release(stack.top()->GetBufferId());
				stack.pop();
			}
		}
	}

	std::string Texture::GetKeyFromParams(int width, int height, int depth, TextureFormat format, TextureType type)
	{
		std::stringstream ss;
		ss << width << "*" << height << "*" << depth << "*" << EnumUtil::TextureFormatToString(format) <<
			"*" << EnumUtil::TextureTypeToString(type);
		return ss.str();
	}

	std::string Texture::GetKeyFromPtr(const std::shared_ptr<Texture> &ptr)
	{
		return GetKeyFromParams(ptr->GetWidth(), ptr->GetHeight(), ptr->GetDepth(), ptr->GetFormat(), ptr->GetType());
	}

	Texture::Texture(const std::string &name)
		: Entity(name), m_BorderColor(0, 0, 0, 0)
	{
		m_TypeIndex = typeid(Texture);
		m_TypeUint = EnumUtil::TextureTypeToUnit(m_Type);
	}

	Texture::~Texture()
	{
		DeleteBuffer();
	}

	bool Texture::Load(const void* wrapper, bool object)
	{
		std::string str;

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		auto filterMode = FilterMode::LINEAR;
		if (LoadMemberValue(wrapper, "filter", str))
			filterMode = EnumUtil::FilterModeFromString(str);

		auto wrapMode = WrapMode::REPEAT;
		if (LoadMemberValue(wrapper, "wrap", str))
			wrapMode = EnumUtil::WrapModeFromString(str);

		auto color = Color::Black;
		LoadMemberValue(wrapper, "borderColor", color);
		SetBorderColor(color);

		bool mipmap = false;
		LoadMemberValue(wrapper, "mipmap", mipmap);

		SetFilterMode(filterMode);
		SetWrapMode(wrapMode);

		if (LoadMemberValue(wrapper, "path", str))
		{
			bool srgb = false;
			LoadMemberValue(wrapper, "srgb", srgb);
			CreateFromImage(str, srgb, mipmap);
		}
		else
		{
			if (!LoadMemberValue(wrapper, "format", str))
			{
				FURYE << "Texture param 'format' not found!";
				return false;
			}
			auto format = EnumUtil::TextureFormatFromString(str);

			if (!LoadMemberValue(wrapper, "type", str))
				str = EnumUtil::TextureTypeToString(TextureType::TEXTURE_2D);
			auto type = EnumUtil::TextureTypeFromString(str);

			int width = 0, height = 0;
			if (!LoadMemberValue(wrapper, "width", width) || !LoadMemberValue(wrapper, "height", height))
			{
				FURYE << "Texture param 'width/height' not found!";
				return false;
			}

			int depth = 0;
			LoadMemberValue(wrapper, "depth", depth);

			CreateEmpty(width, height, depth, format, type, mipmap);
		}

		return true;
	}

	void Texture::Save(void* wrapper, bool object)
	{
		// FileUtil::SaveFile / SaveCompressedFile must extract any
		// memory-backed textures to sibling files before serialization
		// runs. If a memory-backed texture reaches Save it indicates a
		// missed extraction call site.
		assert(!IsMemoryBacked() && "Texture::Save reached with memory-backed texture; FileUtil should have extracted");

		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		if (m_FilePath.empty())
		{
			SaveKey(wrapper, "format");
			SaveValue(wrapper, EnumUtil::TextureFormatToString(m_Format));
			SaveKey(wrapper, "type");
			SaveValue(wrapper, EnumUtil::TextureTypeToString(m_Type));
			SaveKey(wrapper, "width");
			SaveValue(wrapper, m_Width);
			SaveKey(wrapper, "height");
			SaveValue(wrapper, m_Height);
			SaveKey(wrapper, "depth");
			SaveValue(wrapper, m_Depth);
		}
		else
		{
			SaveKey(wrapper, "path");
			SaveValue(wrapper, m_FilePath);
			SaveKey(wrapper, "srgb");
			SaveValue(wrapper, m_Format == TextureFormat::SRGB || m_Format == TextureFormat::SRGB8 ||
				m_Format == TextureFormat::SRGB8_ALPHA8 || m_Format == TextureFormat::SRGB_ALPHA);
		}

		SaveKey(wrapper, "borderColor");
		SaveValue(wrapper, m_BorderColor);
		SaveKey(wrapper, "mipmap");
		SaveValue(wrapper, m_Mipmap);

		SaveKey(wrapper, "filter");
		SaveValue(wrapper, EnumUtil::FilterModeToString(m_FilterMode));
		SaveKey(wrapper, "wrap");
		SaveValue(wrapper, EnumUtil::WrapModeToString(m_WrapMode));

		if (object)
			EndObject(wrapper);
	}

	void Texture::CreateFromImage(const std::string &filePath, bool srgb, bool mipMap)
	{
		// Path resolution + decode + bookkeeping run on the CALLING thread:
		// Scene::ResolveAsset depends on the active scene's working dir,
		// which only the caller's context knows (mid-run scene opens must
		// not resolve paths on the render thread). Only the GL upload is
		// dispatched to the GL thread.
		DeleteBuffer();

		int channels = 0;
		std::vector<unsigned char> pixels;

		// Resolve via Scene::ResolveAsset (Engine/ prefix routes to the
		// engine resource root, anything else joins the active scene's
		// working_dir). Absolute paths are passed through untouched so
		// glTF-importer-extracted textures (which write to
		// /tmp/.../outdoor_imageN.jpg etc.) resolve correctly without
		// being double-prefixed.
		const bool isAbsolute = !filePath.empty()
			&& (filePath.front() == '/' || filePath.front() == '\\'
				|| (filePath.size() >= 2 && filePath[1] == ':'));
		const std::string resolved = isAbsolute ? filePath : Scene::ResolveAsset(filePath);

		// Record the requested path up-front so the save-side
		// relocation can find and rewrite it even when the load
		// fails (the pre-fix behavior cleared m_FilePath on a
		// failed LoadImage, which meant Texture::Save fell through
		// to the embedded 0x0 branch and the bad multi-segment
		// path was lost). Set once at the top, regardless of
		// whether LoadImage succeeds below.
		SetPath(filePath);

		if (!FileUtil::LoadImage(resolved, pixels, m_Width, m_Height, channels))
			return;

		unsigned int internalFormat, imageFormat;
		switch (channels)
		{
		case 3:
			m_Format = srgb ? TextureFormat::SRGB8 : TextureFormat::RGB8;
			internalFormat = srgb ? GL_SRGB8 : GL_RGB8;
			imageFormat = GL_RGB;
			break;
		case 4:
			m_Format = srgb ? TextureFormat::SRGB8_ALPHA8 : TextureFormat::RGBA8;
			internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
			imageFormat = GL_RGBA;
			break;
		default:
			m_Format = TextureFormat::UNKNOW;
			FURYW << channels << " channel image not supported!";
			return;
		}

		m_Depth = 0;
		m_Mipmap = mipMap;

		// CLI / no-GL-context path: the serialization shape (path,
		// width/height, format) is set, the GPU upload is skipped.
		if (_ptrc_glGenTextures == nullptr)
		{
			m_Dirty = true;
			return;
		}

		// GL upload on the GL thread (immediate there, queued from foreign
		// threads; skipped if this texture dies first). Filter/wrap/border
		// values are captured at call time.
		const unsigned int filterMode = EnumUtil::FilterModeToUint(m_FilterMode);
		const unsigned int wrapMode = EnumUtil::WrapModeToUint(m_WrapMode);
		const Color borderColor = m_BorderColor;
		const unsigned int texTarget = m_TypeUint;
		const bool genMips = m_Mipmap;
		const int w = m_Width;
		const int h = m_Height;
		DispatchGL(this, [this, pixels = std::move(pixels), internalFormat, imageFormat,
			filterMode, wrapMode, borderColor, texTarget, genMips, w, h]() mutable
		{
			FURY_GL_THREAD_GUARD();

			glGenTextures(1, &m_ID);
			glBindTexture(texTarget, m_ID);

			glTexStorage2D(texTarget, genMips ? FURY_MIPMAP_LEVEL : 1, internalFormat, w, h);
			glTexSubImage2D(texTarget, 0, 0, 0, w, h, imageFormat, GL_UNSIGNED_BYTE, pixels.data());

			glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, filterMode);
			glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, filterMode);
			glTexParameteri(texTarget, GL_TEXTURE_WRAP_S, wrapMode);
			glTexParameteri(texTarget, GL_TEXTURE_WRAP_T, wrapMode);
			glTexParameteri(texTarget, GL_TEXTURE_WRAP_R, wrapMode);

			float color[] = { borderColor.r, borderColor.g, borderColor.b, borderColor.a };
			glTexParameterfv(texTarget, GL_TEXTURE_BORDER_COLOR, color);

			if (genMips)
				glGenerateMipmap(texTarget);

			glBindTexture(texTarget, 0);

			m_Dirty = false;
			IncreaseMemory();

			FURYD << m_Name << " [" << w << " x " << h << " x " << EnumUtil::TextureTypeToString(m_Type) << "]";
		});
	}

	void Texture::CreateFromMemory(const unsigned char *bytes, size_t len, bool srgb, bool mipMap)
	{
		if (!RenderThread::Get().MayUseGL())
		{
			std::vector<unsigned char> copy(bytes, bytes + len);
			DispatchGL(this, [this, copy = std::move(copy), srgb, mipMap]() mutable {
				CreateFromMemory(copy.data(), copy.size(), srgb, mipMap);
			});
			return;
		}
		if (bytes == nullptr || len == 0)
		{
			FURYE << "Texture::CreateFromMemory: empty input buffer";
			return;
		}

		// CLI / no-GL-context path: store bytes for later save extraction
		// and set the serialization shape, but skip the GPU upload. Detect
		// via the engine's GL function-pointer loader -- when LoadGLFunctions
		// hasn't run, the function pointer is null.
		if (_ptrc_glGenTextures == nullptr)
		{
			DeleteBuffer();
			// Quick image header probe to set width/height. stbi can read
			// these without decoding the full image -- but its public API
			// doesn't expose that; we accept a small wasted decode here so
			// the serialized texture record's width/height are non-zero.
			int width = 0, height = 0, channels = 0;
			if (unsigned char *probe = stbi_load_from_memory(bytes,
				static_cast<int>(len), &width, &height, &channels, 0))
			{
				stbi_image_free(probe);
			}
			m_Width = width;
			m_Height = height;
			m_Depth = 0;
			m_Mipmap = mipMap;
			m_FilePath = "";
			m_Format = srgb ? TextureFormat::SRGB8_ALPHA8 : TextureFormat::RGBA8;
			m_EncodedBytes.assign(bytes, bytes + len);
			m_Dirty = true;
			FURYD << m_Name << " [memory-backed CPU-only, " << len << " bytes]";
			return;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		unsigned char *pixels = stbi_load_from_memory(
			bytes, static_cast<int>(len), &width, &height, &channels, 0);
		if (!pixels || width == 0 || height == 0)
		{
			FURYE << "Texture::CreateFromMemory: stbi_load_from_memory failed: "
				<< (stbi_failure_reason() ? stbi_failure_reason() : "(unknown)");
			if (pixels) stbi_image_free(pixels);
			return;
		}

		DeleteBuffer();

		unsigned int internalFormat = 0;
		unsigned int imageFormat = 0;
		switch (channels)
		{
		case 3:
			m_Format = srgb ? TextureFormat::SRGB8 : TextureFormat::RGB8;
			internalFormat = srgb ? GL_SRGB8 : GL_RGB8;
			imageFormat = GL_RGB;
			break;
		case 4:
			m_Format = srgb ? TextureFormat::SRGB8_ALPHA8 : TextureFormat::RGBA8;
			internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
			imageFormat = GL_RGBA;
			break;
		default:
			m_Format = TextureFormat::UNKNOW;
			FURYW << channels << " channel image not supported!";
			stbi_image_free(pixels);
			return;
		}

		m_Width = width;
		m_Height = height;
		m_Depth = 0;
		m_Mipmap = mipMap;
		m_FilePath = "";
		m_Dirty = false;

		glGenTextures(1, &m_ID);
		glBindTexture(m_TypeUint, m_ID);

		glTexStorage2D(m_TypeUint, m_Mipmap ? FURY_MIPMAP_LEVEL : 1, internalFormat, m_Width, m_Height);
		glTexSubImage2D(m_TypeUint, 0, 0, 0, m_Width, m_Height, imageFormat, GL_UNSIGNED_BYTE, pixels);

		unsigned int filterMode = EnumUtil::FilterModeToUint(m_FilterMode);
		unsigned int wrapMode = EnumUtil::WrapModeToUint(m_WrapMode);
		unsigned int magMode = (m_FilterMode == FilterMode::NEAREST
			|| m_FilterMode == FilterMode::NEAREST_MIPMAP_NEAREST)
			? GL_NEAREST : GL_LINEAR;

		glTexParameteri(m_TypeUint, GL_TEXTURE_MIN_FILTER, filterMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_MAG_FILTER, magMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_S, wrapMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_T, wrapMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_R, wrapMode);

		float color[] = { m_BorderColor.r, m_BorderColor.g, m_BorderColor.b, m_BorderColor.a };
		glTexParameterfv(m_TypeUint, GL_TEXTURE_BORDER_COLOR, color);

		if (m_Mipmap)
			glGenerateMipmap(m_TypeUint);

		glBindTexture(m_TypeUint, 0);

		stbi_image_free(pixels);

		// Retain encoded bytes so FileUtil::SaveFile can extract them to
		// disk when the scene is serialized.
		m_EncodedBytes.assign(bytes, bytes + len);

		FURYD << m_Name << " [" << m_Width << " x " << m_Height << " x "
			<< EnumUtil::TextureTypeToString(m_Type) << " from memory, "
			<< len << " bytes]";

		IncreaseMemory();
	}

	void Texture::CreateEmpty(int width, int height, int depth, TextureFormat format, TextureType type, bool mipMap)
	{
		if (!RenderThread::Get().MayUseGL())
		{
			DispatchGL(this, [this, width, height, depth, format, type, mipMap]() {
				CreateEmpty(width, height, depth, format, type, mipMap);
			});
			return;
		}
		DeleteBuffer();

		if (format == TextureFormat::UNKNOW)
			return;

		m_Mipmap = mipMap;
		m_Format = format;
		m_Dirty = false;
		m_Width = width;
		m_Height = height;
		m_Depth = depth;

		m_Type = type;
		m_TypeUint = EnumUtil::TextureTypeToUnit(m_Type);

		unsigned int internalFormat = EnumUtil::TextureFormatToUint(format).second;

		glGenTextures(1, &m_ID);
		glBindTexture(m_TypeUint, m_ID);

		if (m_Type == TextureType::TEXTURE_2D_ARRAY || m_Type == TextureType::TEXTURE_3D)
		{
			glTexStorage3D(m_TypeUint, m_Mipmap ? FURY_MIPMAP_LEVEL : 1, internalFormat, width, height, depth);
		}
		else
		{
			glTexStorage2D(m_TypeUint, m_Mipmap ? FURY_MIPMAP_LEVEL : 1, internalFormat, width, height);
		}

		unsigned int filterMode = EnumUtil::FilterModeToUint(m_FilterMode);
		unsigned int wrapMode = EnumUtil::WrapModeToUint(m_WrapMode);
		unsigned int magMode = (m_FilterMode == FilterMode::NEAREST
			|| m_FilterMode == FilterMode::NEAREST_MIPMAP_NEAREST)
			? GL_NEAREST : GL_LINEAR;

		glTexParameteri(m_TypeUint, GL_TEXTURE_MIN_FILTER, filterMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_MAG_FILTER, magMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_S, wrapMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_T, wrapMode);
		glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_R, wrapMode);

		float color[] = { m_BorderColor.r, m_BorderColor.g, m_BorderColor.b, m_BorderColor.a };
		glTexParameterfv(m_TypeUint, GL_TEXTURE_BORDER_COLOR, color);

		if (m_Mipmap)
			glGenerateMipmap(m_TypeUint);

		glBindTexture(m_TypeUint, 0);

		FURYD << m_Name << " [" << m_Width << " x " << m_Height << " x " << EnumUtil::TextureTypeToString(m_Type) << "]";

		IncreaseMemory();
	}

	void Texture::SetPixels(const void* pixels)
	{
		if (m_ID == 0)
		{
			FURYW << "Texture buffer not created yet!";
			return;
		}

		glBindTexture(m_TypeUint, m_ID);
		glTexSubImage2D(m_TypeUint, 0, 0, 0, m_Width, m_Height, EnumUtil::TextureFormatToUint(m_Format).second, GL_UNSIGNED_BYTE, pixels);

		if (m_Mipmap)
			glGenerateMipmap(m_TypeUint);

		glBindTexture(m_TypeUint, 0);
	}

	void Texture::UpdateBuffer()
	{
		// CreateFromImage decodes on this (calling) thread and dispatches
		// only its GL upload; CreateEmpty dispatches itself. No
		// self-re-dispatch: path resolution must stay on the caller.
		if (m_ID > 0 || !m_Dirty)
			return;

		m_Dirty = false;

		if (m_FilePath.size() > 0)
			CreateFromImage(m_FilePath, IsSRGB(), m_Mipmap);
		else
			CreateEmpty(m_Width, m_Height, m_Depth, m_Format, m_Type, m_Mipmap);
	}

	void Texture::DeleteBuffer()
	{
		m_Dirty = true;

		if (m_ID != 0)
		{
			DecreaseMemory();
			// GL handles die on the GL thread (direct-exec there, queued
			// from foreign threads). Captured by value: this object may
			// already be gone when a queued delete runs.
			unsigned int id = m_ID;
			RenderThread::Get().EnqueueJob([id]() { glDeleteTextures(1, &id); });
			m_ID = 0;
			m_Width = m_Height = 0;
			m_Format = TextureFormat::UNKNOW;
			m_FilePath = "";
			m_EncodedBytes.clear();
		}
	}

	void Texture::SetPath(const std::string &path)
	{
		m_FilePath = path;
		m_Name = PathBasename(path);
	}

	void Texture::SetFilePathAndSRGB(const std::string &filePath, bool srgb)
	{
		SetPath(filePath);
		// Use the sRGB-encoding family so Texture::Save emits "srgb": true.
		// 3 vs 4 channels gets fixed up later by CreateFromImage on the
		// first runtime load -- for serialization the family is what matters.
		m_Format = srgb ? TextureFormat::SRGB8_ALPHA8 : TextureFormat::RGBA8;
	}

	void Texture::SetOriginalFilename(const std::string &filename)
	{
		m_OriginalFilename = filename;
	}

	bool Texture::IsMemoryBacked() const
	{
		return m_FilePath.empty() && !m_EncodedBytes.empty();
	}

	const std::vector<unsigned char> &Texture::GetEncodedBytes() const
	{
		return m_EncodedBytes;
	}

	const std::string &Texture::GetOriginalFilename() const
	{
		return m_OriginalFilename;
	}

	bool Texture::IsSRGB() const
	{
		return m_Format == TextureFormat::SRGB || m_Format == TextureFormat::SRGB8 ||
			m_Format == TextureFormat::SRGB8_ALPHA8 || m_Format == TextureFormat::SRGB_ALPHA;
	}

	TextureFormat Texture::GetFormat() const
	{
		return m_Format;
	}

	TextureType Texture::GetType() const
	{
		return m_Type;
	}

	unsigned int Texture::GetTypeUint() const
	{
		return m_TypeUint;
	}

	FilterMode Texture::GetFilterMode() const
	{
		return m_FilterMode;
	}

	void Texture::SetFilterMode(FilterMode mode)
	{
		if (m_FilterMode != mode)
		{
			m_FilterMode = mode;
			if (m_ID != 0)
			{
				glBindTexture(m_TypeUint, m_ID);

				unsigned int filterMode = EnumUtil::FilterModeToUint(m_FilterMode);
				glTexParameteri(m_TypeUint, GL_TEXTURE_MIN_FILTER, filterMode);
				// mag has no mip filtering in GL; a mipmap mode here would
				// make the texture incomplete (and black)
				unsigned int magMode = (m_FilterMode == FilterMode::NEAREST
					|| m_FilterMode == FilterMode::NEAREST_MIPMAP_NEAREST)
					? GL_NEAREST : GL_LINEAR;
				glTexParameteri(m_TypeUint, GL_TEXTURE_MAG_FILTER, magMode);

				glBindTexture(m_TypeUint, 0);
			}
		}
	}

	WrapMode Texture::GetWrapMode() const
	{
		return m_WrapMode;
	}

	void Texture::SetWrapMode(WrapMode mode)
	{
		if (m_WrapMode != mode)
		{
			m_WrapMode = mode;
			if (m_ID != 0)
			{
				glBindTexture(m_TypeUint, m_ID);

				unsigned int wrapMode = EnumUtil::WrapModeToUint(m_WrapMode);
				glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_S, wrapMode);
				glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_T, wrapMode);
				glTexParameteri(m_TypeUint, GL_TEXTURE_WRAP_R, wrapMode);

				glBindTexture(m_TypeUint, 0);
			}
		}
	}

	Color Texture::GetBorderColor() const
	{
		return m_BorderColor;
	}

	void Texture::SetBorderColor(Color color)
	{
		if (m_BorderColor != color)
		{
			m_BorderColor = color;
			if (m_ID != 0)
			{
				glBindTexture(m_TypeUint, m_ID);

				float border[] = { m_BorderColor.r, m_BorderColor.g, m_BorderColor.b, m_BorderColor.a };
				glTexParameterfv(m_TypeUint, GL_TEXTURE_BORDER_COLOR, border);

				glBindTexture(m_TypeUint, 0);
			}
		}
	}

	void Texture::GenerateMipMap()
	{
		if (m_ID == 0)
			return;

		m_Mipmap = true;

		glBindTexture(m_TypeUint, m_ID);
		glGenerateMipmap(m_TypeUint);
	}

	bool Texture::GetMipmap() const
	{
		return m_Mipmap;
	}

	int Texture::GetWidth() const
	{
		return m_Width;
	}

	int Texture::GetHeight() const
	{
		return m_Height;
	}

	int Texture::GetDepth() const
	{
		return m_Depth;
	}

	unsigned int Texture::GetID() const
	{
		return m_ID;
	}

	std::string Texture::GetFilePath() const
	{
		return m_FilePath;
	}


	void Texture::IncreaseMemory()
	{
		unsigned int bitPerPixel = EnumUtil::TextureBitPerPixel(m_Format);
		BufferManager::Instance()->IncreaseMemory(m_Width * m_Height * (m_Depth + 1) * bitPerPixel / 8);
	}

	void Texture::DecreaseMemory()
	{
		unsigned int bitPerPixel = EnumUtil::TextureBitPerPixel(m_Format);
		BufferManager::Instance()->DecreaseMemory(m_Width * m_Height * (m_Depth + 1) * bitPerPixel / 8);
	}
}