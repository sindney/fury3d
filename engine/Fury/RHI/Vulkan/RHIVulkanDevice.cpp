#include "Fury/RHI/Vulkan/RHIVulkanDevice.h"

#if FURY_RHI_VULKAN

#include <SFML/Window/Vulkan.hpp>
#include <SFML/Window/WindowBase.hpp>

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Fury/Log.h"
#include "Fury/RHI/Vulkan/VulkanCommandList.h"
#include "Fury/RHI/Vulkan/VulkanEnumMap.h"
#include "Fury/RHI/Vulkan/VulkanImGuiShader.h"
#include "Fury/RHI/Vulkan/VulkanResources.h"
#include "Fury/RHI/Vulkan/VulkanShaderModule.h"

namespace fury
{
	namespace
	{
		VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
		{
			if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
				FURYE << "Vulkan validation: " << data->pMessage;
			else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
				FURYW << "Vulkan validation: " << data->pMessage;
			else
				FURYD << "Vulkan validation: " << data->pMessage;
			return VK_FALSE;
		}

		bool HasInstanceExtension(const std::vector<VkExtensionProperties> &extensions, const char *name)
		{
			return std::any_of(extensions.begin(), extensions.end(), [&](const auto &value) { return std::strcmp(value.extensionName, name) == 0; });
		}

		bool HasDeviceExtension(VkPhysicalDevice device, const char *name)
		{
			unsigned int count = 0;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
			std::vector<VkExtensionProperties> extensions(count);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
			return std::any_of(extensions.begin(), extensions.end(), [&](const auto &value) { return std::strcmp(value.extensionName, name) == 0; });
		}

		unsigned int ChannelCount(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::R8: case TextureFormat::R16: case TextureFormat::R16F: case TextureFormat::R32F: case TextureFormat::R32UI:
			case TextureFormat::DEPTH16: case TextureFormat::DEPTH24: case TextureFormat::DEPTH32F: case TextureFormat::DEPTH32F_STENCIL8: case TextureFormat::DEPTH24_STENCIL8:
				return 1;
			case TextureFormat::RG8: case TextureFormat::RG16: case TextureFormat::RG16F: case TextureFormat::RG32F: return 2;
			case TextureFormat::RGB8: case TextureFormat::RGB16: case TextureFormat::RGB16F: case TextureFormat::RGB32F:
			case TextureFormat::SRGB: case TextureFormat::SRGB8: return 3;
			case TextureFormat::RGBA8: case TextureFormat::RGBA16: case TextureFormat::RGBA16F: case TextureFormat::RGBA32F:
			case TextureFormat::SRGB_ALPHA: case TextureFormat::SRGB8_ALPHA8: return 4;
			default: return 0;
			}
		}

		bool IsFloatFormat(TextureFormat format)
		{
			switch (format)
			{
			case TextureFormat::R16F: case TextureFormat::R32F: case TextureFormat::RG16F: case TextureFormat::RG32F:
			case TextureFormat::RGB16F: case TextureFormat::RGB32F: case TextureFormat::RGBA16F: case TextureFormat::RGBA32F:
			case TextureFormat::DEPTH32F: case TextureFormat::DEPTH24:
				return true;
			default: return false;
			}
		}

		bool IsHalfFormat(TextureFormat format)
		{
			return format == TextureFormat::R16F || format == TextureFormat::RG16F || format == TextureFormat::RGB16F || format == TextureFormat::RGBA16F;
		}

		unsigned short FloatToHalf(float value)
		{
			unsigned int bits = 0;
			std::memcpy(&bits, &value, 4);
			const unsigned int sign = (bits >> 16) & 0x8000u;
			const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
			unsigned int mantissa = bits & 0x7FFFFFu;
			if (exponent >= 31) return static_cast<unsigned short>(sign | 0x7C00u);
			if (exponent <= 0)
			{
				if (exponent < -10) return static_cast<unsigned short>(sign);
				mantissa |= 0x800000u;
				const unsigned int shift = 14 - exponent;
				unsigned int result = mantissa >> shift;
				const unsigned int rest = mantissa & ((1u << shift) - 1u);
				if (rest > (1u << (shift - 1)) || (rest == (1u << (shift - 1)) && (result & 1u))) ++result;
				return static_cast<unsigned short>(sign | result);
			}
			unsigned int result = (exponent << 10) | (mantissa >> 13);
			const unsigned int rest = mantissa & 0x1FFFu;
			if (rest > 0x1000u || (rest == 0x1000u && (result & 1u))) ++result;
			return static_cast<unsigned short>(sign | result);
		}

		float HalfToFloat(unsigned short value)
		{
			const unsigned int sign = value & 0x8000u;
			const unsigned int exponent = (value >> 10) & 0x1Fu;
			const unsigned int mantissa = value & 0x3FFu;
			float result = exponent == 0 ? mantissa / 16777216.0f : (1.0f + mantissa / 1024.0f) * std::pow(2.0f, static_cast<int>(exponent) - 15);
			return sign ? -result : result;
		}

		unsigned char To8(float value)
		{
			return static_cast<unsigned char>(std::max(0.0f, std::min(1.0f, value)) * 255.0f);
		}
	}

	RHIDevice *CreateVulkanDevice(const RHIWindowInfo &window)
	{
		auto *device = new RHIVulkanDevice(window);
		if (!device->IsUsable())
		{
			delete device;
			return nullptr;
		}
		return device;
	}

	RHIVulkanDevice::RHIVulkanDevice(const RHIWindowInfo &window)
		: m_Window(window.Window), m_Vsync(window.Vsync)
	{
		if (m_Window == nullptr || volkInitialize() != VK_SUCCESS)
		{
			FURYE << "Vulkan loader initialization failed";
			return;
		}
		unsigned int extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
		std::vector<const char *> extensions = sf::Vulkan::getGraphicsRequiredInstanceExtensions();
		const bool debugUtils = HasInstanceExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (HasInstanceExtension(availableExtensions, "VK_KHR_portability_enumeration")) extensions.push_back("VK_KHR_portability_enumeration");
		bool validation = false;
#ifdef FURY_BUILD_DEBUG
		unsigned int layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> layers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
		validation = std::any_of(layers.begin(), layers.end(), [](const auto &layer) { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
#endif
		const char *validationLayer = "VK_LAYER_KHRONOS_validation";
		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "fury3d";
		app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		app.pEngineName = "fury3d";
		app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &app;
		instanceInfo.enabledExtensionCount = static_cast<unsigned int>(extensions.size());
		instanceInfo.ppEnabledExtensionNames = extensions.data();
		instanceInfo.enabledLayerCount = validation ? 1u : 0u;
		instanceInfo.ppEnabledLayerNames = validation ? &validationLayer : nullptr;
		if (HasInstanceExtension(availableExtensions, "VK_KHR_portability_enumeration"))
			instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		VkDebugUtilsMessengerCreateInfoEXT debugInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugInfo.pfnUserCallback = DebugCallback;
		if (debugUtils) instanceInfo.pNext = &debugInfo;
		if (vkCreateInstance(&instanceInfo, nullptr, &m_Instance) != VK_SUCCESS)
		{
			FURYE << "Vulkan instance creation failed";
			return;
		}
		volkLoadInstance(m_Instance);
		if (debugUtils && vkCreateDebugUtilsMessengerEXT != nullptr)
			vkCreateDebugUtilsMessengerEXT(m_Instance, &debugInfo, nullptr, &m_DebugMessenger);
		if (!m_Window->createVulkanSurface(m_Instance, m_Surface))
		{
			FURYE << "SFML Vulkan surface creation failed";
			return;
		}
		unsigned int deviceCount = 0;
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
		std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, physicalDevices.data());
		int bestScore = -1;
		unsigned int bestQueue = 0;
		for (VkPhysicalDevice candidate : physicalDevices)
		{
			if (!HasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
				continue;
			unsigned int queueCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
			std::vector<VkQueueFamilyProperties> queues(queueCount);
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
			for (unsigned int i = 0; i < queueCount; ++i)
			{
				VkBool32 present = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_Surface, &present);
				if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || !present)
					continue;
				VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
				VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
				features2.pNext = &features13;
				vkGetPhysicalDeviceFeatures2(candidate, &features2);
				if (!features13.dynamicRendering)
					continue;
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(candidate, &properties);
				const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
				if (score > bestScore)
				{
					bestScore = score;
					m_PhysicalDevice = candidate;
					bestQueue = i;
				}
			}
		}
		if (m_PhysicalDevice == VK_NULL_HANDLE)
		{
			FURYE << "No Vulkan 1.3 device supports dynamic rendering and presentation";
			return;
		}
		m_QueueFamily = bestQueue;
		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = m_QueueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;
		std::vector<const char *> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		if (HasDeviceExtension(m_PhysicalDevice, VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME))
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME);
		if (HasDeviceExtension(m_PhysicalDevice, "VK_KHR_portability_subset"))
			deviceExtensions.push_back("VK_KHR_portability_subset");
		VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features13.dynamicRendering = VK_TRUE;
		VkPhysicalDeviceFeatures supportedFeatures;
		vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supportedFeatures);
		VkPhysicalDeviceFeatures enabledFeatures = {};
		enabledFeatures.fillModeNonSolid = supportedFeatures.fillModeNonSolid;
		enabledFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
		enabledFeatures.textureCompressionBC = supportedFeatures.textureCompressionBC;
		VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		deviceInfo.pNext = &features13;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = static_cast<unsigned int>(deviceExtensions.size());
		deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
		deviceInfo.pEnabledFeatures = &enabledFeatures;
		deviceInfo.enabledLayerCount = validation ? 1u : 0u;
		deviceInfo.ppEnabledLayerNames = validation ? &validationLayer : nullptr;
		if (vkCreateDevice(m_PhysicalDevice, &deviceInfo, nullptr, &m_Device) != VK_SUCCESS)
		{
			FURYE << "Vulkan logical device creation failed";
			return;
		}
		volkLoadDevice(m_Device);
		vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
		VmaVulkanFunctions functions = {};
		functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		allocatorInfo.physicalDevice = m_PhysicalDevice;
		allocatorInfo.device = m_Device;
		allocatorInfo.instance = m_Instance;
		allocatorInfo.pVulkanFunctions = &functions;
		if (vmaCreateAllocator(&allocatorInfo, &m_Allocator) != VK_SUCCESS)
			return;
		VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = m_QueueFamily;
		if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
			return;
		VkCommandBufferAllocateInfo commandInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		commandInfo.commandPool = m_CommandPool;
		commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandInfo.commandBufferCount = 1;
		vkAllocateCommandBuffers(m_Device, &commandInfo, &m_CommandBuffer);
		VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailable);
		vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinished);
		vkCreateFence(m_Device, &fenceInfo, nullptr, &m_FrameFence);
		m_CommandList = std::make_unique<VulkanCommandList>(this);
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
		m_Caps.NdcDepthZeroToOne = true;
		m_Caps.ClipSpaceYDown = false;
		m_Caps.BackbufferSRGB = true;
		m_Caps.SupportsSSBO = true;
		m_Caps.SupportsCompute = true;
		m_Caps.SupportsTexture3D = true;
		m_Caps.SupportsCompressedBC = supportedFeatures.textureCompressionBC == VK_TRUE;
		m_Caps.MaxTextureSize = properties.limits.maxImageDimension2D;
		m_Caps.MaxTextureArrayLayers = properties.limits.maxImageArrayLayers;
		FURYI << "Vulkan device: " << properties.deviceName;
	}

	RHIVulkanDevice::~RHIVulkanDevice()
	{
		if (m_Device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(m_Device);
#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)
		if (m_TracyContext != nullptr)
		{
			TracyVkDestroy(m_TracyContext);
			m_TracyContext = nullptr;
		}
#endif
		m_CommandList.reset();
		for (auto &weak : m_RenderTargets)
			if (auto resource = weak.lock()) resource->ReleaseNative();
		for (auto &weak : m_ShaderModules)
			if (auto resource = weak.lock()) resource->ReleaseNative();
		for (auto &weak : m_Textures)
			if (auto resource = weak.lock()) resource->ReleaseNative();
		for (auto &weak : m_Buffers)
			if (auto resource = weak.lock()) resource->ReleaseNative();
		m_RenderTargets.clear();
		m_ShaderModules.clear();
		m_Textures.clear();
		m_Buffers.clear();
		DestroyHostBuffer(m_BackbufferReadback, m_BackbufferReadbackAllocation);
		for (VkImageView view : m_BackbufferUnormViews)
			if (view != VK_NULL_HANDLE) vkDestroyImageView(m_Device, view, nullptr);
		for (VkImageView view : m_BackbufferViews)
			if (view != VK_NULL_HANDLE) vkDestroyImageView(m_Device, view, nullptr);
		if (m_Swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
		if (m_ImageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(m_Device, m_ImageAvailable, nullptr);
		if (m_RenderFinished != VK_NULL_HANDLE) vkDestroySemaphore(m_Device, m_RenderFinished, nullptr);
		if (m_FrameFence != VK_NULL_HANDLE) vkDestroyFence(m_Device, m_FrameFence, nullptr);
		if (m_CommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
		if (m_Allocator != VK_NULL_HANDLE) vmaDestroyAllocator(m_Allocator);
		if (m_Device != VK_NULL_HANDLE) vkDestroyDevice(m_Device, nullptr);
		if (m_Surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
		if (m_DebugMessenger != VK_NULL_HANDLE) vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
		if (m_Instance != VK_NULL_HANDLE) vkDestroyInstance(m_Instance, nullptr);
	}

#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)
	TracyVkCtx RHIVulkanDevice::GetTracyContext()
	{
		if (m_TracyContext != nullptr)
			return m_TracyContext;
		VkPhysicalDeviceProperties properties = {};
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
		if (properties.limits.timestampPeriod == 0.0f)
			return nullptr;
		VkCommandBufferAllocateInfo allocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocate.commandPool = m_CommandPool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(m_Device, &allocate, &commandBuffer) != VK_SUCCESS)
			return nullptr;
		m_TracyContext = TracyVkContext(m_PhysicalDevice, m_Device, m_Queue, commandBuffer);
		vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
		return m_TracyContext;
	}
#endif

	bool RHIVulkanDevice::CreateHostBuffer(size_t size, VkBufferUsageFlags usage, VkBuffer &buffer, VmaAllocation &allocation, void **mapped)
	{
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = std::max<size_t>(size, 1);
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VmaAllocationCreateInfo allocationInfo = {};
		allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		VmaAllocationInfo resultInfo = {};
		if (vmaCreateBuffer(m_Allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, &resultInfo) != VK_SUCCESS)
			return false;
		if (mapped != nullptr) *mapped = resultInfo.pMappedData;
		return true;
	}

	void RHIVulkanDevice::DestroyHostBuffer(VkBuffer &buffer, VmaAllocation &allocation)
	{
		if (buffer != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE)
			vmaDestroyBuffer(m_Allocator, buffer, allocation);
		buffer = VK_NULL_HANDLE;
		allocation = VK_NULL_HANDLE;
	}

	bool RHIVulkanDevice::ImmediateCommands(const std::function<bool(VkCommandBuffer)> &record)
	{
		if (m_Device == VK_NULL_HANDLE || vkQueueWaitIdle(m_Queue) != VK_SUCCESS)
			return false;
		VkCommandBufferAllocateInfo allocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocate.commandPool = m_CommandPool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(m_Device, &allocate, &cmd) != VK_SUCCESS)
			return false;
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		bool ok = vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS && record(cmd) && vkEndCommandBuffer(cmd) == VK_SUCCESS;
		if (ok)
		{
			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmd;
			ok = vkQueueSubmit(m_Queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(m_Queue) == VK_SUCCESS;
		}
		vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &cmd);
		return ok;
	}

	bool RHIVulkanDevice::RecreateSwapchain(unsigned int width, unsigned int height)
	{
		if (width == 0 || height == 0)
			return false;
		vkDeviceWaitIdle(m_Device);
		for (VkImageView view : m_BackbufferUnormViews)
			if (view != VK_NULL_HANDLE) vkDestroyImageView(m_Device, view, nullptr);
		for (VkImageView view : m_BackbufferViews) vkDestroyImageView(m_Device, view, nullptr);
		m_BackbufferUnormViews.clear();
		m_BackbufferViews.clear();
		m_BackbufferImages.clear();
		m_BackbufferLayouts.clear();
		if (m_Swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}
		VkSurfaceCapabilitiesKHR capabilities;
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities) != VK_SUCCESS)
			return false;
		unsigned int formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());
		if (formats.empty())
			return false;
		VkSurfaceFormatKHR surfaceFormat = formats[0];
		for (const auto &format : formats)
			if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				surfaceFormat = format;
				break;
			}
		unsigned int presentCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentCount, nullptr);
		std::vector<VkPresentModeKHR> presentModes(presentCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentCount, presentModes.data());
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if (!m_Vsync)
		{
			if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			else if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != presentModes.end()) presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
		VkExtent2D extent = { width, height };
		extent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, extent.width));
		extent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, extent.height));
		unsigned int imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0) imageCount = std::min(imageCount, capabilities.maxImageCount);
		VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		if (HasDeviceExtension(m_PhysicalDevice, VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME))
			info.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
		info.surface = m_Surface;
		info.minImageCount = imageCount;
		info.imageFormat = surfaceFormat.format;
		info.imageColorSpace = surfaceFormat.colorSpace;
		info.imageExtent = extent;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = capabilities.currentTransform;
		info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		info.presentMode = presentMode;
		info.clipped = VK_TRUE;
		if (vkCreateSwapchainKHR(m_Device, &info, nullptr, &m_Swapchain) != VK_SUCCESS)
			return false;
		m_BackbufferFormat = surfaceFormat.format;
		m_BackbufferExtent = extent;
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
		m_BackbufferImages.resize(imageCount);
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_BackbufferImages.data());
		m_BackbufferLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);
		const VkFormat unormFormat = m_BackbufferFormat == VK_FORMAT_B8G8R8A8_SRGB ? VK_FORMAT_B8G8R8A8_UNORM
			: (m_BackbufferFormat == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_UNDEFINED);
		for (VkImage image : m_BackbufferImages)
		{
			VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view.image = image;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = m_BackbufferFormat;
			view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VkImageView handle = VK_NULL_HANDLE;
			if (vkCreateImageView(m_Device, &view, nullptr, &handle) != VK_SUCCESS)
				return false;
			m_BackbufferViews.push_back(handle);
			if ((info.flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) != 0 && unormFormat != VK_FORMAT_UNDEFINED)
			{
				view.format = unormFormat;
				handle = VK_NULL_HANDLE;
				if (vkCreateImageView(m_Device, &view, nullptr, &handle) != VK_SUCCESS)
					return false;
				m_BackbufferUnormViews.push_back(handle);
			}
		}
		m_Caps.SupportsUnormBackbufferView = !m_BackbufferUnormViews.empty();
		return true;
	}

	bool RHIVulkanDevice::AcquireBackbuffer(unsigned int width, unsigned int height)
	{
		if (m_BackbufferAcquired)
			return true;
		if (m_Swapchain == VK_NULL_HANDLE || m_BackbufferExtent.width != width || m_BackbufferExtent.height != height)
			if (!RecreateSwapchain(width, height)) return false;
		VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailable, VK_NULL_HANDLE, &m_BackbufferIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			if (!RecreateSwapchain(width, height)) return false;
			result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailable, VK_NULL_HANDLE, &m_BackbufferIndex);
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			return false;
		m_BackbufferAcquired = true;
		return true;
	}

	VkImageView RHIVulkanDevice::GetBackbufferView() const
	{
		return m_BackbufferAcquired && m_BackbufferIndex < m_BackbufferViews.size() ? m_BackbufferViews[m_BackbufferIndex] : VK_NULL_HANDLE;
	}

	VkImageView RHIVulkanDevice::GetBackbufferUnormView() const
	{
		return m_BackbufferAcquired && m_BackbufferIndex < m_BackbufferUnormViews.size() ? m_BackbufferUnormViews[m_BackbufferIndex] : GetBackbufferView();
	}

	VkFormat RHIVulkanDevice::GetBackbufferUnormFormat() const
	{
		if (m_BackbufferFormat == VK_FORMAT_B8G8R8A8_SRGB) return VK_FORMAT_B8G8R8A8_UNORM;
		if (m_BackbufferFormat == VK_FORMAT_R8G8B8A8_SRGB) return VK_FORMAT_R8G8B8A8_UNORM;
		return m_BackbufferFormat;
	}

	void RHIVulkanDevice::BeginFrame()
	{
		vkWaitForFences(m_Device, 1, &m_FrameFence, VK_TRUE, UINT64_MAX);
		RetireDeletedResources();
		for (auto it = m_Buffers.begin(); it != m_Buffers.end();)
		{
			if (auto buffer = it->lock()) { buffer->ReleaseRetired(); ++it; }
			else it = m_Buffers.erase(it);
		}
		BeginFrameTracking();
		m_BackbufferAcquired = false;
		m_FrameSubmitted = false;
		vkResetCommandBuffer(m_CommandBuffer, 0);
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(m_CommandBuffer, &begin);
		m_FrameActive = true;
		m_CommandList->OnBeginFrame();
	}

	void RHIVulkanDevice::EndFrame()
	{
		m_CommandList->OnEndFrame();
#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)
		if (m_TracyContext != nullptr)
			TracyVkCollect(m_TracyContext, m_CommandBuffer);
#endif
	}

	void RHIVulkanDevice::Present()
	{
		if (!m_FrameActive)
			return;
		m_CommandList->EndRendering();
		if (m_BackbufferAcquired)
		{
			const VkImage image = m_BackbufferImages[m_BackbufferIndex];
			VkImageMemoryBarrier toCopy = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			toCopy.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			toCopy.oldLayout = m_BackbufferLayouts[m_BackbufferIndex];
			toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toCopy.srcQueueFamilyIndex = toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toCopy.image = image;
			toCopy.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier(m_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &toCopy);
			const size_t bytes = static_cast<size_t>(m_BackbufferExtent.width) * m_BackbufferExtent.height * 4;
			if (bytes != m_BackbufferReadbackSize)
			{
				DestroyHostBuffer(m_BackbufferReadback, m_BackbufferReadbackAllocation);
				if (CreateHostBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_BackbufferReadback, m_BackbufferReadbackAllocation))
					m_BackbufferReadbackSize = bytes;
			}
			if (m_BackbufferReadback != VK_NULL_HANDLE)
			{
				VkBufferImageCopy copy = {};
				copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				copy.imageExtent = { m_BackbufferExtent.width, m_BackbufferExtent.height, 1 };
				vkCmdCopyImageToBuffer(m_CommandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_BackbufferReadback, 1, &copy);
			}
			VkImageMemoryBarrier toPresent = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			toPresent.dstAccessMask = 0;
			toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			toPresent.srcQueueFamilyIndex = toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toPresent.image = image;
			toPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier(m_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 0, nullptr, 1, &toPresent);
			m_BackbufferLayouts[m_BackbufferIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
		vkEndCommandBuffer(m_CommandBuffer);
		vkResetFences(m_Device, 1, &m_FrameFence);
		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.waitSemaphoreCount = m_BackbufferAcquired ? 1u : 0u;
		submit.pWaitSemaphores = m_BackbufferAcquired ? &m_ImageAvailable : nullptr;
		submit.pWaitDstStageMask = m_BackbufferAcquired ? &waitStage : nullptr;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &m_CommandBuffer;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &m_RenderFinished;
		m_FrameSubmitted = vkQueueSubmit(m_Queue, 1, &submit, m_FrameFence) == VK_SUCCESS;
		if (m_BackbufferAcquired && m_FrameSubmitted)
		{
			VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			present.waitSemaphoreCount = 1;
			present.pWaitSemaphores = &m_RenderFinished;
			present.swapchainCount = 1;
			present.pSwapchains = &m_Swapchain;
			present.pImageIndices = &m_BackbufferIndex;
			vkQueuePresentKHR(m_Queue, &present);
		}
		m_BackbufferAcquired = false;
		m_FrameActive = false;
		vkQueueWaitIdle(m_Queue);
	}

	RHITextureRef RHIVulkanDevice::CreateTexture(const RHITextureDesc &desc)
	{
		auto texture = VulkanTexture::Create(m_Device, m_Allocator, desc);
		if (texture != nullptr)
		{
			m_Textures.push_back(texture);
			if (!TransitionTexture(texture))
				return nullptr;
			RegisterUiTexture(texture);
		}
		return texture;
	}

	void RHIVulkanDevice::RegisterUiTexture(const std::shared_ptr<VulkanTexture> &texture)
	{
		std::lock_guard<std::mutex> lock(m_UiMutex);
		if (!m_UiReady || texture == nullptr || m_UiTextures.count(texture.get()) != 0)
			return;
		VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(texture->GetView(), VK_IMAGE_LAYOUT_GENERAL);
		if (descriptor != VK_NULL_HANDLE)
			m_UiTextures[texture.get()] = { texture, descriptor };
	}

	uintptr_t RHIVulkanDevice::GetUiTextureId(const RHITextureRef &texture)
	{
		std::lock_guard<std::mutex> lock(m_UiMutex);
		auto found = m_UiTextures.find(static_cast<const VulkanTexture *>(texture.get()));
		return found != m_UiTextures.end() ? reinterpret_cast<uintptr_t>(found->second.Descriptor) : 0;
	}

	void RHIVulkanDevice::RetireDeletedResources()
	{
		std::lock_guard<std::mutex> lock(m_UiMutex);
		if (!m_UiReady)
			return;
		for (auto it = m_UiTextures.begin(); it != m_UiTextures.end();)
		{
			if (it->second.Texture.expired())
			{
				ImGui_ImplVulkan_RemoveTexture(it->second.Descriptor);
				it = m_UiTextures.erase(it);
			}
			else ++it;
		}
	}

	bool RHIVulkanDevice::InitUiRenderer()
	{
		if (ImGui::GetCurrentContext() == nullptr || m_BackbufferFormat == VK_FORMAT_UNDEFINED)
			return false;
		if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3,
			[](const char *name, void *user) { return vkGetInstanceProcAddr(static_cast<VkInstance>(user), name); }, m_Instance))
		{
			FURYE << "Vulkan: ImGui function loading failed";
			return false;
		}
		ImGui_ImplVulkan_InitInfo info = {};
		info.ApiVersion = VK_API_VERSION_1_3;
		info.Instance = m_Instance;
		info.PhysicalDevice = m_PhysicalDevice;
		info.Device = m_Device;
		info.QueueFamily = m_QueueFamily;
		info.Queue = m_Queue;
		info.DescriptorPoolSize = 1024;
		info.MinImageCount = 2;
		info.ImageCount = static_cast<unsigned int>(std::max<size_t>(2, m_BackbufferImages.size()));
		info.UseDynamicRendering = true;
		info.PipelineInfoMain.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_BackbufferFormat;
		if (m_BackbufferUnormViews.empty() && m_Caps.BackbufferSRGB)
		{
			info.CustomShaderFragCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			info.CustomShaderFragCreateInfo.codeSize = sizeof(VulkanImGuiLinearFragment);
			info.CustomShaderFragCreateInfo.pCode = VulkanImGuiLinearFragment;
		}
		if (!ImGui_ImplVulkan_Init(&info))
		{
			FURYE << "Vulkan: ImGui Vulkan backend init failed";
			return false;
		}
		{
			std::lock_guard<std::mutex> lock(m_UiMutex);
			m_UiReady = true;
		}
		for (auto &weak : m_Textures)
			if (auto texture = weak.lock()) RegisterUiTexture(texture);
		return true;
	}

	void RHIVulkanDevice::ShutdownUiRenderer()
	{
		if (!m_UiReady || ImGui::GetCurrentContext() == nullptr)
			return;
		{
			std::lock_guard<std::mutex> lock(m_UiMutex);
			for (auto &entry : m_UiTextures)
				ImGui_ImplVulkan_RemoveTexture(entry.second.Descriptor);
			m_UiTextures.clear();
			m_UiReady = false;
		}
		ImGui_ImplVulkan_Shutdown();
	}

	void RHIVulkanDevice::DrawUiOverlay(void *drawData)
	{
		if (!m_UiReady || drawData == nullptr || m_BackbufferExtent.width == 0 || m_BackbufferExtent.height == 0)
			return;
		ImDrawData *uiData = static_cast<ImDrawData *>(drawData);
		const unsigned int width = static_cast<unsigned int>(uiData->DisplaySize.x * uiData->FramebufferScale.x);
		const unsigned int height = static_cast<unsigned int>(uiData->DisplaySize.y * uiData->FramebufferScale.y);
		if (width == 0 || height == 0 || !AcquireBackbuffer(width, height))
			return;
		m_CommandList->SetSRGBWriteEnabled(false);
		m_CommandList->BeginBackbuffer(m_BackbufferExtent.width, m_BackbufferExtent.height, ClearMode::NONE, Color(0, 0, 0, 1));
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplVulkan_RenderDrawData(uiData, m_CommandBuffer);
		m_CommandList->EndRenderTargets();
		m_CommandList->SetSRGBWriteEnabled(true);
	}

	bool RHIVulkanDevice::TransitionTexture(const RHITextureRef &ref)
	{
		auto *texture = static_cast<VulkanTexture *>(ref.get());
		if (texture == nullptr || texture->GetLayout() == VK_IMAGE_LAYOUT_GENERAL)
			return texture != nullptr;
		const bool ok = ImmediateCommands([&](VkCommandBuffer cmd) {
			VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = texture->Get();
			barrier.subresourceRange = { texture->GetAspectMask(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
			return true;
		});
		if (ok) texture->SetLayout(VK_IMAGE_LAYOUT_GENERAL);
		return ok;
	}

	void RHIVulkanDevice::UpdateTexture(const RHITextureRef &ref, const void *data, size_t size, int mipLevel, int layer)
	{
		auto *texture = static_cast<VulkanTexture *>(ref.get());
		if (texture == nullptr || data == nullptr || size == 0 || mipLevel < 0 || mipLevel >= static_cast<int>(texture->GetMipCount()))
			return;
		const auto &desc = texture->GetDesc();
		const TextureFormat storageFormat = VulkanEnumMap::ExpandTo4Channels(desc.Format);
		const unsigned int width = std::max(1, desc.Width >> mipLevel);
		const unsigned int height = std::max(1, desc.Height >> mipLevel);
		const unsigned int depth = std::max(1, desc.Depth >> mipLevel);
		const unsigned int edge = VulkanEnumMap::BlockEdge(desc.Format);
		const unsigned int sourceBytes = VulkanEnumMap::BytesPerBlock(desc.Format);
		const unsigned int bytes = VulkanEnumMap::BytesPerBlock(storageFormat);
		const size_t blocks = static_cast<size_t>((width + edge - 1) / edge) * ((height + edge - 1) / edge);
		const size_t sourceImageBytes = blocks * sourceBytes;
		const size_t imageBytes = blocks * bytes;
		unsigned int slices = desc.Type == TextureType::TEXTURE_3D ? depth : (desc.Type == TextureType::TEXTURE_2D ? 1 : std::max(1, desc.Depth));
		if (layer >= 0) slices = 1;
		if (size < sourceImageBytes * slices)
		{
			FURYW << "Vulkan texture upload is too small: " << desc.Name;
			return;
		}
		std::vector<unsigned char> expanded;
		if (storageFormat != desc.Format)
		{
			expanded.resize(imageBytes * slices);
			const unsigned char *src = static_cast<const unsigned char *>(data);
			unsigned char *dst = expanded.data();
			for (size_t i = 0; i < blocks * slices; ++i, src += sourceBytes, dst += bytes)
			{
				std::memcpy(dst, src, sourceBytes);
				if (storageFormat == TextureFormat::RGBA32F)
				{
					const float one = 1.0f;
					std::memcpy(dst + sourceBytes, &one, sizeof(one));
				}
				else if (storageFormat == TextureFormat::RGBA16F)
				{
					const unsigned short one = 0x3C00;
					std::memcpy(dst + sourceBytes, &one, sizeof(one));
				}
				else
					std::memset(dst + sourceBytes, 0xFF, bytes - sourceBytes);
			}
			data = expanded.data();
		}
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		void *mapped = nullptr;
		if (!CreateHostBuffer(imageBytes * slices, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, allocation, &mapped))
			return;
		std::memcpy(mapped, data, imageBytes * slices);
		ImmediateCommands([&](VkCommandBuffer cmd) {
			std::vector<VkBufferImageCopy> copies(slices);
			for (unsigned int slice = 0; slice < slices; ++slice)
			{
				copies[slice].bufferOffset = imageBytes * slice;
				copies[slice].imageSubresource = { texture->GetAspectMask(), static_cast<unsigned int>(mipLevel), 0, 1 };
				copies[slice].imageExtent = { width, height, 1 };
				if (desc.Type == TextureType::TEXTURE_3D)
				{
					copies[slice].imageOffset.z = layer >= 0 ? layer : static_cast<int>(slice);
				}
				else
					copies[slice].imageSubresource.baseArrayLayer = layer >= 0 ? layer : slice;
			}
			vkCmdCopyBufferToImage(cmd, staging, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, slices, copies.data());
			VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0, 1, &barrier, 0, nullptr, 0, nullptr);
			return true;
		});
		DestroyHostBuffer(staging, allocation);
	}

	void RHIVulkanDevice::UpdateTextureFloat32(const RHITextureRef &texture, const float *data, size_t componentCount, int mipLevel, int layer)
	{
		if (texture == nullptr || data == nullptr || !IsFloatFormat(texture->GetDesc().Format))
			return;
		if (!IsHalfFormat(texture->GetDesc().Format))
		{
			UpdateTexture(texture, data, componentCount * 4, mipLevel, layer);
			return;
		}
		std::vector<unsigned short> half(componentCount);
		for (size_t i = 0; i < componentCount; ++i) half[i] = FloatToHalf(data[i]);
		UpdateTexture(texture, half.data(), half.size() * 2, mipLevel, layer);
	}

	void RHIVulkanDevice::GenerateMipmaps(const RHITextureRef &ref)
	{
		auto *texture = static_cast<VulkanTexture *>(ref.get());
		if (texture == nullptr || texture->GetMipCount() <= 1)
			return;
		ImmediateCommands([&](VkCommandBuffer cmd) {
			for (unsigned int mip = 1; mip < texture->GetMipCount(); ++mip)
			{
				VkImageBlit blit = {};
				blit.srcSubresource = { texture->GetAspectMask(), mip - 1, 0, texture->GetLayerCount() };
				blit.dstSubresource = { texture->GetAspectMask(), mip, 0, texture->GetLayerCount() };
				blit.srcOffsets[1] = { std::max(1, texture->GetDesc().Width >> (mip - 1)), std::max(1, texture->GetDesc().Height >> (mip - 1)), 1 };
				blit.dstOffsets[1] = { std::max(1, texture->GetDesc().Width >> mip), std::max(1, texture->GetDesc().Height >> mip), 1 };
				vkCmdBlitImage(cmd, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
					VulkanEnumMap::IsDepthFormat(texture->GetDesc().Format) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
				VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 1, &barrier, 0, nullptr, 0, nullptr);
			}
			return true;
		});
	}

	void RHIVulkanDevice::UpdateSampler(const RHITextureRef &texture, const RHISamplerDesc &sampler)
	{
		if (texture != nullptr) static_cast<VulkanTexture *>(texture.get())->ApplySampler(m_Device, sampler);
	}

	RHIBufferRef RHIVulkanDevice::CreateBuffer(const RHIBufferDesc &desc, const void *initialData)
	{
		auto buffer = VulkanBuffer::Create(m_Device, m_Allocator, desc, initialData);
		if (buffer != nullptr) m_Buffers.push_back(buffer);
		return buffer;
	}

	void RHIVulkanDevice::UpdateBuffer(const RHIBufferRef &buffer, const void *data, size_t size, size_t offset)
	{
		if (buffer != nullptr) static_cast<VulkanBuffer *>(buffer.get())->Update(data, size, offset);
	}

	RHIShaderModuleRef RHIVulkanDevice::CreateShaderModule(const std::string &name, const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry)
	{
		auto module = VulkanShaderModule::Create(m_Device, name, vertex, fragment, geometry);
		if (module != nullptr) m_ShaderModules.push_back(module);
		return module;
	}

	RHIShaderModuleRef RHIVulkanDevice::CreateComputeModule(const std::string &name, const RHIShaderBlob &compute)
	{
		auto module = VulkanShaderModule::CreateCompute(m_Device, name, compute);
		if (module != nullptr) m_ShaderModules.push_back(module);
		return module;
	}

	RHIGraphicsPipelineRef RHIVulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const RHIShaderModuleRef &shaders)
	{
		auto module = std::static_pointer_cast<VulkanShaderModule>(shaders);
		return module != nullptr && module->IsValid() ? std::make_shared<VulkanGraphicsPipeline>(desc, module) : nullptr;
	}

	RHIRenderTargetSetRef RHIVulkanDevice::CreateRenderTargetSet(const RHIRenderTargetSetDesc &desc)
	{
		auto targets = VulkanRenderTargetSet::Create(m_Device, desc);
		if (targets != nullptr) m_RenderTargets.push_back(targets);
		return targets;
	}

	RHICommandList *RHIVulkanDevice::GetCommandList()
	{
		return m_CommandList.get();
	}

	bool RHIVulkanDevice::Readback(const RHITextureRef &ref, RHIPixelData &out)
	{
		auto *texture = static_cast<VulkanTexture *>(ref.get());
		if (texture == nullptr || VulkanEnumMap::BlockEdge(texture->GetDesc().Format) != 1)
			return false;
		const auto &desc = texture->GetDesc();
		const unsigned int channels = ChannelCount(desc.Format);
		const unsigned int bytes = VulkanEnumMap::BytesPerBlock(VulkanEnumMap::ExpandTo4Channels(desc.Format));
		if (channels == 0 || bytes == 0)
			return false;
		const unsigned int slices = desc.Type == TextureType::TEXTURE_3D || desc.Type == TextureType::TEXTURE_2D_ARRAY ? std::max(1, desc.Depth) : 1;
		const size_t imageBytes = static_cast<size_t>(desc.Width) * desc.Height * bytes;
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		void *mapped = nullptr;
		if (!CreateHostBuffer(imageBytes * slices, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging, allocation, &mapped))
			return false;
		const bool ok = ImmediateCommands([&](VkCommandBuffer cmd) {
			std::vector<VkBufferImageCopy> copies(slices);
			for (unsigned int slice = 0; slice < slices; ++slice)
			{
				copies[slice].bufferOffset = imageBytes * slice;
				copies[slice].imageSubresource = { texture->GetAspectMask(), 0, 0, 1 };
				copies[slice].imageExtent = { static_cast<unsigned int>(desc.Width), static_cast<unsigned int>(desc.Height), 1 };
				if (desc.Type == TextureType::TEXTURE_3D) copies[slice].imageOffset.z = slice;
				else copies[slice].imageSubresource.baseArrayLayer = slice;
			}
			vkCmdCopyImageToBuffer(cmd, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, staging, slices, copies.data());
			return true;
		});
		if (!ok)
		{
			DestroyHostBuffer(staging, allocation);
			return false;
		}
		out.Width = desc.Width;
		out.Height = desc.Height;
		out.Depth = slices;
		out.Format = TextureFormat::RGBA8;
		out.Pixels.resize(static_cast<size_t>(desc.Width) * desc.Height * slices * 4);
		const unsigned char *raw = static_cast<const unsigned char *>(mapped);
		for (size_t pixel = 0; pixel < static_cast<size_t>(desc.Width) * desc.Height * slices; ++pixel)
		{
			const unsigned char *src = raw + pixel * bytes;
			unsigned char *dst = out.Pixels.data() + pixel * 4;
			dst[0] = dst[1] = dst[2] = 0;
			dst[3] = 255;
			if (IsHalfFormat(desc.Format))
			{
				const auto *values = reinterpret_cast<const unsigned short *>(src);
				for (unsigned int c = 0; c < channels; ++c) dst[c] = To8(HalfToFloat(values[c]));
			}
			else if (IsFloatFormat(desc.Format))
			{
				const auto *values = reinterpret_cast<const float *>(src);
				for (unsigned int c = 0; c < channels; ++c) dst[c] = To8(values[c]);
				if (VulkanEnumMap::IsDepthFormat(desc.Format)) dst[1] = dst[2] = dst[0];
			}
			else if (desc.Format == TextureFormat::R16 || desc.Format == TextureFormat::RG16 || desc.Format == TextureFormat::RGB16 || desc.Format == TextureFormat::RGBA16 || desc.Format == TextureFormat::DEPTH16)
			{
				const auto *values = reinterpret_cast<const unsigned short *>(src);
				for (unsigned int c = 0; c < channels; ++c) dst[c] = values[c] >> 8;
				if (desc.Format == TextureFormat::DEPTH16) dst[1] = dst[2] = dst[0];
			}
			else if (desc.Format == TextureFormat::DEPTH24_STENCIL8)
			{
				dst[0] = dst[1] = dst[2] = src[3];
			}
			else
			{
				for (unsigned int c = 0; c < channels; ++c) dst[c] = src[c];
			}
			if (channels < 3) dst[1] = dst[2] = dst[0];
			if (channels < 4) dst[3] = 255;
		}
		DestroyHostBuffer(staging, allocation);
		return true;
	}

	bool RHIVulkanDevice::ReadbackBackbuffer(RHIPixelData &out)
	{
		if (m_BackbufferReadback == VK_NULL_HANDLE)
			return false;
		if (m_FrameSubmitted) vkWaitForFences(m_Device, 1, &m_FrameFence, VK_TRUE, UINT64_MAX);
		VmaAllocationInfo info = {};
		vmaGetAllocationInfo(m_Allocator, m_BackbufferReadbackAllocation, &info);
		const unsigned char *raw = static_cast<const unsigned char *>(info.pMappedData);
		const unsigned int width = m_BackbufferExtent.width;
		const unsigned int height = m_BackbufferExtent.height;
		const size_t rowBytes = static_cast<size_t>(width) * 4;
		out.Width = width;
		out.Height = height;
		out.Depth = 1;
		out.Format = TextureFormat::RGBA8;
		out.Pixels.resize(rowBytes * height);
		for (unsigned int y = 0; y < height; ++y)
		{
			const unsigned char *src = raw + y * rowBytes;
			unsigned char *dst = out.Pixels.data() + (height - 1 - y) * rowBytes;
			for (unsigned int x = 0; x < width; ++x, src += 4, dst += 4)
			{
				dst[0] = (m_BackbufferFormat == VK_FORMAT_B8G8R8A8_UNORM || m_BackbufferFormat == VK_FORMAT_B8G8R8A8_SRGB) ? src[2] : src[0];
				dst[1] = src[1];
				dst[2] = (m_BackbufferFormat == VK_FORMAT_B8G8R8A8_UNORM || m_BackbufferFormat == VK_FORMAT_B8G8R8A8_SRGB) ? src[0] : src[2];
				dst[3] = src[3];
			}
		}
		return true;
	}

	bool RHIVulkanDevice::ReadbackPixelUInt(const RHITextureRef &ref, int x, int y, unsigned int &out)
	{
		auto *texture = static_cast<VulkanTexture *>(ref.get());
		if (texture == nullptr || texture->GetDesc().Format != TextureFormat::R32UI || x < 0 || y < 0 || x >= texture->GetDesc().Width || y >= texture->GetDesc().Height)
			return false;
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		void *mapped = nullptr;
		if (!CreateHostBuffer(4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging, allocation, &mapped))
			return false;
		const bool ok = ImmediateCommands([&](VkCommandBuffer cmd) {
			VkBufferImageCopy copy = {};
			copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copy.imageOffset = { x, y, 0 };
			copy.imageExtent = { 1, 1, 1 };
			vkCmdCopyImageToBuffer(cmd, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, staging, 1, &copy);
			return true;
		});
		if (ok) std::memcpy(&out, mapped, 4);
		DestroyHostBuffer(staging, allocation);
		return ok;
	}
}

#endif // FURY_RHI_VULKAN
