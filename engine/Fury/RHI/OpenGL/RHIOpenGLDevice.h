#ifndef _FURY_RHI_OPENGL_DEVICE_H_
#define _FURY_RHI_OPENGL_DEVICE_H_

#include "Fury/RHI/RHIDevice.h"
#include "Fury/RHI/OpenGL/GLCommandList.h"

namespace sf
{
	class Window;
}

namespace fury
{
	class FURY_API RHIOpenGLDevice : public RHIDevice
	{
	protected:

		sf::Window *m_Window = nullptr;

		RHICapabilities m_Caps;

		GLCommandList m_CommandList;

		unsigned int m_PixelReadbackFBO = 0;

	public:

		RHIOpenGLDevice(sf::Window *window);

		~RHIOpenGLDevice() override;

		RHIBackend GetBackend() const override;

		const RHICapabilities &GetCapabilities() const override;

		RHITextureRef CreateTexture(const RHITextureDesc &desc) override;

		void UpdateTexture(const RHITextureRef &texture, const void *data, size_t size, int mipLevel = 0, int layer = -1) override;

		void UpdateTextureFloat32(const RHITextureRef &texture, const float *data, size_t componentCount, int mipLevel = 0, int layer = -1) override;

		void GenerateMipmaps(const RHITextureRef &texture) override;

		void UpdateSampler(const RHITextureRef &texture, const RHISamplerDesc &sampler) override;

		RHIBufferRef CreateBuffer(const RHIBufferDesc &desc, const void *initialData = nullptr) override;

		void UpdateBuffer(const RHIBufferRef &buffer, const void *data, size_t size, size_t offset = 0) override;

		RHIShaderModuleRef CreateShaderModule(const std::string &name, const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry) override;

		RHIShaderModuleRef CreateComputeModule(const std::string &name, const RHIShaderBlob &compute) override;

		RHIGraphicsPipelineRef CreateGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const RHIShaderModuleRef &shaders) override;

		RHIRenderTargetSetRef CreateRenderTargetSet(const RHIRenderTargetSetDesc &desc) override;

		RHICommandList *GetCommandList() override;

		void BeginFrame() override;

		void EndFrame() override;

		void Present() override;

		bool Readback(const RHITextureRef &texture, RHIPixelData &out) override;

		bool ReadbackBackbuffer(RHIPixelData &out) override;

		bool ReadbackPixelUInt(const RHITextureRef &texture, int x, int y, unsigned int &out) override;

		void RetireDeletedResources() override;
	};
}

#endif // _FURY_RHI_OPENGL_DEVICE_H_
