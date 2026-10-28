#ifndef _FURY_VULKAN_RESOURCES_H_
#define _FURY_VULKAN_RESOURCES_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_VULKAN

#include <volk.h>
#include <vk_mem_alloc.h>

#include "Fury/RHI/RHIResources.h"

namespace fury
{
	class FURY_API VulkanTexture : public RHITexture
	{
	protected:

		RHITextureDesc m_Desc;

		VkDevice m_Device = VK_NULL_HANDLE;

		VmaAllocator m_Allocator = VK_NULL_HANDLE;

		VkImage m_Image = VK_NULL_HANDLE;

		VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocation m_Allocation = VK_NULL_HANDLE;

		// Whole-texture view (all mips and layers) for sampling.
		VkImageView m_View = VK_NULL_HANDLE;

		VkSampler m_Sampler = VK_NULL_HANDLE;

	public:

		// Allocates the image, the whole-texture view and the paired sampler.
		// Returns nullptr when the format has no Vk mapping.
		static std::shared_ptr<VulkanTexture> Create(VkDevice device, VmaAllocator allocator, const RHITextureDesc &desc);

		~VulkanTexture() override;

		const RHITextureDesc &GetDesc() const override;

		uintptr_t GetNativeHandle() const override;

		VkImage Get() const { return m_Image; }

		VkImageLayout GetLayout() const { return m_Layout; }

		void SetLayout(VkImageLayout layout) { m_Layout = layout; }

		VkImageView GetView() const { return m_View; }

		VkSampler GetSampler() const { return m_Sampler; }

		VmaAllocation GetAllocation() const { return m_Allocation; }

		VkImageAspectFlags GetAspectMask() const;

		unsigned int GetMipCount() const;

		// Array layers: 6 for cube, Depth for 2D arrays, 1 otherwise.
		unsigned int GetLayerCount() const;

		void ApplySampler(VkDevice device, const RHISamplerDesc &sampler);

		// Idempotent native teardown; the device idles and calls this before
		// vmaDestroyAllocator. Safe to run again from the destructor.
		void ReleaseNative();
	};

	typedef std::shared_ptr<VulkanTexture> VulkanTextureRef;

	class FURY_API VulkanBuffer : public RHIBuffer
	{
	protected:

		RHIBufferDesc m_Desc;

		VkDevice m_Device = VK_NULL_HANDLE;

		VmaAllocator m_Allocator = VK_NULL_HANDLE;

		VkBuffer m_Buffer = VK_NULL_HANDLE;

		VmaAllocation m_Allocation = VK_NULL_HANDLE;

		// Persistent host mapping (HOST_VISIBLE | HOST_COHERENT storage).
		void *m_Mapped = nullptr;

		size_t m_Capacity = 0;

		struct RetiredEntry
		{
			VkBuffer Buffer;

			VmaAllocation Allocation;
		};

		// Previous allocations awaiting GPU completion; the parent device
		// calls ReleaseRetired() each BeginFrame after the frame fence.
		std::vector<RetiredEntry> m_Retired;

		bool Allocate(size_t capacity);

	public:

		static std::shared_ptr<VulkanBuffer> Create(VkDevice device, VmaAllocator allocator, const RHIBufferDesc &desc, const void *initialData);

		~VulkanBuffer() override;

		const RHIBufferDesc &GetDesc() const override;

		uintptr_t GetNativeHandle() const override;

		VkBuffer Get() const { return m_Buffer; }

		VmaAllocation GetAllocation() const { return m_Allocation; }

		void *GetMappedData() const { return m_Mapped; }

		size_t GetCapacity() const { return m_Capacity; }

		// Every update takes a fresh allocation (writes between recorded
		// draws must not touch memory the command buffer references);
		// partial/static updates preserve the old bytes, DYNAMIC offset-0
		// writes replace wholesale. The old allocation goes to m_Retired.
		void Update(const void *data, size_t size, size_t offset);

		// Destroys retired allocations; call only after the frame fence.
		void ReleaseRetired();

		// Idempotent native teardown; the device idles and calls this before
		// vmaDestroyAllocator. Safe to run again from the destructor.
		void ReleaseNative();
	};

	typedef std::shared_ptr<VulkanBuffer> VulkanBufferRef;

	class FURY_API VulkanRenderTargetSet : public RHIRenderTargetSet
	{
	public:

		struct AttachmentState
		{
			RHITextureRef Texture;

			int MipLevel = 0;

			// -1: whole texture (all layers); else one array layer / 3D slice.
			int Layer = -1;

			// -1: not a cube attachment; else cube face 0-5.
			int CubeFace = -1;
		};

	protected:

		struct ViewEntry
		{
			const void *KeyTexture;

			int MipLevel;

			int Layer;

			int CubeFace;

			VkImageView View;
		};

		std::vector<AttachmentState> m_Colors;

		AttachmentState m_Depth;

		bool m_HasDepth = false;

		int m_Width = 0;

		int m_Height = 0;

		VkDevice m_Device = VK_NULL_HANDLE;

		// Attachment views created on demand; destroyed with the set.
		std::vector<ViewEntry> m_Views;

	public:

		static std::shared_ptr<VulkanRenderTargetSet> Create(VkDevice device, const RHIRenderTargetSetDesc &desc);

		~VulkanRenderTargetSet() override;

		int GetWidth() const override;

		int GetHeight() const override;

		unsigned int GetColorCount() const override;

		RHITextureRef GetColorTexture(unsigned int index) const override;

		RHITextureRef GetDepthTexture() const override;

		// No FBO equivalent; framebuffers are built per Begin by the
		// command list from GetAttachmentView().
		uintptr_t GetNativeHandle() const override { return 0; }

		void SetAttachmentLayer(unsigned int index, int layer) override;

		void SetAttachmentCubeFace(unsigned int index, int face) override;

		const AttachmentState &GetColorState(unsigned int index) const { return m_Colors[index]; }

		const AttachmentState &GetDepthState() const { return m_Depth; }

		bool HasDepth() const { return m_HasDepth; }

		// Whole-mip view for an attachment's current state, created on
		// demand and cached. index >= GetColorCount() addresses the depth
		// attachment. VK_NULL_HANDLE when the state selects nothing valid.
		VkImageView GetAttachmentView(unsigned int index);

		// Mip level + slice for one attachment state (command-list use).
		static void ResolveSlice(const AttachmentState &state, TextureType type, unsigned int &outLevel, unsigned int &outSlice);

		// Idempotent native teardown; the device idles and calls this before
		// vmaDestroyAllocator. Safe to run again from the destructor.
		void ReleaseNative();
	};

	typedef std::shared_ptr<VulkanRenderTargetSet> VulkanRenderTargetSetRef;
}

#endif // FURY_RHI_VULKAN

#endif // _FURY_VULKAN_RESOURCES_H_
