#ifndef _FURY_RHI_DEVICE_H_
#define _FURY_RHI_DEVICE_H_

#include "Fury/RHI/RHIResources.h"
#include "Fury/RHI/RHITypes.h"

namespace sf
{
	class WindowBase;
}

namespace fury
{
	class RHICommandList;

	// Window the device presents to. GL: a sf::Window with an active context.
	// Vulkan/Metal: a sf::WindowBase (no GL context created).
	struct RHIWindowInfo
	{
		sf::WindowBase *Window = nullptr;

		bool Vsync = true;
	};

	// CPU copy of a texture/backbuffer region, filled by Readback.
	struct RHIPixelData
	{
		int Width = 0;

		int Height = 0;

		// Slice count for array/3D/cube textures (1 for plain 2D); Pixels is
		// [slice][row][pixel] contiguous when > 1.
		int Depth = 1;

		TextureFormat Format = TextureFormat::UNKNOW;

		std::vector<unsigned char> Pixels;
	};

	class FURY_API RHIDevice
	{
	public:

		virtual ~RHIDevice() = default;

		virtual RHIBackend GetBackend() const = 0;

		virtual const RHICapabilities &GetCapabilities() const = 0;

		virtual RHITextureRef CreateTexture(const RHITextureDesc &desc) = 0;

		// Full upload of one mip level (or one layer/slice when layer >= 0);
		// compressed formats take pre-blocked data.
		virtual void UpdateTexture(const RHITextureRef &texture, const void *data, size_t size, int mipLevel = 0, int layer = -1) = 0;

		// Upload of float32 pixel components; the backend converts them to
		// the texture's storage precision (float16 targets). UpdateTexture
		// takes the bytes as they land in memory, so float sources need
		// this path.
		virtual void UpdateTextureFloat32(const RHITextureRef &texture, const float *data, size_t componentCount, int mipLevel = 0, int layer = -1) = 0;

		virtual void GenerateMipmaps(const RHITextureRef &texture) = 0;

		// Re-applies sampler state in place (filter/wrap/border changes).
		virtual void UpdateSampler(const RHITextureRef &texture, const RHISamplerDesc &sampler) = 0;

		virtual RHIBufferRef CreateBuffer(const RHIBufferDesc &desc, const void *initialData = nullptr) = 0;

		virtual void UpdateBuffer(const RHIBufferRef &buffer, const void *data, size_t size, size_t offset = 0) = 0;

		// Programs link from stage blobs: source text for GL/Metal, SPIR-V
		// binary for Vulkan. Empty geometry = no geometry stage.
		virtual RHIShaderModuleRef CreateShaderModule(const std::string &name, const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry) = 0;

		virtual RHIShaderModuleRef CreateComputeModule(const std::string &name, const RHIShaderBlob &compute) = 0;

		virtual RHIGraphicsPipelineRef CreateGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const RHIShaderModuleRef &shaders) = 0;

		virtual RHIRenderTargetSetRef CreateRenderTargetSet(const RHIRenderTargetSetDesc &desc) = 0;

		virtual RHICommandList *GetCommandList() = 0;

		virtual void BeginFrame() = 0;

		virtual void EndFrame() = 0;

		virtual void Present() = 0;

		// UI overlay: the backend's own ImGui renderer, used by backends
		// whose window is not a GL context (Metal). The draw data is the
		// backend's own type (ImDrawData*), kept opaque here so the RHI
		// core stays ImGui-free. Backends without one report false and
		// the engine draws the UI with the GL backend instead.
		virtual bool InitUiRenderer() { return false; }

		virtual void ShutdownUiRenderer() {}

		// Draws the overlay into the backbuffer. Must be called with the
		// backbuffer pass still open (before Present).
		virtual void DrawUiOverlay(void *drawData) {}

		virtual uintptr_t GetUiTextureId(const RHITextureRef &texture)
		{
			return texture != nullptr ? texture->GetNativeHandle() : 0;
		}

		// Synchronous (fence/stall) copy to CPU. Editor picking/screenshots only.
		virtual bool Readback(const RHITextureRef &texture, RHIPixelData &out) = 0;

		virtual bool ReadbackBackbuffer(RHIPixelData &out) = 0;

		// Synchronous single-pixel uint readback (editor id/pick passes).
		virtual bool ReadbackPixelUInt(const RHITextureRef &texture, int x, int y, unsigned int &out) = 0;

		// Every texture used as a render target this frame (command lists call
		// TrackRenderTarget from BeginRenderTargets; cleared on BeginFrame).
		// The GPU dump reads it to cover runtime-created targets (shadow maps,
		// sky LUTs, chain temps) that the pipeline never registers.
		void TrackRenderTarget(const RHITextureRef &texture);

		const std::vector<RHITextureRef> &GetFrameRenderTargets() const { return m_FrameRenderTargets; }

		// Clears the frame render-target list; called from BeginFrame impls.
		void BeginFrameTracking();

		// Backends defer native destruction until the GPU is done; called per frame.
		virtual void RetireDeletedResources() = 0;

	protected:

		std::vector<RHITextureRef> m_FrameRenderTargets;
	};

	// Set by RHI::CreateDevice at engine startup; nullptr before init.
	FURY_API extern RHIDevice *GRHIDevice;
}

#endif // _FURY_RHI_DEVICE_H_
