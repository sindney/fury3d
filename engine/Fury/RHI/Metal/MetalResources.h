#ifndef _FURY_METAL_RESOURCES_H_
#define _FURY_METAL_RESOURCES_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_METAL

#include <Metal/Metal.hpp>

#include "Fury/RHI/RHIResources.h"

namespace fury
{
	class FURY_API MetalTexture : public RHITexture
	{
	protected:

		RHITextureDesc m_Desc;

		MTL::Texture *m_Texture = nullptr;

		MTL::SamplerState *m_Sampler = nullptr;

		// Copies bytes that are already in the texture's native layout.
		void UploadNative(const void *data, int mipLevel, int layer);

	public:

		// Allocates storage and builds the paired sampler. Returns nullptr
		// when the format has no MTL mapping.
		static std::shared_ptr<MetalTexture> Create(MTL::Device *device, const RHITextureDesc &desc);

		~MetalTexture() override;

		const RHITextureDesc &GetDesc() const override;

		uintptr_t GetNativeHandle() const override;

		MTL::Texture *Get() const { return m_Texture; }

		MTL::SamplerState *GetSampler() const { return m_Sampler; }

		// Uncompressed upload. layer >= 0 selects an array layer / cube face
		// (slice) or one 3D z-slice; -1 uploads the whole level. 3-channel
		// formats expand to RGBA CPU-side (no 3-channel MTL pixel formats).
		void Upload(const void *data, size_t size, int mipLevel, int layer);

		// Float32 source upload: the components are converted to the
		// texture's storage precision (half for 16F formats) before the
		// copy. Upload takes the bytes as they land in memory.
		void UploadFloat32(const float *data, size_t componentCount, int mipLevel, int layer);

		// Pre-blocked BC upload for one mip level.
		void UploadCompressed(const void *data, size_t size, int mipLevel);

		void ApplySampler(MTL::Device *device, const RHISamplerDesc &sampler);
	};

	typedef std::shared_ptr<MetalTexture> MetalTextureRef;

	class FURY_API MetalBuffer : public RHIBuffer
	{
	protected:

		RHIBufferDesc m_Desc;

		MTL::Buffer *m_Buffer = nullptr;

	public:

		static std::shared_ptr<MetalBuffer> Create(MTL::Device *device, const RHIBufferDesc &desc, const void *initialData);

		~MetalBuffer() override;

		const RHIBufferDesc &GetDesc() const override;

		uintptr_t GetNativeHandle() const override;

		MTL::Buffer *Get() const { return m_Buffer; }

		// Reallocs when size grows past the allocation (stream semantics:
		// content is replaced wholesale, the command buffer retains the old
		// allocation until the GPU is done).
		void Update(MTL::Device *device, const void *data, size_t size, size_t offset);
	};

	typedef std::shared_ptr<MetalBuffer> MetalBufferRef;

	class FURY_API MetalIndirectBatch : public RHIIndirectBatch
	{
	protected:

		RHIIndirectBatchDesc m_Desc;

		MTL::IndirectCommandBuffer *m_Commands = nullptr;

		MetalBufferRef m_Ranges;

		MTL::Buffer *m_Resources = nullptr;

		MTL::Buffer *m_TextureArguments = nullptr;

		MTL::Buffer *m_GlobalArguments[2] = { nullptr, nullptr };

		int m_GlobalArgumentIndices[2] = { -1, -1 };

		int m_TextureArgumentIndex = -1;

		std::uint32_t m_ChunkCount = 0;

	public:

		explicit MetalIndirectBatch(const RHIIndirectBatchDesc &desc) : m_Desc(desc) {}

		~MetalIndirectBatch() override;

		const RHIIndirectBatchDesc &GetDesc() const override { return m_Desc; }

		bool CreateNative(MTL::Device *device, MTL::RenderPipelineState *pipeline, std::uint32_t maxDrawCount);

		bool CreateTextureArguments(MTL::Function *fragment, int bufferIndex,
			const std::vector<std::uint32_t> &textureIndices, const std::vector<std::uint32_t> &samplerIndices);

		bool CreateGlobalArgument(MTL::Device *device, int stage, int bufferIndex, size_t size);

		bool UpdateGlobalArgument(int stage, const void *data, size_t size);

		MTL::Buffer *GetGlobalArgument(int stage) const { return stage >= 0 && stage < 2 ? m_GlobalArguments[stage] : nullptr; }

		int GetGlobalArgumentIndex(int stage) const { return stage >= 0 && stage < 2 ? m_GlobalArgumentIndices[stage] : -1; }

		MTL::IndirectCommandBuffer *GetCommands() const { return m_Commands; }

		MTL::Buffer *GetRanges() const { return m_Ranges != nullptr ? m_Ranges->Get() : nullptr; }

		RHIBufferRef GetExecutionRangeBuffer() const override { return m_Ranges; }

		MTL::Buffer *GetResources() const { return m_Resources; }

		MTL::Buffer *GetTextureArguments() const { return m_TextureArguments; }

		int GetTextureArgumentIndex() const { return m_TextureArgumentIndex; }

		std::uint32_t GetChunkCount() const { return m_ChunkCount; }
	};

	typedef std::shared_ptr<MetalIndirectBatch> MetalIndirectBatchRef;

	class FURY_API MetalRenderTargetSet : public RHIRenderTargetSet
	{
	public:

		struct AttachmentState
		{
			RHITextureRef Texture;

			int MipLevel = 0;

			// -1: whole texture (all slices); else one array layer / 3D slice.
			int Layer = -1;

			// -1: not a cube attachment; else cube face 0-5.
			int CubeFace = -1;
		};

	protected:

		std::vector<AttachmentState> m_Colors;

		AttachmentState m_Depth;

		bool m_HasDepth = false;

		int m_Width = 0;

		int m_Height = 0;

	public:

		static std::shared_ptr<MetalRenderTargetSet> Create(const RHIRenderTargetSetDesc &desc);

		int GetWidth() const override;

		int GetHeight() const override;

		unsigned int GetColorCount() const override;

		RHITextureRef GetColorTexture(unsigned int index) const override;

		RHITextureRef GetDepthTexture() const override;

		// No FBO equivalent; the render pass descriptor is rebuilt per Begin.
		uintptr_t GetNativeHandle() const override { return 0; }

		void SetAttachmentLayer(unsigned int index, int layer) override;

		void SetAttachmentCubeFace(unsigned int index, int face) override;

		const AttachmentState &GetColorState(unsigned int index) const { return m_Colors[index]; }

		const AttachmentState &GetDepthState() const { return m_Depth; }

		bool HasDepth() const { return m_HasDepth; }

		// Mip level + slice for one attachment state (command-list use).
		static void ResolveSlice(const AttachmentState &state, TextureType type, unsigned int &outLevel, unsigned int &outSlice);
	};

	typedef std::shared_ptr<MetalRenderTargetSet> MetalRenderTargetSetRef;
}

#endif // FURY_RHI_METAL

#endif // _FURY_METAL_RESOURCES_H_
