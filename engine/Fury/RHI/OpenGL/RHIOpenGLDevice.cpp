#include "Fury/RHI/OpenGL/RHIOpenGLDevice.h"

#include <SFML/Window.hpp>

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/RHI/OpenGL/GLResources.h"
#include "Fury/RHI/OpenGL/GLShaderModule.h"

namespace fury
{
	RHIOpenGLDevice::RHIOpenGLDevice(sf::Window *window)
		: m_Window(window)
	{
		m_Caps.NdcDepthZeroToOne = false;
		m_Caps.ClipSpaceYDown = false;
		m_Caps.SupportsSSBO = gl::IsVersionGEQ(4, 3);
		m_Caps.SupportsCompute = gl::HasComputeShaders();
		m_Caps.SupportsTexture3D = true;
		m_Caps.SupportsCompressedBC = true;

		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_Caps.MaxTextureSize);
		if (m_Caps.SupportsTexture3D)
			glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &m_Caps.MaxTextureArrayLayers);
	}

	RHIOpenGLDevice::~RHIOpenGLDevice()
	{
		if (m_PixelReadbackFBO != 0)
			glDeleteFramebuffers(1, &m_PixelReadbackFBO);
	}

	RHIBackend RHIOpenGLDevice::GetBackend() const
	{
		return RHIBackend::OPENGL;
	}

	const RHICapabilities &RHIOpenGLDevice::GetCapabilities() const
	{
		return m_Caps;
	}

	RHITextureRef RHIOpenGLDevice::CreateTexture(const RHITextureDesc &desc)
	{
		return GLTexture::Create(desc);
	}

	void RHIOpenGLDevice::UpdateTexture(const RHITextureRef &texture, const void *data, size_t size, int mipLevel, int layer)
	{
		auto *glTexture = static_cast<GLTexture *>(texture.get());
		if (EnumUtil::TextureFormatIsCompressed(glTexture->GetDesc().Format))
			glTexture->UploadCompressed(data, size, mipLevel);
		else
			glTexture->Upload(data, size, mipLevel, layer);
	}

	void RHIOpenGLDevice::UpdateTextureFloat32(const RHITextureRef &texture, const float *data, size_t componentCount, int mipLevel, int layer)
	{
		// GL converts float32 into the float texture's storage (16F) itself.
		UpdateTexture(texture, data, componentCount * sizeof(float), mipLevel, layer);
	}

	void RHIOpenGLDevice::GenerateMipmaps(const RHITextureRef &texture)
	{
		static_cast<GLTexture *>(texture.get())->GenerateMipmaps();
	}

	void RHIOpenGLDevice::UpdateSampler(const RHITextureRef &texture, const RHISamplerDesc &sampler)
	{
		auto *glTexture = static_cast<GLTexture *>(texture.get());
		glTexture->SetSamplerDesc(sampler);
		glTexture->ApplySamplerState(sampler);
	}

	RHIBufferRef RHIOpenGLDevice::CreateBuffer(const RHIBufferDesc &desc, const void *initialData)
	{
		return GLBuffer::Create(desc, initialData);
	}

	void RHIOpenGLDevice::UpdateBuffer(const RHIBufferRef &buffer, const void *data, size_t size, size_t offset)
	{
		static_cast<GLBuffer *>(buffer.get())->Update(data, size, offset);
	}

	RHIShaderModuleRef RHIOpenGLDevice::CreateShaderModule(const std::string &name, const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry)
	{
		auto toString = [](const RHIShaderBlob &blob) {
			return std::string((const char *)blob.Data.data(), blob.Data.size());
		};
		return GLShaderModule::Create(name, toString(vertex), toString(fragment), toString(geometry));
	}

	RHIShaderModuleRef RHIOpenGLDevice::CreateComputeModule(const std::string &name, const RHIShaderBlob &compute)
	{
		return GLShaderModule::CreateCompute(name, std::string((const char *)compute.Data.data(), compute.Data.size()));
	}

	RHIGraphicsPipelineRef RHIOpenGLDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const RHIShaderModuleRef &shaders)
	{
		return std::make_shared<GLGraphicsPipeline>(desc, std::static_pointer_cast<GLShaderModule>(shaders));
	}

	RHIRenderTargetSetRef RHIOpenGLDevice::CreateRenderTargetSet(const RHIRenderTargetSetDesc &desc)
	{
		return GLRenderTargetSet::Create(desc);
	}

	RHICommandList *RHIOpenGLDevice::GetCommandList()
	{
		return &m_CommandList;
	}

	void RHIOpenGLDevice::BeginFrame()
	{
		BeginFrameTracking();
	}

	void RHIOpenGLDevice::EndFrame()
	{
		RetireDeletedResources();
	}

	void RHIOpenGLDevice::Present()
	{
		if (m_Window != nullptr)
			m_Window->display();
	}

	bool RHIOpenGLDevice::Readback(const RHITextureRef &texture, RHIPixelData &out)
	{
		if (texture == nullptr)
			return false;
		return static_cast<GLTexture *>(texture.get())->ReadPixels(out);
	}

	bool RHIOpenGLDevice::ReadbackBackbuffer(RHIPixelData &out)
	{
		if (m_Window == nullptr)
			return false;

		const auto size = m_Window->getSize();
		if (size.x == 0 || size.y == 0)
			return false;

		out.Width = (int)size.x;
		out.Height = (int)size.y;
		out.Format = TextureFormat::RGBA8;
		out.Pixels.resize(static_cast<size_t>(size.x) * size.y * 4);
		glReadPixels(0, 0, (GLsizei)size.x, (GLsizei)size.y, GL_RGBA, GL_UNSIGNED_BYTE, out.Pixels.data());
		return true;
	}

	bool RHIOpenGLDevice::ReadbackPixelUInt(const RHITextureRef &texture, int x, int y, unsigned int &out)
	{
		if (texture == nullptr)
			return false;

		auto *glTexture = static_cast<GLTexture *>(texture.get());
		if (m_PixelReadbackFBO == 0)
			glGenFramebuffers(1, &m_PixelReadbackFBO);

		GLint prevRead = 0, prevDraw = 0;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_PixelReadbackFBO);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, glTexture->GetID(), 0);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &out);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
		return true;
	}

	void RHIOpenGLDevice::RetireDeletedResources()
	{
		// GL objects are deleted in place on the render thread.
	}

	RHIDevice *CreateOpenGLDevice(const RHIWindowInfo &window)
	{
		if (window.Window == nullptr)
		{
			FURYE << "OpenGLRHI needs a window with an active GL context";
			return nullptr;
		}
		return new RHIOpenGLDevice(static_cast<sf::Window *>(window.Window));
	}
}
