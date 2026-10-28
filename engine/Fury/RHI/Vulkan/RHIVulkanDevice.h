#ifndef _FURY_RHI_VULKAN_DEVICE_H_
#define _FURY_RHI_VULKAN_DEVICE_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_VULKAN

#include <volk.h>
#include <vk_mem_alloc.h>

#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)
#include <tracy/TracyVulkan.hpp>
#endif

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "Fury/RHI/RHIDevice.h"

namespace fury
{
	class VulkanCommandList;
	class VulkanBuffer;
	class VulkanTexture;
	class VulkanRenderTargetSet;
	class VulkanShaderModule;

	class FURY_API RHIVulkanDevice : public RHIDevice
	{
		friend class VulkanCommandList;
	protected:
		sf::WindowBase *m_Window = nullptr;
		bool m_Vsync = true;
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VmaAllocator m_Allocator = VK_NULL_HANDLE;
		VkQueue m_Queue = VK_NULL_HANDLE;
		unsigned int m_QueueFamily = 0;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> m_BackbufferImages;
		std::vector<VkImageView> m_BackbufferViews;
		std::vector<VkImageView> m_BackbufferUnormViews;
		std::vector<VkImageLayout> m_BackbufferLayouts;
		VkFormat m_BackbufferFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D m_BackbufferExtent = { 0, 0 };
		unsigned int m_BackbufferIndex = 0;
		bool m_BackbufferAcquired = false;
		VkSemaphore m_ImageAvailable = VK_NULL_HANDLE;
		VkSemaphore m_RenderFinished = VK_NULL_HANDLE;
		VkFence m_FrameFence = VK_NULL_HANDLE;
		bool m_FrameActive = false;
		bool m_FrameSubmitted = false;
		VkBuffer m_BackbufferReadback = VK_NULL_HANDLE;
		VmaAllocation m_BackbufferReadbackAllocation = VK_NULL_HANDLE;
		size_t m_BackbufferReadbackSize = 0;
		RHICapabilities m_Caps;
		std::unique_ptr<VulkanCommandList> m_CommandList;
		std::vector<std::weak_ptr<VulkanBuffer>> m_Buffers;
		std::vector<std::weak_ptr<VulkanTexture>> m_Textures;
		std::vector<std::weak_ptr<VulkanRenderTargetSet>> m_RenderTargets;
		std::vector<std::weak_ptr<VulkanShaderModule>> m_ShaderModules;
		struct UiTextureEntry
		{
			std::weak_ptr<VulkanTexture> Texture;
			VkDescriptorSet Descriptor = VK_NULL_HANDLE;
		};
		std::unordered_map<const VulkanTexture *, UiTextureEntry> m_UiTextures;
		std::mutex m_UiMutex;
		bool m_UiReady = false;
#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)
		TracyVkCtx m_TracyContext = nullptr;
#endif
		void RegisterUiTexture(const std::shared_ptr<VulkanTexture> &texture);

	public:
		explicit RHIVulkanDevice(const RHIWindowInfo &window);
		~RHIVulkanDevice() override;

		bool IsUsable() const { return m_Device != VK_NULL_HANDLE && m_Surface != VK_NULL_HANDLE && m_CommandList != nullptr; }
		RHIBackend GetBackend() const override { return RHIBackend::VULKAN; }
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
		RHICommandList *GetCommandList() override;
		void BeginFrame() override;
		void EndFrame() override;
		void Present() override;
		bool Readback(const RHITextureRef &texture, RHIPixelData &out) override;
		bool ReadbackBackbuffer(RHIPixelData &out) override;
		bool ReadbackPixelUInt(const RHITextureRef &texture, int x, int y, unsigned int &out) override;
		void RetireDeletedResources() override;
		bool InitUiRenderer() override;
		void ShutdownUiRenderer() override;
		void DrawUiOverlay(void *drawData) override;
		uintptr_t GetUiTextureId(const RHITextureRef &texture) override;

		VkDevice GetDevice() const { return m_Device; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VmaAllocator GetAllocator() const { return m_Allocator; }
		VkQueue GetQueue() const { return m_Queue; }
		unsigned int GetQueueFamily() const { return m_QueueFamily; }
		VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }
#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)
		TracyVkCtx GetTracyContext();
#endif
		bool IsFrameActive() const { return m_FrameActive; }
		VkImageView GetBackbufferView() const;
		VkImageView GetBackbufferUnormView() const;
		VkFormat GetBackbufferFormat() const { return m_BackbufferFormat; }
		VkFormat GetBackbufferUnormFormat() const;
		VkExtent2D GetBackbufferExtent() const { return m_BackbufferExtent; }
		bool AcquireBackbuffer(unsigned int width, unsigned int height);
		bool RecreateSwapchain(unsigned int width, unsigned int height);
		bool ImmediateCommands(const std::function<bool(VkCommandBuffer)> &record);
		bool TransitionTexture(const RHITextureRef &texture);
		bool CreateHostBuffer(size_t size, VkBufferUsageFlags usage, VkBuffer &buffer, VmaAllocation &allocation, void **mapped = nullptr);
		void DestroyHostBuffer(VkBuffer &buffer, VmaAllocation &allocation);
	};

	RHIDevice *CreateVulkanDevice(const RHIWindowInfo &window);
}

#endif // FURY_RHI_VULKAN

#endif // _FURY_RHI_VULKAN_DEVICE_H_
