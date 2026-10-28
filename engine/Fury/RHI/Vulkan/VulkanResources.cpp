#include "Fury/RHI/Vulkan/VulkanResources.h"

#if FURY_RHI_VULKAN

#include <algorithm>
#include <cstring>
#include <vector>

#include "Fury/Log.h"
#include "Fury/RHI/Vulkan/VulkanEnumMap.h"

namespace fury
{
	namespace
	{
		// Allocated mip count clamped to what the dimensions can hold.
		unsigned int ClampMipCount(int requested, int width, int height, int depth)
		{
			unsigned int dim = static_cast<unsigned int>(std::max(std::max(width, height), depth));
			unsigned int maxMips = 1;
			while (dim > 1)
			{
				dim >>= 1;
				++maxMips;
			}
			if (requested < 1)
				requested = 1;
			return static_cast<unsigned int>(requested) > maxMips ? maxMips : static_cast<unsigned int>(requested);
		}

		VkBorderColor BorderColor(const Color &color)
		{
			// Vulkan has four fixed border colors; snap to the closest.
			// Shadow maps use white: out-of-range depth reads 1.0 = lit.
			if (color.r >= 0.5f && color.g >= 0.5f && color.b >= 0.5f && color.a >= 0.5f)
				return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			if (color.a >= 0.5f)
				return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		}

		VkSampler CreateSampler(VkDevice device, const RHISamplerDesc &sampler, unsigned int mipCount)
		{
			VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			sci.magFilter = VulkanEnumMap::SamplerFilter(sampler.Filter);
			sci.minFilter = VulkanEnumMap::SamplerFilter(sampler.Filter);
			sci.mipmapMode = VulkanEnumMap::SamplerMipFilter(sampler.Filter);
			sci.addressModeU = VulkanEnumMap::SamplerWrap(sampler.Wrap);
			sci.addressModeV = VulkanEnumMap::SamplerWrap(sampler.Wrap);
			sci.addressModeW = VulkanEnumMap::SamplerWrap(sampler.Wrap);
			sci.maxLod = mipCount > 1 ? static_cast<float>(mipCount - 1) : 0.0f;
			sci.borderColor = BorderColor(sampler.BorderColor);
			if (sampler.ShadowCompare)
			{
				sci.compareEnable = VK_TRUE;
				sci.compareOp = VK_COMPARE_OP_LESS;
			}
			VkSampler out = VK_NULL_HANDLE;
			if (vkCreateSampler(device, &sci, nullptr, &out) != VK_SUCCESS)
				FURYE << "VulkanTexture: vkCreateSampler failed";
			return out;
		}
	}

	std::shared_ptr<VulkanTexture> VulkanTexture::Create(VkDevice device, VmaAllocator allocator, const RHITextureDesc &desc)
	{
		const VkFormat format = VulkanEnumMap::PixelFormat(desc.Format);
		if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE
			|| format == VK_FORMAT_UNDEFINED || desc.Width <= 0 || desc.Height <= 0)
			return nullptr;

		auto out = std::make_shared<VulkanTexture>();
		out->m_Desc = desc;
		out->m_Device = device;
		out->m_Allocator = allocator;

		const unsigned int mipCount = ClampMipCount(desc.MipLevels, desc.Width, desc.Height,
			desc.Type == TextureType::TEXTURE_3D ? desc.Depth : 1);

		VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ici.imageType = VulkanEnumMap::ImageType(desc.Type);
		ici.format = format;
		ici.extent.width = static_cast<unsigned int>(desc.Width);
		ici.extent.height = static_cast<unsigned int>(desc.Height);
		ici.extent.depth = desc.Type == TextureType::TEXTURE_3D ? static_cast<unsigned int>(std::max(desc.Depth, 1)) : 1;
		ici.mipLevels = mipCount;
		ici.arrayLayers = out->GetLayerCount();
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (desc.RenderTarget)
		{
			ici.usage |= VulkanEnumMap::IsDepthFormat(desc.Format)
				? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
				: VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (desc.Type == TextureType::TEXTURE_CUBE_MAP)
			ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		else if (desc.Type == TextureType::TEXTURE_3D)
			ici.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;

		VmaAllocationCreateInfo aci{};
		aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		if (vmaCreateImage(allocator, &ici, &aci, &out->m_Image, &out->m_Allocation, nullptr) != VK_SUCCESS)
		{
			FURYE << "VulkanTexture: vmaCreateImage failed for '" << desc.Name << "'";
			return nullptr;
		}

		VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		vci.image = out->m_Image;
		vci.format = format;
		vci.viewType = VulkanEnumMap::ImageViewType(desc.Type);
		vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		vci.subresourceRange.aspectMask = VulkanEnumMap::AspectMask(desc.Format);
		vci.subresourceRange.levelCount = mipCount;
		vci.subresourceRange.layerCount = out->GetLayerCount();
		if (vkCreateImageView(device, &vci, nullptr, &out->m_View) != VK_SUCCESS)
		{
			FURYE << "VulkanTexture: vkCreateImageView failed for '" << desc.Name << "'";
			return nullptr;
		}

		out->m_Sampler = CreateSampler(device, desc.Sampler, mipCount);
		return out;
	}

	VulkanTexture::~VulkanTexture()
	{
		// Device teardown already idled the GPU; just drain the handles.
		if (m_Device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(m_Device);
		ReleaseNative();
	}

	void VulkanTexture::ReleaseNative()
	{
		if (m_Device != VK_NULL_HANDLE && m_Sampler != VK_NULL_HANDLE)
			vkDestroySampler(m_Device, m_Sampler, nullptr);
		if (m_Device != VK_NULL_HANDLE && m_View != VK_NULL_HANDLE)
			vkDestroyImageView(m_Device, m_View, nullptr);
		if (m_Allocator != VK_NULL_HANDLE && m_Image != VK_NULL_HANDLE)
			vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
		m_Sampler = VK_NULL_HANDLE;
		m_View = VK_NULL_HANDLE;
		m_Image = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_Device = VK_NULL_HANDLE;
		m_Allocator = VK_NULL_HANDLE;
	}

	const RHITextureDesc &VulkanTexture::GetDesc() const
	{
		return m_Desc;
	}

	uintptr_t VulkanTexture::GetNativeHandle() const
	{
		return reinterpret_cast<uintptr_t>(m_Image);
	}

	VkImageAspectFlags VulkanTexture::GetAspectMask() const
	{
		return VulkanEnumMap::AspectMask(m_Desc.Format);
	}

	unsigned int VulkanTexture::GetMipCount() const
	{
		return ClampMipCount(m_Desc.MipLevels, m_Desc.Width, m_Desc.Height,
			m_Desc.Type == TextureType::TEXTURE_3D ? m_Desc.Depth : 1);
	}

	unsigned int VulkanTexture::GetLayerCount() const
	{
		if (m_Desc.Type == TextureType::TEXTURE_CUBE_MAP)
			return 6;
		if (m_Desc.Type == TextureType::TEXTURE_2D_ARRAY)
			return static_cast<unsigned int>(std::max(m_Desc.Depth, 1));
		return 1;
	}

	void VulkanTexture::ApplySampler(VkDevice device, const RHISamplerDesc &sampler)
	{
		VkSampler next = CreateSampler(device, sampler, GetMipCount());
		if (m_Device != VK_NULL_HANDLE && m_Sampler != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_Device);
			vkDestroySampler(m_Device, m_Sampler, nullptr);
		}
		m_Sampler = next;
		m_Desc.Sampler = sampler;
	}

	bool VulkanBuffer::Allocate(size_t capacity)
	{
		const VkDeviceSize size = capacity > 0 ? capacity : 1;
		VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bci.size = size;
		bci.usage = VulkanEnumMap::BufferUsage(m_Desc.Type) | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		// Host-visible coherent storage for bring-up: uploads and updates are
		// plain writes through the persistent mapping.
		VmaAllocationCreateInfo aci{};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		aci.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VmaAllocationInfo info{};
		if (vmaCreateBuffer(m_Allocator, &bci, &aci, &buffer, &allocation, &info) != VK_SUCCESS)
		{
			FURYE << "VulkanBuffer: vmaCreateBuffer failed for '" << m_Desc.Name << "'";
			return false;
		}
		m_Buffer = buffer;
		m_Allocation = allocation;
		m_Mapped = info.pMappedData;
		m_Capacity = static_cast<size_t>(size);
		return true;
	}

	void VulkanBuffer::ReleaseRetired()
	{
		for (const RetiredEntry &entry : m_Retired)
			vmaDestroyBuffer(m_Allocator, entry.Buffer, entry.Allocation);
		m_Retired.clear();
	}

	std::shared_ptr<VulkanBuffer> VulkanBuffer::Create(VkDevice device, VmaAllocator allocator, const RHIBufferDesc &desc, const void *initialData)
	{
		if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE)
			return nullptr;

		auto out = std::make_shared<VulkanBuffer>();
		out->m_Desc = desc;
		out->m_Device = device;
		out->m_Allocator = allocator;
		// DYNAMIC buffers start empty and grow via Update.
		if (!out->Allocate(desc.Size > 0 ? desc.Size : 1))
			return nullptr;
		if (initialData != nullptr && desc.Size > 0)
			std::memcpy(out->m_Mapped, initialData, desc.Size);
		return out;
	}

	VulkanBuffer::~VulkanBuffer()
	{
		// Device teardown already idled the GPU; just drain the handles.
		if (m_Device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(m_Device);
		ReleaseNative();
	}

	void VulkanBuffer::ReleaseNative()
	{
		if (m_Allocator != VK_NULL_HANDLE && m_Buffer != VK_NULL_HANDLE)
			vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
		m_Buffer = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_Mapped = nullptr;
		m_Capacity = 0;
		ReleaseRetired();
		m_Device = VK_NULL_HANDLE;
		m_Allocator = VK_NULL_HANDLE;
	}

	const RHIBufferDesc &VulkanBuffer::GetDesc() const
	{
		return m_Desc;
	}

	uintptr_t VulkanBuffer::GetNativeHandle() const
	{
		return reinterpret_cast<uintptr_t>(m_Buffer);
	}

	void VulkanBuffer::Update(const void *data, size_t size, size_t offset)
	{
		if (m_Buffer == VK_NULL_HANDLE || m_Mapped == nullptr || data == nullptr || size == 0)
			return;

		// Writes can land between recorded draws of the in-flight frame, so
		// the update never touches the allocation the command buffer holds:
		// it always takes a fresh one and retires the old for the device to
		// release once the frame fence completes.
		const bool streamWrite = offset == 0 && m_Desc.Usage == RHIBufferUsage::DYNAMIC;
		VkBuffer oldBuffer = m_Buffer;
		VmaAllocation oldAllocation = m_Allocation;
		void *oldMapped = m_Mapped;
		const size_t oldCapacity = m_Capacity;

		const size_t needed = offset + size;
		const size_t nextCapacity = std::max(needed, m_Capacity);
		if (!Allocate(nextCapacity))
			return;

		if (!streamWrite && oldMapped != nullptr && oldCapacity > 0)
			std::memcpy(m_Mapped, oldMapped, std::min(oldCapacity, m_Capacity));
		m_Retired.push_back({ oldBuffer, oldAllocation });
		m_Desc.Size = std::max(m_Desc.Size, needed);
		std::memcpy(static_cast<unsigned char *>(m_Mapped) + offset, data, size);
	}

	std::shared_ptr<VulkanRenderTargetSet> VulkanRenderTargetSet::Create(VkDevice device, const RHIRenderTargetSetDesc &desc)
	{
		if (desc.Colors.empty() && !desc.HasDepth)
		{
			FURYE << "VulkanRenderTargetSet: empty attachment list";
			return nullptr;
		}

		auto out = std::make_shared<VulkanRenderTargetSet>();
		out->m_Device = device;
		out->m_HasDepth = desc.HasDepth;

		auto adopt = [](const RHIRenderTargetAttachment &src, AttachmentState &dst) -> bool {
			dst.Texture = src.Texture;
			dst.MipLevel = src.MipLevel;
			dst.Layer = src.Layer;
			dst.CubeFace = src.CubeFace;
			return src.Texture != nullptr;
		};

		for (const auto &color : desc.Colors)
		{
			AttachmentState state;
			if (!adopt(color, state))
			{
				FURYE << "VulkanRenderTargetSet: null color attachment";
				return nullptr;
			}
			out->m_Colors.push_back(state);
		}

		if (desc.HasDepth && !adopt(desc.Depth, out->m_Depth))
		{
			FURYE << "VulkanRenderTargetSet: null depth attachment";
			return nullptr;
		}

		const RHITextureRef &extentTex = out->m_Colors.empty() ? out->m_Depth.Texture : out->m_Colors[0].Texture;
		out->m_Width = extentTex->GetDesc().Width;
		out->m_Height = extentTex->GetDesc().Height;
		for (const auto &state : out->m_Colors)
		{
			if (state.Texture->GetDesc().Width != out->m_Width || state.Texture->GetDesc().Height != out->m_Height)
			{
				FURYE << "VulkanRenderTargetSet: attachment size mismatch";
				return nullptr;
			}
		}
		return out;
	}

	VulkanRenderTargetSet::~VulkanRenderTargetSet()
	{
		// Device teardown already idled the GPU; just drain the handles.
		if (m_Device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(m_Device);
		ReleaseNative();
	}

	void VulkanRenderTargetSet::ReleaseNative()
	{
		for (const ViewEntry &entry : m_Views)
		{
			if (entry.View != VK_NULL_HANDLE)
				vkDestroyImageView(m_Device, entry.View, nullptr);
		}
		m_Views.clear();
		m_Device = VK_NULL_HANDLE;
	}

	int VulkanRenderTargetSet::GetWidth() const { return m_Width; }

	int VulkanRenderTargetSet::GetHeight() const { return m_Height; }

	unsigned int VulkanRenderTargetSet::GetColorCount() const
	{
		return static_cast<unsigned int>(m_Colors.size());
	}

	RHITextureRef VulkanRenderTargetSet::GetColorTexture(unsigned int index) const
	{
		return index < m_Colors.size() ? m_Colors[index].Texture : nullptr;
	}

	RHITextureRef VulkanRenderTargetSet::GetDepthTexture() const
	{
		return m_HasDepth ? m_Depth.Texture : nullptr;
	}

	void VulkanRenderTargetSet::SetAttachmentLayer(unsigned int index, int layer)
	{
		// index == GetColorCount() addresses the depth attachment (GL parity).
		if (index >= m_Colors.size())
			m_Depth.Layer = layer;
		else
			m_Colors[index].Layer = layer;
	}

	void VulkanRenderTargetSet::SetAttachmentCubeFace(unsigned int index, int face)
	{
		if (index >= m_Colors.size())
			m_Depth.CubeFace = face;
		else
			m_Colors[index].CubeFace = face;
	}

	VkImageView VulkanRenderTargetSet::GetAttachmentView(unsigned int index)
	{
		const bool depth = index >= m_Colors.size();
		const AttachmentState &state = depth ? m_Depth : m_Colors[index];
		VulkanTexture *tex = state.Texture == nullptr ? nullptr : static_cast<VulkanTexture *>(state.Texture.get());
		if (tex == nullptr || tex->Get() == VK_NULL_HANDLE)
			return VK_NULL_HANDLE;

		// 3D attachments use per-slice 2D views; other whole textures reuse their view.
		if (state.MipLevel == 0 && state.Layer < 0 && state.CubeFace < 0 && tex->GetDesc().Type != TextureType::TEXTURE_3D)
			return tex->GetView();

		const void *key = state.Texture.get();
		for (const ViewEntry &entry : m_Views)
		{
			if (entry.KeyTexture == key && entry.MipLevel == state.MipLevel
				&& entry.Layer == state.Layer && entry.CubeFace == state.CubeFace)
				return entry.View;
		}

		const bool singleSlice = state.CubeFace >= 0 || state.Layer >= 0;
		const TextureType type = tex->GetDesc().Type;

		VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		vci.image = tex->Get();
		vci.format = VulkanEnumMap::PixelFormat(tex->GetDesc().Format);
		vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		vci.subresourceRange.aspectMask = tex->GetAspectMask();
		vci.subresourceRange.baseMipLevel = static_cast<unsigned int>(std::max(state.MipLevel, 0));
		vci.subresourceRange.levelCount = 1;
		vci.subresourceRange.baseArrayLayer = state.CubeFace >= 0 ? static_cast<unsigned int>(state.CubeFace)
			: (state.Layer >= 0 ? static_cast<unsigned int>(state.Layer) : 0);
		if (type == TextureType::TEXTURE_3D)
		{
			vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vci.subresourceRange.baseArrayLayer = static_cast<unsigned int>(std::max(state.Layer, 0));
			vci.subresourceRange.layerCount = 1;
		}
		else
		{
			vci.viewType = singleSlice ? VK_IMAGE_VIEW_TYPE_2D : VulkanEnumMap::ImageViewType(type);
			vci.subresourceRange.layerCount = singleSlice ? 1 : tex->GetLayerCount();
		}

		VkImageView view = VK_NULL_HANDLE;
		if (vkCreateImageView(m_Device, &vci, nullptr, &view) != VK_SUCCESS)
		{
			FURYE << "VulkanRenderTargetSet: vkCreateImageView failed";
			return VK_NULL_HANDLE;
		}
		m_Views.push_back({ key, state.MipLevel, state.Layer, state.CubeFace, view });
		return view;
	}

	void VulkanRenderTargetSet::ResolveSlice(const AttachmentState &state, TextureType type, unsigned int &outLevel, unsigned int &outSlice)
	{
		outLevel = static_cast<unsigned int>(std::max(state.MipLevel, 0));
		if (type == TextureType::TEXTURE_CUBE_MAP)
			outSlice = state.CubeFace >= 0 ? static_cast<unsigned int>(state.CubeFace) : 0;
		else if (state.Layer >= 0)
			outSlice = static_cast<unsigned int>(state.Layer);
		else
			outSlice = 0;
	}
}

#endif // FURY_RHI_VULKAN
