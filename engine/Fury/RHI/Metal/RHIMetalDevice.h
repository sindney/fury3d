#ifndef _FURY_RHI_METAL_DEVICE_H_
#define _FURY_RHI_METAL_DEVICE_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_METAL

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <map>

#include "Fury/RHI/RHIDevice.h"
#include "Fury/RHI/Metal/MetalCommandList.h"
#include "Fury/RHI/Metal/MetalShaderModule.h"

namespace fury
{
	class FURY_API RHIMetalDevice : public RHIDevice
	{
	public:

		struct PSOKey
		{
			const MetalShaderModule *Module = nullptr;
			BlendMode Blend = BlendMode::REPLACE;
			bool ColorWrite = true;
			bool BlendEnabled[4] = { true, true, true, true };
			uint8_t ColorFormats[4] = { 0, 0, 0, 0 };
			uint8_t DepthFormat = 0;
			uint32_t VertexHash = 0;

			bool operator<(const PSOKey &o) const;
		};

		struct PSOEntry
		{
			MTL::RenderPipelineState *PSO = nullptr;
			MetalBindingMaps Maps;
		};

	protected:

		sf::WindowBase *m_Window = nullptr;

		CA::MetalLayer *m_Layer = nullptr;

		MTL::Device *m_Device = nullptr;

		MTL::CommandQueue *m_Queue = nullptr;

		RHICapabilities m_Caps;

		MetalCommandList m_CommandList;

		// One pool + command buffer per frame; the pool's drain recycles the
		// buffer, drawable and pass descriptors.
		NS::AutoreleasePool *m_FramePool = nullptr;

		MTL::CommandBuffer *m_CommandBuffer = nullptr;

		CA::MetalDrawable *m_Drawable = nullptr;

		// The most recently presented drawable, retained across the frame
		// boundary so between-frame ReadbackBackbuffer calls have a texture
		// to copy (autoreleased drawables die with the frame pool).
		CA::MetalDrawable *m_LastPresented = nullptr;

		MTL::BlitCommandEncoder *m_BlitEncoder = nullptr;

		// Last known drawable size in pixels. SFML window getters do AppKit
		// calls (main thread only), so the render thread never queries the
		// window: the ctor seeds these and BeginBackbuffer dims keep them
		// current (packet.windowW/H is game-thread sourced).
		std::atomic<unsigned int> m_LayerWidth{0};

		std::atomic<unsigned int> m_LayerHeight{0};

		std::map<PSOKey, PSOEntry> m_PSOCache;

		std::map<uint32_t, MTL::DepthStencilState *> m_DepthCache;

	public:

		RHIMetalDevice(const RHIWindowInfo &window);

		~RHIMetalDevice() override;

		// False when device/queue/layer creation failed (factory returns
		// nullptr then so the fallback chain can run).
		bool IsUsable() const { return m_Device != nullptr && m_Queue != nullptr && m_Layer != nullptr; }

		RHIBackend GetBackend() const override { return RHIBackend::METAL; }

		const RHICapabilities &GetCapabilities() const override { return m_Caps; }

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

		RHICommandList *GetCommandList() override { return &m_CommandList; }

		void BeginFrame() override;

		void EndFrame() override;

		void Present() override;

		// UI overlay: the ImGui Metal renderer draws the editor/game UI
		// into the backbuffer (the window is not a GL context here).
		bool InitUiRenderer() override;

		void ShutdownUiRenderer() override;

		void DrawUiOverlay(void *drawData) override;

		bool Readback(const RHITextureRef &texture, RHIPixelData &out) override;

		bool ReadbackBackbuffer(RHIPixelData &out) override;

		bool ReadbackPixelUInt(const RHITextureRef &texture, int x, int y, unsigned int &out) override;

		void RetireDeletedResources() override {}

		// Backend-internal accessors (command list + readbacks).
		MTL::Device *GetMTLDevice() const { return m_Device; }

		MTL::CommandBuffer *GetCommandBuffer() const { return m_CommandBuffer; }

		CA::MetalLayer *GetLayer() const { return m_Layer; }

		// Lazily acquired per frame; nullptr when the layer has no drawable
		// yet (minimized window).
		CA::MetalDrawable *GetDrawable();

		// Ends any open render encoder and hands out a blit encoder on the
		// frame command buffer (mipmap generation, readback copies).
		MTL::BlitCommandEncoder *GetBlitEncoder();

		void EndBlitEncoder();

		// nullptr on creation failure; Maps are built once at creation.
		const PSOEntry *GetOrCreatePSO(const PSOKey &key,
			const std::vector<RHIVertexAttribute> *vertexLayout);

		MTL::DepthStencilState *GetDepthState(CompareMode func, bool write);

		void EndEncoders();

		void SyncLayerSize();

		// Command list reports the current backbuffer dims (game-thread
		// sourced); the layer resizes on change.
		void SetBackbufferSize(unsigned int width, unsigned int height);
	};
}

#endif // FURY_RHI_METAL

#endif // _FURY_RHI_METAL_DEVICE_H_
