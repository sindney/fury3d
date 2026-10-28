#include "Fury/RHI/OpenGL/GLResources.h"

#include <algorithm>
#include <unordered_set>

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/RHI/OpenGL/GLEnumMap.h"

namespace fury
{
	namespace
	{
		// Extension scan cached per process; render thread only.
		bool GLHasExtension(const char *name)
		{
			static std::unordered_set<std::string> s_Exts;
			static bool s_Scanned = false;
			if (!s_Scanned)
			{
				GLint count = 0;
				glGetIntegerv(GL_NUM_EXTENSIONS, &count);
				for (GLint i = 0; i < count; i++)
					s_Exts.insert((const char *)glGetStringi(GL_EXTENSIONS, i));
				s_Scanned = true;
			}
			return s_Exts.count(name) > 0;
		}

		bool CompressedFormatSupported(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::BC1_UNORM:
			case TextureFormat::BC1_SRGB:
			case TextureFormat::BC3_UNORM:
			case TextureFormat::BC3_SRGB:
				return GLHasExtension("GL_EXT_texture_compression_s3tc");
			case TextureFormat::BC5_UNORM:
				// RGTC is core since GL 3.0; every context we run on has it.
				return true;
			case TextureFormat::BC6H_UF:
			case TextureFormat::BC7_UNORM:
			case TextureFormat::BC7_SRGB:
				return GLHasExtension("GL_ARB_texture_compression_bptc");
			default:
				return false;
			}
		}

		unsigned int MagFilterFor(FilterMode mode)
		{
			// Mag has no mip filtering in GL; a mipmap mode here would make
			// the texture incomplete (and black).
			return (mode == FilterMode::NEAREST || mode == FilterMode::NEAREST_MIPMAP_NEAREST)
				? GL_NEAREST : GL_LINEAR;
		}
	}

	std::shared_ptr<GLTexture> GLTexture::Create(const RHITextureDesc &desc)
	{
		if (desc.Format == TextureFormat::UNKNOW)
			return nullptr;

		if (EnumUtil::TextureFormatIsCompressed(desc.Format) && !CompressedFormatSupported(desc.Format))
		{
			FURYE << desc.Name << ": compressed format "
				<< EnumUtil::TextureFormatToString(desc.Format)
				<< " not supported by this GL context (cook target mismatch?)";
			return nullptr;
		}

		unsigned int id = 0;
		glGenTextures(1, &id);
		if (id == 0)
		{
			FURYW << "Failed to glGenTextures for " << desc.Name;
			return nullptr;
		}

		const unsigned int target = GLEnumMap::TextureTypeToGL(desc.Type);
		const unsigned int internalFormat = GLEnumMap::TextureFormatToGL(desc.Format).second;

		glBindTexture(target, id);

		if (desc.Type == TextureType::TEXTURE_2D_ARRAY || desc.Type == TextureType::TEXTURE_3D)
		{
			glTexStorage3D(target, desc.MipLevels, internalFormat, desc.Width, desc.Height, desc.Depth);
		}
		else if (desc.Type == TextureType::TEXTURE_1D)
		{
			glTexStorage1D(target, desc.MipLevels, internalFormat, desc.Width);
		}
		else
		{
			glTexStorage2D(target, desc.MipLevels, internalFormat, desc.Width, desc.Height);
		}

		glBindTexture(target, 0);

		auto texture = std::make_shared<GLTexture>();
		texture->m_Desc = desc;
		texture->m_ID = id;
		texture->m_Target = target;
		texture->ApplySamplerState(desc.Sampler);
		return texture;
	}

	GLTexture::~GLTexture()
	{
		if (m_ID != 0)
			glDeleteTextures(1, &m_ID);
	}

	const RHITextureDesc &GLTexture::GetDesc() const
	{
		return m_Desc;
	}

	uintptr_t GLTexture::GetNativeHandle() const
	{
		return m_ID;
	}

	unsigned int GLTexture::GetID() const
	{
		return m_ID;
	}

	unsigned int GLTexture::GetTarget() const
	{
		return m_Target;
	}

	void GLTexture::Upload(const void *data, size_t size, int mipLevel, int layer)
	{
		if (m_ID == 0)
			return;

		const unsigned int baseFormat = GLEnumMap::TextureFormatToGL(m_Desc.Format, false).second;

		// Uncompressed uploads hand float32 data for float formats
		// (GL converts into 16F storage), bytes otherwise.
		unsigned int sourceType = GL_UNSIGNED_BYTE;
		switch (m_Desc.Format)
		{
		case TextureFormat::R16F:
		case TextureFormat::R32F:
		case TextureFormat::RG16F:
		case TextureFormat::RG32F:
		case TextureFormat::RGB16F:
		case TextureFormat::RGB32F:
		case TextureFormat::RGBA16F:
		case TextureFormat::RGBA32F:
			sourceType = GL_FLOAT;
			break;
		default:
			break;
		}

		glBindTexture(m_Target, m_ID);

		if (m_Target == GL_TEXTURE_3D || m_Target == GL_TEXTURE_2D_ARRAY || m_Target == GL_TEXTURE_CUBE_MAP)
		{
			// layer < 0 uploads the whole level's depth.
			const int z = layer >= 0 ? layer : 0;
			const int depth = layer >= 0 ? 1 : std::max(m_Desc.Depth, 1);
			glTexSubImage3D(m_Target, mipLevel, 0, 0, z, m_Desc.Width, m_Desc.Height, depth,
				baseFormat, sourceType, data);
		}
		else
		{
			glTexSubImage2D(m_Target, mipLevel, 0, 0, m_Desc.Width, m_Desc.Height,
				baseFormat, sourceType, data);
		}

		glBindTexture(m_Target, 0);
	}

	void GLTexture::UploadCompressed(const void *data, size_t size, int mipLevel)
	{
		if (m_ID == 0)
			return;

		const unsigned int internalFormat = GLEnumMap::TextureFormatToGL(m_Desc.Format).second;
		const int lw = std::max(1, m_Desc.Width >> mipLevel);
		const int lh = std::max(1, m_Desc.Height >> mipLevel);

		glBindTexture(m_Target, m_ID);
		glCompressedTexSubImage2D(m_Target, mipLevel, 0, 0, lw, lh, internalFormat, (GLsizei)size, data);
		glBindTexture(m_Target, 0);
	}

	void GLTexture::GenerateMipmaps()
	{
		if (m_ID == 0)
			return;

		glBindTexture(m_Target, m_ID);
		glGenerateMipmap(m_Target);
		glBindTexture(m_Target, 0);
	}

	void GLTexture::ApplySamplerState(const RHISamplerDesc &sampler)
	{
		if (m_ID == 0)
			return;

		glBindTexture(m_Target, m_ID);

		glTexParameteri(m_Target, GL_TEXTURE_MIN_FILTER, GLEnumMap::FilterModeToGL(sampler.Filter));
		glTexParameteri(m_Target, GL_TEXTURE_MAG_FILTER, MagFilterFor(sampler.Filter));

		const unsigned int wrap = GLEnumMap::WrapModeToGL(sampler.Wrap);
		glTexParameteri(m_Target, GL_TEXTURE_WRAP_S, wrap);
		glTexParameteri(m_Target, GL_TEXTURE_WRAP_T, wrap);
		glTexParameteri(m_Target, GL_TEXTURE_WRAP_R, wrap);

		float color[] = { sampler.BorderColor.r, sampler.BorderColor.g, sampler.BorderColor.b, sampler.BorderColor.a };
		glTexParameterfv(m_Target, GL_TEXTURE_BORDER_COLOR, color);

		glBindTexture(m_Target, 0);
	}

	bool GLTexture::ReadPixels(RHIPixelData &out) const
	{
		if (m_ID == 0)
			return false;

		int channels = 4;
		switch (GLEnumMap::TextureFormatToGL(m_Desc.Format, false).second)
		{
		case GL_RED: channels = 1; break;
		case GL_RG: channels = 2; break;
		case GL_RGB: channels = 3; break;
		default: break;
		}

		out.Width = m_Desc.Width;
		out.Height = m_Desc.Height;
		out.Format = m_Desc.Format;
		out.Pixels.resize(static_cast<size_t>(m_Desc.Width) * m_Desc.Height * std::max(m_Desc.Depth, 1) * channels);

		glBindTexture(m_Target, m_ID);
		glGetTexImage(m_Target, 0, GLEnumMap::TextureFormatToGL(m_Desc.Format, false).second,
			GL_UNSIGNED_BYTE, out.Pixels.data());
		glBindTexture(m_Target, 0);
		return true;
	}

	std::shared_ptr<GLBuffer> GLBuffer::Create(const RHIBufferDesc &desc, const void *initialData)
	{
		unsigned int target = 0;
		switch (desc.Type)
		{
		case RHIBufferType::VERTEX: target = GL_ARRAY_BUFFER; break;
		case RHIBufferType::INDEX: target = GL_ELEMENT_ARRAY_BUFFER; break;
		case RHIBufferType::UNIFORM: target = GL_UNIFORM_BUFFER; break;
		case RHIBufferType::STORAGE: target = GL_SHADER_STORAGE_BUFFER; break;
		}

		unsigned int id = 0;
		glGenBuffers(1, &id);
		if (id == 0)
			return nullptr;

		glBindBuffer(target, id);
		glBufferData(target, desc.Size, initialData,
			desc.Usage == RHIBufferUsage::DYNAMIC ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
		glBindBuffer(target, 0);

		auto buffer = std::make_shared<GLBuffer>();
		buffer->m_Desc = desc;
		buffer->m_ID = id;
		buffer->m_Target = target;
		return buffer;
	}

	GLBuffer::~GLBuffer()
	{
		if (m_ID != 0)
			glDeleteBuffers(1, &m_ID);
	}

	const RHIBufferDesc &GLBuffer::GetDesc() const
	{
		return m_Desc;
	}

	uintptr_t GLBuffer::GetNativeHandle() const
	{
		return m_ID;
	}

	unsigned int GLBuffer::GetID() const
	{
		return m_ID;
	}

	unsigned int GLBuffer::GetTarget() const
	{
		return m_Target;
	}

	void GLBuffer::Update(const void *data, size_t size, size_t offset)
	{
		if (m_ID == 0)
			return;

		// The element binding is VAO state; an upload must not clobber the
		// index source of whatever VAO is currently bound.
		GLint prevElement = 0;
		if (m_Target == GL_ELEMENT_ARRAY_BUFFER)
			glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElement);

		glBindBuffer(m_Target, m_ID);
		if (size != m_Desc.Size)
		{
			glBufferData(m_Target, size, data,
				m_Desc.Usage == RHIBufferUsage::DYNAMIC ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
			m_Desc.Size = size;
		}
		else
		{
			glBufferSubData(m_Target, offset, size, data);
		}
		glBindBuffer(m_Target, m_Target == GL_ELEMENT_ARRAY_BUFFER ? (GLuint)prevElement : 0);
	}

	std::shared_ptr<GLRenderTargetSet> GLRenderTargetSet::Create(const RHIRenderTargetSetDesc &desc)
	{
		unsigned int fbo = 0;
		glGenFramebuffers(1, &fbo);
		if (fbo == 0)
			return nullptr;

		auto set = std::make_shared<GLRenderTargetSet>();
		set->m_Desc = desc;
		for (const auto &attach : desc.Colors)
			set->m_Colors.push_back(attach.Texture);
		set->m_Depth = desc.Depth.Texture;
		set->m_FBO = fbo;
		set->Attach();

		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			FURYE << "Render target set incomplete (status 0x" << std::hex << status << std::dec << ")";
			return nullptr;
		}
		return set;
	}

	GLRenderTargetSet::~GLRenderTargetSet()
	{
		if (m_FBO != 0)
			glDeleteFramebuffers(1, &m_FBO);
	}

	void GLRenderTargetSet::Attach()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

		m_Width = m_Height = 0;

		for (unsigned int i = 0; i < m_Colors.size(); i++)
		{
			auto *texture = static_cast<GLTexture *>(m_Colors[i].get());
			const auto &texDesc = texture->GetDesc();
			const auto &attach = m_Desc.Colors[i];

			m_Width = std::max(m_Width, texDesc.Width);
			m_Height = std::max(m_Height, texDesc.Height);

			if (texDesc.Type == TextureType::TEXTURE_2D_ARRAY || texDesc.Type == TextureType::TEXTURE_3D)
			{
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
					texture->GetID(), attach.MipLevel, std::max(attach.Layer, 0));
			}
			else if (texDesc.Type == TextureType::TEXTURE_CUBE_MAP)
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + std::max(attach.CubeFace, 0), texture->GetID(), attach.MipLevel);
			}
			else
			{
				glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, texture->GetID(), attach.MipLevel);
			}
		}

		if (m_Desc.HasDepth && m_Depth != nullptr)
		{
			auto *texture = static_cast<GLTexture *>(m_Depth.get());
			const auto &texDesc = texture->GetDesc();
			const auto &attach = m_Desc.Depth;

			m_Width = std::max(m_Width, texDesc.Width);
			m_Height = std::max(m_Height, texDesc.Height);

			const unsigned int attachId = (texDesc.Format == TextureFormat::DEPTH24_STENCIL8
				|| texDesc.Format == TextureFormat::DEPTH32F_STENCIL8)
				? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;

			if (texDesc.Type == TextureType::TEXTURE_2D_ARRAY || texDesc.Type == TextureType::TEXTURE_3D)
			{
				glFramebufferTextureLayer(GL_FRAMEBUFFER, attachId,
					texture->GetID(), attach.MipLevel, std::max(attach.Layer, 0));
			}
			else if (texDesc.Type == TextureType::TEXTURE_CUBE_MAP)
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, attachId,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + std::max(attach.CubeFace, 0), texture->GetID(), attach.MipLevel);
			}
			else
			{
				glFramebufferTexture(GL_FRAMEBUFFER, attachId, texture->GetID(), attach.MipLevel);
			}
		}

		if (m_Colors.size() > 0)
		{
			std::vector<unsigned int> buffers(m_Colors.size());
			for (unsigned int i = 0; i < m_Colors.size(); i++)
				buffers[i] = GL_COLOR_ATTACHMENT0 + i;
			glDrawBuffers((GLsizei)m_Colors.size(), buffers.data());
		}
		else
		{
			glDrawBuffers(0, GL_NONE);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void GLRenderTargetSet::SetAttachmentLayer(unsigned int index, int layer)
	{
		if (index >= m_Colors.size() && !(m_Desc.HasDepth && index == m_Colors.size()))
			return;

		bool isDepth = index >= m_Colors.size();
		auto *texture = static_cast<GLTexture *>((isDepth ? m_Depth : m_Colors[index]).get());
		const auto &texDesc = texture->GetDesc();

		unsigned int attachId = GL_COLOR_ATTACHMENT0 + index;
		if (isDepth)
		{
			attachId = (texDesc.Format == TextureFormat::DEPTH24_STENCIL8
				|| texDesc.Format == TextureFormat::DEPTH32F_STENCIL8)
				? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
		}

		GLint prevFbo = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);

		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glFramebufferTextureLayer(GL_FRAMEBUFFER, attachId, texture->GetID(), 0, layer);
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	}

	void GLRenderTargetSet::SetAttachmentCubeFace(unsigned int index, int face)
	{
		if (index >= m_Colors.size() && !(m_Desc.HasDepth && index == m_Colors.size()))
			return;

		bool isDepth = index >= m_Colors.size();
		auto *texture = static_cast<GLTexture *>((isDepth ? m_Depth : m_Colors[index]).get());
		const auto &texDesc = texture->GetDesc();

		unsigned int attachId = GL_COLOR_ATTACHMENT0 + index;
		if (isDepth)
		{
			attachId = (texDesc.Format == TextureFormat::DEPTH24_STENCIL8
				|| texDesc.Format == TextureFormat::DEPTH32F_STENCIL8)
				? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
		}

		GLint prevFbo = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);

		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachId, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, texture->GetID(), 0);
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	}

	bool GLRenderTargetSet::HasLayeredAttachments() const
	{
		auto isLayered = [](const RHITextureRef &tex)
		{
			if (tex == nullptr)
				return false;
			auto t = static_cast<GLTexture *>(tex.get())->GetDesc().Type;
			return t == TextureType::TEXTURE_2D_ARRAY || t == TextureType::TEXTURE_3D
				|| t == TextureType::TEXTURE_CUBE_MAP;
		};
		for (const auto &c : m_Colors)
		{
			if (isLayered(c))
				return true;
		}
		return m_Desc.HasDepth && isLayered(m_Depth);
	}

	void GLRenderTargetSet::ClearAllLayers(ClearMode mode, const Color &color)
	{
		if (mode == ClearMode::NONE)
			return;

		const bool wantColor = mode == ClearMode::COLOR || mode == ClearMode::COLOR_DEPTH
			|| mode == ClearMode::COLOR_DEPTH_STENCIL || mode == ClearMode::COLOR_STENCIL;
		const bool wantDepth = mode == ClearMode::DEPTH || mode == ClearMode::COLOR_DEPTH
			|| mode == ClearMode::COLOR_DEPTH_STENCIL || mode == ClearMode::STENCIL_DEPTH;
		const bool wantStencil = mode == ClearMode::STENCIL || mode == ClearMode::COLOR_STENCIL
			|| mode == ClearMode::COLOR_DEPTH_STENCIL || mode == ClearMode::STENCIL_DEPTH;

		glClearColor(color.r, color.g, color.b, color.a);

		GLint prevFbo = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

		auto clearAttachment = [&](GLenum attachId, GLTexture *texture, bool isDepth)
		{
			const auto &texDesc = texture->GetDesc();
			GLenum bits = 0;
			if (isDepth)
			{
				if (wantDepth) bits |= GL_DEPTH_BUFFER_BIT;
				if (wantStencil) bits |= GL_STENCIL_BUFFER_BIT;
			}
			else if (wantColor)
			{
				bits |= GL_COLOR_BUFFER_BIT;
			}
			if (bits == 0)
				return;

			if (texDesc.Type == TextureType::TEXTURE_2D_ARRAY || texDesc.Type == TextureType::TEXTURE_3D)
			{
				for (int layer = 0; layer < texDesc.Depth; layer++)
				{
					glFramebufferTextureLayer(GL_FRAMEBUFFER, attachId, texture->GetID(), 0, layer);
					glClear(bits);
				}
			}
			else if (texDesc.Type == TextureType::TEXTURE_CUBE_MAP)
			{
				for (int face = 0; face < 6; face++)
				{
					glFramebufferTexture2D(GL_FRAMEBUFFER, attachId,
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, texture->GetID(), 0);
					glClear(bits);
				}
			}
			else
			{
				glClear(bits);
			}
		};

		for (unsigned int i = 0; i < m_Colors.size(); i++)
			clearAttachment(GL_COLOR_ATTACHMENT0 + i, static_cast<GLTexture *>(m_Colors[i].get()), false);

		if (m_Desc.HasDepth && m_Depth != nullptr)
		{
			auto *texture = static_cast<GLTexture *>(m_Depth.get());
			const auto &texDesc = texture->GetDesc();
			GLenum attachId = (texDesc.Format == TextureFormat::DEPTH24_STENCIL8
				|| texDesc.Format == TextureFormat::DEPTH32F_STENCIL8)
				? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
			clearAttachment(attachId, texture, true);
		}

		// Put the default attachments back so later draws start from layer 0.
		Attach();

		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	}

	int GLRenderTargetSet::GetWidth() const
	{
		return m_Width;
	}

	int GLRenderTargetSet::GetHeight() const
	{
		return m_Height;
	}

	unsigned int GLRenderTargetSet::GetColorCount() const
	{
		return (unsigned int)m_Colors.size();
	}

	RHITextureRef GLRenderTargetSet::GetColorTexture(unsigned int index) const
	{
		return index < m_Colors.size() ? m_Colors[index] : nullptr;
	}

	RHITextureRef GLRenderTargetSet::GetDepthTexture() const
	{
		return m_Depth;
	}

	uintptr_t GLRenderTargetSet::GetNativeHandle() const
	{
		return m_FBO;
	}

	unsigned int GLRenderTargetSet::GetFBO() const
	{
		return m_FBO;
	}
}
