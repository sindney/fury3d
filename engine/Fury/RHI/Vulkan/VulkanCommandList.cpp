#include "Fury/RHI/Vulkan/VulkanCommandList.h"

#if FURY_RHI_VULKAN

#include <algorithm>
#include <cstring>
#include <tuple>

#include "Fury/Log.h"
#include "Fury/RHI/Vulkan/RHIVulkanDevice.h"
#include "Fury/RHI/Vulkan/VulkanEnumMap.h"

namespace fury
{
	namespace
	{
		bool ClearColor(ClearMode mode)
		{
			return mode == ClearMode::COLOR || mode == ClearMode::COLOR_DEPTH
				|| mode == ClearMode::COLOR_STENCIL || mode == ClearMode::COLOR_DEPTH_STENCIL;
		}

		bool ClearDepth(ClearMode mode)
		{
			return mode == ClearMode::DEPTH || mode == ClearMode::COLOR_DEPTH
				|| mode == ClearMode::STENCIL_DEPTH || mode == ClearMode::COLOR_DEPTH_STENCIL;
		}

		VkDescriptorType DescriptorType(VulkanShaderResourceType type)
		{
			switch (type)
			{
			case VulkanShaderResourceType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case VulkanShaderResourceType::SampledImage: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			case VulkanShaderResourceType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			}
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		}
	}

	bool VulkanCommandList::PipelineKey::operator<(const PipelineKey &o) const
	{
		return std::tie(Module, Blend, DepthFunc, DepthTest, DepthWrite, Cull, Primitive, Fill, ColorWrite,
			BlendEnabled[0], BlendEnabled[1], BlendEnabled[2], BlendEnabled[3],
			ColorFormats[0], ColorFormats[1], ColorFormats[2], ColorFormats[3], DepthFormat, VertexHash)
			< std::tie(o.Module, o.Blend, o.DepthFunc, o.DepthTest, o.DepthWrite, o.Cull, o.Primitive, o.Fill, o.ColorWrite,
				o.BlendEnabled[0], o.BlendEnabled[1], o.BlendEnabled[2], o.BlendEnabled[3],
				o.ColorFormats[0], o.ColorFormats[1], o.ColorFormats[2], o.ColorFormats[3], o.DepthFormat, o.VertexHash);
	}

	VulkanCommandList::VulkanCommandList(RHIVulkanDevice *device)
		: m_Device(device)
	{
		VkDescriptorPoolSize sizes[3] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048 }
		};
		VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		info.maxSets = 16384;
		info.poolSizeCount = 3;
		info.pPoolSizes = sizes;
		if (vkCreateDescriptorPool(m_Device->GetDevice(), &info, nullptr, &m_DescriptorPool) != VK_SUCCESS)
			FURYE << "VulkanCommandList: descriptor pool creation failed";
	}

	VulkanCommandList::~VulkanCommandList()
	{
		VkDevice device = m_Device->GetDevice();
		if (device == VK_NULL_HANDLE)
			return;
		vkDeviceWaitIdle(device);
		for (auto &kv : m_Pipelines)
		{
			if (kv.second.Pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, kv.second.Pipeline, nullptr);
			if (kv.second.Layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, kv.second.Layout, nullptr);
			for (VkDescriptorSetLayout layout : kv.second.SetLayouts)
				if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
		}
		if (m_DescriptorPool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
	}

	void VulkanCommandList::OnBeginFrame()
	{
		EndRendering();
		m_FrameRetain.clear();
		for (auto &buffer : m_GlobalsBuffers)
			if (buffer != nullptr) buffer->ReleaseRetired();
		m_TargetStack.clear();
		m_Targets = nullptr;
		m_Backbuffer = false;
		m_Rendering = false;
		if (m_DescriptorPool != VK_NULL_HANDLE)
			vkResetDescriptorPool(m_Device->GetDevice(), m_DescriptorPool, 0);
	}

	void VulkanCommandList::OnEndFrame()
	{
		EndRendering();
		m_Targets = nullptr;
		m_Backbuffer = false;
		m_TargetStack.clear();
	}

	void VulkanCommandList::DecodeHandle(RHIUniformHandle handle, int &stage, int &index, int &kind)
	{
		if (handle <= 0)
		{
			stage = 0;
			index = -1;
			kind = -1;
			return;
		}
		stage = (handle >> 24) & 0x3;
		index = (handle & 0xFFFFFF) - 1;
		kind = (handle >> 26) & 0x7;
	}

	void VulkanCommandList::SetTargetFormats()
	{
		for (unsigned int i = 0; i < 4; ++i)
			m_ColorFormats[i] = !m_Backbuffer && m_Targets != nullptr && i < m_Targets->GetColorCount()
				? VulkanEnumMap::PixelFormat(m_Targets->GetColorTexture(i)->GetDesc().Format)
				: (i == 0 && m_Backbuffer ? (m_SRGBWriteEnabled ? m_Device->GetBackbufferFormat() : m_Device->GetBackbufferUnormFormat()) : VK_FORMAT_UNDEFINED);
		m_DepthFormat = !m_Backbuffer && m_Targets != nullptr && m_Targets->HasDepth()
			? VulkanEnumMap::PixelFormat(m_Targets->GetDepthTexture()->GetDesc().Format) : VK_FORMAT_UNDEFINED;
	}

	void VulkanCommandList::ClearAttachments(ClearMode mode, const Color &color, bool clearUInt, unsigned int uintValue, bool allLayers)
	{
		if (m_Targets == nullptr)
			return;
		VkCommandBuffer cmd = m_Device->GetCommandBuffer();
		if (cmd == VK_NULL_HANDLE)
			return;
		auto clearOne = [&](const VulkanRenderTargetSet::AttachmentState &state, unsigned int attachment, bool depth) {
			VulkanTexture *texture = static_cast<VulkanTexture *>(state.Texture.get());
			if (texture == nullptr || (depth && !ClearDepth(mode)) || (!depth && !ClearColor(mode)) || (clearUInt && attachment != 0))
				return;
			unsigned int level = 0, slice = 0;
			VulkanRenderTargetSet::ResolveSlice(state, texture->GetDesc().Type, level, slice);
			VkImageSubresourceRange range = { texture->GetAspectMask(), level, 1, 0, 1 };
			if (allLayers || (state.Layer < 0 && state.CubeFace < 0))
			{
				range.baseArrayLayer = 0;
				range.layerCount = texture->GetLayerCount();
			}
			else
			{
				range.baseArrayLayer = slice;
				range.layerCount = 1;
			}
			if (depth)
			{
				VkClearDepthStencilValue value = { 1.0f, 0 };
				vkCmdClearDepthStencilImage(cmd, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
			}
			else if (clearUInt)
			{
				VkClearColorValue value = {};
				value.uint32[0] = value.uint32[1] = value.uint32[2] = value.uint32[3] = uintValue;
				vkCmdClearColorImage(cmd, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
			}
			else
			{
				VkClearColorValue value = {};
				value.float32[0] = color.r;
				value.float32[1] = color.g;
				value.float32[2] = color.b;
				value.float32[3] = color.a;
				vkCmdClearColorImage(cmd, texture->Get(), VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
			}
		};
		for (unsigned int i = 0; i < m_Targets->GetColorCount(); ++i)
			clearOne(m_Targets->GetColorState(i), i, false);
		if (m_Targets->HasDepth())
			clearOne(m_Targets->GetDepthState(), m_Targets->GetColorCount(), true);
	}

	bool VulkanCommandList::BeginRendering(ClearMode clear, const Color &color, bool clearUInt, unsigned int uintValue, bool allLayers)
	{
		if (m_Rendering)
		{
			bool attachmentsChanged = false;
			if (m_Targets != nullptr)
			{
				for (unsigned int i = 0; i < m_Targets->GetColorCount(); ++i)
					attachmentsChanged |= m_ActiveViews[i] != m_Targets->GetAttachmentView(i);
				if (m_Targets->HasDepth())
					attachmentsChanged |= m_ActiveViews[m_Targets->GetColorCount()] != m_Targets->GetAttachmentView(m_Targets->GetColorCount());
			}
			if (!attachmentsChanged)
				return true;
			EndRendering();
		}
		VkCommandBuffer cmd = m_Device->GetCommandBuffer();
		if (cmd == VK_NULL_HANDLE)
			return false;
		if (m_Backbuffer && !m_Device->AcquireBackbuffer(m_TargetWidth, m_TargetHeight))
			return false;

		ClearAttachments(clear, color, clearUInt, uintValue, allLayers);
		VkRenderingAttachmentInfo colors[4] = {};
		VkRenderingAttachmentInfo depth = {};
		VkRenderingAttachmentInfo stencil = {};
		unsigned int colorCount = m_Backbuffer ? 1 : (m_Targets != nullptr ? m_Targets->GetColorCount() : 0);
		auto imageBarrier = [&](VkImage image, VkImageAspectFlags aspect, bool backbuffer) {
			VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.oldLayout = backbuffer ? (m_Device->m_BackbufferLayouts[m_Device->m_BackbufferIndex] == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED) : VK_IMAGE_LAYOUT_GENERAL;
			barrier.newLayout = backbuffer ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
			barrier.srcAccessMask = backbuffer ? 0 : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
			barrier.dstAccessMask = aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
			vkCmdPipelineBarrier(cmd,
				backbuffer ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT),
				aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		};
		(void)imageBarrier;

		if (m_Backbuffer)
		{
			VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.oldLayout = m_Device->m_BackbufferLayouts[m_Device->m_BackbufferIndex];
			barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_Device->m_BackbufferImages[m_Device->m_BackbufferIndex];
			barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
			m_Device->m_BackbufferLayouts[m_Device->m_BackbufferIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colors[0] = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			colors[0].imageView = m_SRGBWriteEnabled ? m_Device->GetBackbufferView() : m_Device->GetBackbufferUnormView();
			m_ActiveViews[0] = colors[0].imageView;
			colors[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colors[0].loadOp = ClearColor(clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			colors[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colors[0].clearValue.color.float32[0] = color.r;
			colors[0].clearValue.color.float32[1] = color.g;
			colors[0].clearValue.color.float32[2] = color.b;
			colors[0].clearValue.color.float32[3] = color.a;
		}
		else if (m_Targets != nullptr)
		{
			for (unsigned int i = 0; i < colorCount; ++i)
			{
				VulkanTexture *texture = static_cast<VulkanTexture *>(m_Targets->GetColorTexture(i).get());
				colors[i] = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				colors[i].imageView = m_Targets->GetAttachmentView(i);
				m_ActiveViews[i] = colors[i].imageView;
				colors[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = texture->Get();
				barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			}
			if (m_Targets->HasDepth())
			{
				VulkanTexture *texture = static_cast<VulkanTexture *>(m_Targets->GetDepthTexture().get());
				depth = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				depth.imageView = m_Targets->GetAttachmentView(m_Targets->GetColorCount());
				m_ActiveViews[m_Targets->GetColorCount()] = depth.imageView;
				depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				stencil = depth;
				VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = texture->Get();
				barrier.subresourceRange = { texture->GetAspectMask(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			}
		}

		VkRenderingInfo rendering = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		rendering.renderArea = { { 0, 0 }, { static_cast<unsigned int>(m_TargetWidth), static_cast<unsigned int>(m_TargetHeight) } };
		rendering.layerCount = 1;
		rendering.colorAttachmentCount = colorCount;
		rendering.pColorAttachments = colors;
		rendering.pDepthAttachment = m_DepthFormat != VK_FORMAT_UNDEFINED ? &depth : nullptr;
		rendering.pStencilAttachment = (m_DepthFormat == VK_FORMAT_D24_UNORM_S8_UINT || m_DepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) ? &stencil : nullptr;
		vkCmdBeginRendering(cmd, &rendering);
		m_Rendering = true;
		vkCmdSetViewport(cmd, 0, 1, &m_Viewport);
		const VkRect2D scissor = m_ScissorEnabled ? m_Scissor : VkRect2D{ { 0, 0 }, { static_cast<unsigned int>(m_TargetWidth), static_cast<unsigned int>(m_TargetHeight) } };
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdSetDepthBias(cmd, m_DepthBiasUnits, 0.0f, m_DepthBiasFactor);
		return true;
	}

	void VulkanCommandList::EndRendering()
	{
		if (!m_Rendering)
			return;
		VkCommandBuffer cmd = m_Device->GetCommandBuffer();
		vkCmdEndRendering(cmd);
		m_Rendering = false;
		for (auto &view : m_ActiveViews) view = VK_NULL_HANDLE;
		if (!m_Backbuffer && m_Targets != nullptr)
		{
			auto barrierOne = [&](const RHITextureRef &ref) {
				VulkanTexture *texture = static_cast<VulkanTexture *>(ref.get());
				const bool depth = VulkanEnumMap::IsDepthFormat(texture->GetDesc().Format);
				VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				barrier.srcAccessMask = depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = texture->Get();
				barrier.subresourceRange = { texture->GetAspectMask(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
				vkCmdPipelineBarrier(cmd,
					depth ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			};
			for (unsigned int i = 0; i < m_Targets->GetColorCount(); ++i)
				barrierOne(m_Targets->GetColorTexture(i));
			if (m_Targets->HasDepth())
				barrierOne(m_Targets->GetDepthTexture());
		}
	}

	void VulkanCommandList::BeginRenderTargets(const RHIRenderTargetSetRef &targets, ClearMode clear, const Color &clearColor)
	{
		if (targets == nullptr)
			return;
		EndRendering();
		Retain(targets);
		m_Targets = std::static_pointer_cast<VulkanRenderTargetSet>(targets);
		m_Backbuffer = false;
		m_TargetWidth = targets->GetWidth();
		m_TargetHeight = targets->GetHeight();
		for (unsigned int i = 0; i < targets->GetColorCount(); ++i)
		{
			Retain(targets->GetColorTexture(i));
			m_Device->TrackRenderTarget(targets->GetColorTexture(i));
		}
		if (targets->GetDepthTexture() != nullptr)
		{
			Retain(targets->GetDepthTexture());
			m_Device->TrackRenderTarget(targets->GetDepthTexture());
		}
		SetTargetFormats();
		SetViewport(0, 0, m_TargetWidth, m_TargetHeight);
		InvalidatePipeline();
		BeginRendering(clear, clearColor, false, 0, true);
	}

	void VulkanCommandList::BeginBackbuffer(int width, int height, ClearMode clear, const Color &clearColor)
	{
		EndRendering();
		m_Targets = nullptr;
		m_Backbuffer = true;
		m_TargetWidth = width;
		m_TargetHeight = height;
		if (!m_Device->AcquireBackbuffer(width, height))
			return;
		SetTargetFormats();
		SetViewport(0, 0, width, height);
		InvalidatePipeline();
		BeginRendering(clear, clearColor);
	}

	void VulkanCommandList::EndRenderTargets()
	{
		EndRendering();
		m_Targets = nullptr;
		m_Backbuffer = false;
		m_DepthFormat = VK_FORMAT_UNDEFINED;
		InvalidatePipeline();
	}

	void VulkanCommandList::SetViewport(int x, int y, int width, int height)
	{
		if (m_Backbuffer)
			m_Viewport = { static_cast<float>(x), static_cast<float>(y + height), static_cast<float>(width), static_cast<float>(-height), 0.0f, 1.0f };
		else
			m_Viewport = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
		if (m_Rendering)
			vkCmdSetViewport(m_Device->GetCommandBuffer(), 0, 1, &m_Viewport);
	}

	void VulkanCommandList::SetScissor(bool enabled, int x, int y, int width, int height)
	{
		m_ScissorEnabled = enabled;
		if (enabled)
		{
			x = std::max(0, x);
			width = std::max(0, std::min(width, m_TargetWidth - x));
			if (m_Backbuffer)
				y = m_TargetHeight - (y + height);
			y = std::max(0, y);
			height = std::max(0, std::min(height, m_TargetHeight - y));
			m_Scissor = { { x, y }, { static_cast<unsigned int>(width), static_cast<unsigned int>(height) } };
		}
		if (m_Rendering)
		{
			const VkRect2D rect = enabled ? m_Scissor : VkRect2D{ { 0, 0 }, { static_cast<unsigned int>(m_TargetWidth), static_cast<unsigned int>(m_TargetHeight) } };
			vkCmdSetScissor(m_Device->GetCommandBuffer(), 0, 1, &rect);
		}
	}

	void VulkanCommandList::Clear(ClearMode mode, const Color &color)
	{
		EndRendering();
		BeginRendering(mode, color);
	}

	void VulkanCommandList::ClearColorUInt(unsigned int value)
	{
		EndRendering();
		BeginRendering(ClearMode::COLOR, Color(0, 0, 0, 1), true, value);
	}

	void VulkanCommandList::InvalidatePipeline()
	{
		m_NativePipeline = nullptr;
	}

	void VulkanCommandList::AdoptModule(const VulkanShaderModuleRef &module)
	{
		if (m_CurrentModule == module)
			return;
		for (int stage = 0; stage < 2; ++stage)
			Retain(m_GlobalsBuffers[stage]);
		m_CurrentModule = module;
		for (int stage = 0; stage < 2; ++stage)
		{
			const auto &globals = module != nullptr ? module->GetStageGlobals(stage) : VulkanStageGlobals{};
			m_GlobalsShadow[stage] = globals.defaultValues;
			if (m_GlobalsShadow[stage].size() < globals.blockSize)
				m_GlobalsShadow[stage].resize(globals.blockSize, 0);
			m_GlobalsBuffers[stage] = nullptr;
			if (globals.HasBlock())
			{
				RHIBufferDesc desc;
				desc.Name = module->GetName() + (stage == 0 ? " vertex globals" : " fragment globals");
				desc.Type = RHIBufferType::UNIFORM;
				desc.Usage = RHIBufferUsage::DYNAMIC;
				desc.Size = globals.blockSize;
				m_GlobalsBuffers[stage] = VulkanBuffer::Create(m_Device->GetDevice(), m_Device->GetAllocator(), desc, m_GlobalsShadow[stage].data());
				Retain(m_GlobalsBuffers[stage]);
			}
		}
		InvalidatePipeline();
	}

	void VulkanCommandList::SetPipeline(const RHIGraphicsPipelineRef &pipeline)
	{
		m_CurrentPipeline = pipeline;
		AdoptModule(pipeline != nullptr ? std::static_pointer_cast<VulkanShaderModule>(pipeline->GetShaders()) : nullptr);
		if (pipeline != nullptr)
		{
			const auto &desc = pipeline->GetDesc();
			m_Blend = desc.Blend;
			m_DepthFunc = desc.DepthFunc;
			m_DepthTest = desc.DepthTest;
			m_DepthWrite = desc.DepthWrite;
			m_Cull = desc.Cull;
			m_Primitive = desc.Primitive;
			m_Fill = desc.Fill;
		}
		m_TextureCursor = 0;
		InvalidatePipeline();
	}

	void VulkanCommandList::BindShaderModule(const RHIShaderModuleRef &module)
	{
		m_CurrentPipeline = nullptr;
		AdoptModule(std::static_pointer_cast<VulkanShaderModule>(module));
		m_TextureCursor = 0;
		InvalidatePipeline();
	}

	void VulkanCommandList::SetRasterState(BlendMode blend, CompareMode depthFunc, bool depthTest, bool depthWrite, CullMode cull)
	{
		m_Blend = blend;
		for (bool &enabled : m_BlendEnabled) enabled = true;
		m_DepthFunc = depthFunc;
		m_DepthTest = depthTest;
		m_DepthWrite = depthWrite;
		m_Cull = cull;
		InvalidatePipeline();
	}

	void VulkanCommandList::SetBlendMode(BlendMode mode)
	{
		m_Blend = mode;
		for (bool &enabled : m_BlendEnabled) enabled = true;
		InvalidatePipeline();
	}

	void VulkanCommandList::SetDepthState(CompareMode func, bool test, bool write)
	{
		m_DepthFunc = func;
		m_DepthTest = test;
		m_DepthWrite = write;
		InvalidatePipeline();
	}

	void VulkanCommandList::SetCullMode(CullMode mode) { m_Cull = mode; InvalidatePipeline(); }
	void VulkanCommandList::SetColorWriteEnabled(bool enabled) { m_ColorWrite = enabled; InvalidatePipeline(); }
	void VulkanCommandList::SetBlendEnabled(unsigned int attachment, bool enabled)
	{
		if (attachment < 4) { m_BlendEnabled[attachment] = enabled; InvalidatePipeline(); }
	}

	void VulkanCommandList::PushRenderTarget()
	{
		m_TargetStack.push_back({ m_Targets, m_Backbuffer, m_Viewport, m_TargetWidth, m_TargetHeight });
	}

	void VulkanCommandList::PopRenderTarget()
	{
		if (m_TargetStack.empty())
			return;
		EndRendering();
		const TargetState state = m_TargetStack.back();
		m_TargetStack.pop_back();
		m_Targets = state.Targets;
		m_Backbuffer = state.Backbuffer;
		m_TargetWidth = state.Width;
		m_TargetHeight = state.Height;
		m_Viewport = state.Viewport;
		SetTargetFormats();
		InvalidatePipeline();
		BeginRendering(ClearMode::NONE, Color(0, 0, 0, 1));
	}

	void VulkanCommandList::SetPrimitiveMode(RHIPrimitiveMode mode) { m_Primitive = mode; InvalidatePipeline(); }
	void VulkanCommandList::SetFillMode(RHIFillMode mode) { m_Fill = mode; InvalidatePipeline(); }
	void VulkanCommandList::SetDepthBias(float factor, float units)
	{
		m_DepthBiasFactor = factor;
		m_DepthBiasUnits = units;
		if (m_Rendering)
			vkCmdSetDepthBias(m_Device->GetCommandBuffer(), units, 0.0f, factor);
	}

	void VulkanCommandList::SetVertexBuffer(const RHIBufferRef &buffer, unsigned int stream)
	{
		if (stream < MAX_STREAMS)
		{
			m_VertexStreams[stream] = std::static_pointer_cast<VulkanBuffer>(buffer);
			Retain(buffer);
		}
	}

	void VulkanCommandList::SetIndexBuffer(const RHIBufferRef &buffer)
	{
		m_IndexBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
		Retain(buffer);
	}

	void VulkanCommandList::SetMeshGeometry(const RHIMeshGeometryDesc &desc)
	{
		m_MeshChannelCount = std::min(desc.ChannelCount, RHIMeshGeometryDesc::MAX_CHANNELS);
		for (int i = 0; i < m_MeshChannelCount; ++i)
		{
			m_MeshChannels[i] = desc.Channels[i];
			Retain(desc.Channels[i].Buffer);
		}
		m_MeshName = desc.MeshName;
		InvalidatePipeline();
	}

	void VulkanCommandList::SetInstanceMatrixStream(const RHIBufferRef &buffer, const char *const attrNames[4])
	{
		m_InstanceBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
		m_HasInstanceStream = buffer != nullptr;
		if (attrNames != nullptr)
			for (int i = 0; i < 4; ++i) m_InstanceAttrNames[i] = attrNames[i] != nullptr ? attrNames[i] : "";
		Retain(buffer);
		InvalidatePipeline();
	}

	void VulkanCommandList::SetStorageBuffer(unsigned int binding, const RHIBufferRef &buffer)
	{
		if (buffer == nullptr) m_StorageBuffers.erase(binding);
		else m_StorageBuffers[binding] = std::static_pointer_cast<VulkanBuffer>(buffer);
		Retain(buffer);
	}

	void VulkanCommandList::SetStorageBufferNamed(const char *name, unsigned int binding, const RHIBufferRef &buffer)
	{
		if (name == nullptr) { SetStorageBuffer(binding, buffer); return; }
		if (buffer == nullptr)
		{
			m_NamedStorageBuffers.erase(name);
			m_StorageBuffers.erase(binding);
		}
		else
		{
			m_NamedStorageBuffers[name] = std::static_pointer_cast<VulkanBuffer>(buffer);
			m_StorageBuffers[binding] = std::static_pointer_cast<VulkanBuffer>(buffer);
		}
		Retain(buffer);
	}

	RHIUniformHandle VulkanCommandList::ResolveUniform(const std::string &name)
	{
		return m_CurrentModule != nullptr ? m_CurrentModule->ResolveUniform(name) : -1;
	}

	void VulkanCommandList::WriteShadow(const std::string &name, const void *data, size_t size)
	{
		if (m_CurrentModule == nullptr)
			return;
		for (int stage = 0; stage < 2; ++stage)
		{
			const auto &globals = m_CurrentModule->GetStageGlobals(stage);
			const long index = globals.FindMember(name);
			if (index < 0)
				continue;
			const auto &member = globals.members[index];
			const size_t bytes = std::min(size, member.size);
			if (member.offset + bytes <= m_GlobalsShadow[stage].size())
				std::memcpy(m_GlobalsShadow[stage].data() + member.offset, data, bytes);
		}
	}

	void VulkanCommandList::WriteShadowAt(RHIUniformHandle handle, const void *data, size_t size)
	{
		int stage = 0, index = -1, kind = -1;
		DecodeHandle(handle, stage, index, kind);
		if (index < 0 || m_CurrentModule == nullptr)
			return;
		if (kind == 3)
		{
			WriteShadow(m_CurrentModule->GetHandleName(index), data, size);
			return;
		}
		if (kind != 2)
			return;
		const auto &member = m_CurrentModule->GetMember(index);
		for (int s = 0; s < 2; ++s)
		{
			if (member.offset[s] < 0)
				continue;
			const size_t bytes = std::min(size, member.size);
			if (static_cast<size_t>(member.offset[s]) + bytes <= m_GlobalsShadow[s].size())
				std::memcpy(m_GlobalsShadow[s].data() + member.offset[s], data, bytes);
		}
	}

	void VulkanCommandList::SetUniformFloat(const std::string &name, float x) { WriteShadow(name, &x, 4); }
	void VulkanCommandList::SetUniformFloat(const std::string &name, float x, float y) { const float v[2] = {x,y}; WriteShadow(name, v, 8); }
	void VulkanCommandList::SetUniformFloat(const std::string &name, float x, float y, float z) { const float v[3] = {x,y,z}; WriteShadow(name, v, 12); }
	void VulkanCommandList::SetUniformFloat(const std::string &name, float x, float y, float z, float w) { const float v[4] = {x,y,z,w}; WriteShadow(name, v, 16); }
	void VulkanCommandList::SetUniformFloatArray(const std::string &name, int size, int count, const float *value) { WriteShadow(name, value, static_cast<size_t>(size) * count * 4); }
	void VulkanCommandList::SetUniformInt(const std::string &name, int x) { WriteShadow(name, &x, 4); }
	void VulkanCommandList::SetUniformInt(const std::string &name, int x, int y) { const int v[2] = {x,y}; WriteShadow(name, v, 8); }
	void VulkanCommandList::SetUniformInt(const std::string &name, int x, int y, int z) { const int v[3] = {x,y,z}; WriteShadow(name, v, 12); }
	void VulkanCommandList::SetUniformInt(const std::string &name, int x, int y, int z, int w) { const int v[4] = {x,y,z,w}; WriteShadow(name, v, 16); }
	void VulkanCommandList::SetUniformIntArray(const std::string &name, int size, int count, const int *value) { WriteShadow(name, value, static_cast<size_t>(size) * count * 4); }
	void VulkanCommandList::SetUniformUInt(const std::string &name, unsigned int x) { WriteShadow(name, &x, 4); }
	void VulkanCommandList::SetUniformMatrix(const std::string &name, const Matrix4 &matrix) { WriteShadow(name, matrix.Raw, 64); }
	void VulkanCommandList::SetUniformMatrix(const std::string &name, const float *raw) { WriteShadow(name, raw, 64); }
	void VulkanCommandList::SetUniformMatrices(const std::string &name, int count, const float *raw) { WriteShadow(name, raw, static_cast<size_t>(count) * 64); }
	void VulkanCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x) { WriteShadowAt(handle, &x, 4); }
	void VulkanCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y) { const float v[2] = {x,y}; WriteShadowAt(handle, v, 8); }
	void VulkanCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z) { const float v[3] = {x,y,z}; WriteShadowAt(handle, v, 12); }
	void VulkanCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z, float w) { const float v[4] = {x,y,z,w}; WriteShadowAt(handle, v, 16); }
	void VulkanCommandList::SetUniformIntAt(RHIUniformHandle handle, int x) { WriteShadowAt(handle, &x, 4); }
	void VulkanCommandList::SetUniformMatrixAt(RHIUniformHandle handle, const float *raw) { WriteShadowAt(handle, raw, 64); }
	void VulkanCommandList::SetUniformMatricesAt(RHIUniformHandle handle, int count, const float *raw) { WriteShadowAt(handle, raw, static_cast<size_t>(count) * 64); }

	void VulkanCommandList::SetTexture(const std::string &name, const RHITextureRef &texture)
	{
		m_Textures[name] = std::static_pointer_cast<VulkanTexture>(texture);
		Retain(texture);
	}

	int VulkanCommandList::SetTextureAt(RHIUniformHandle handle, const RHITextureRef &texture)
	{
		int stage = 0, index = -1, kind = -1;
		DecodeHandle(handle, stage, index, kind);
		if (kind == 3 && index >= 0 && m_CurrentModule != nullptr)
			SetTexture(m_CurrentModule->GetHandleName(index), texture);
		++m_TextureCursor;
		return m_TextureCursor;
	}

	void VulkanCommandList::SetTextureUnitCursor(int unitOffset) { m_TextureCursor = unitOffset; }

	void VulkanCommandList::BindTextureUnit(const RHITextureRef &texture)
	{
		if (m_CurrentModule != nullptr)
		{
			for (size_t i = 0; i < m_CurrentModule->ResourceCount(); ++i)
			{
				const auto &resource = m_CurrentModule->GetResource(i);
				if (resource.type == VulkanShaderResourceType::SampledImage && resource.binding == m_TextureCursor)
					m_Textures[resource.name] = std::static_pointer_cast<VulkanTexture>(texture);
			}
		}
		Retain(texture);
		++m_TextureCursor;
	}

	RHIVertexFormat VulkanCommandList::MeshChannelFormat(int components, bool integer)
	{
		if (integer) return RHIVertexFormat::INT4;
		switch (components)
		{
		case 1: return RHIVertexFormat::FLOAT;
		case 2: return RHIVertexFormat::FLOAT2;
		case 4: return RHIVertexFormat::FLOAT4;
		default: return RHIVertexFormat::FLOAT3;
		}
	}

	void VulkanCommandList::BuildMeshLayout(std::vector<RHIVertexAttribute> &out)
	{
		out.clear();
		for (int i = 0; i < m_MeshChannelCount; ++i)
		{
			const auto &channel = m_MeshChannels[i];
			if (channel.Attrib < 0)
				continue;
			RHIVertexAttribute attr;
			attr.Name = channel.Name;
			attr.Format = MeshChannelFormat(channel.Components, channel.Integer);
			attr.Location = channel.Attrib;
			if (!channel.Dirty && channel.Buffer != nullptr)
			{
				attr.Stream = MESH_STREAM_BASE + i;
				attr.Stride = channel.Components * 4;
			}
			else if (channel.Empty)
			{
				attr.Stream = CONSTANTS_STREAM;
				attr.ConstantFill = true;
			}
			else
				continue;
			out.push_back(attr);
		}
		if (m_HasInstanceStream && m_InstanceBuffer != nullptr && m_CurrentModule != nullptr)
		{
			for (int row = 0; row < 4; ++row)
			{
				const int location = m_CurrentModule->ResolveAttribLocation(m_InstanceAttrNames[row]);
				if (location < 0)
					continue;
				RHIVertexAttribute attr;
				attr.Name = m_InstanceAttrNames[row];
				attr.Format = RHIVertexFormat::FLOAT4;
				attr.Location = location;
				attr.Offset = row * 16;
				attr.Stream = INSTANCE_STREAM;
				attr.Stride = 64;
				attr.Divisor = 1;
				out.push_back(attr);
			}
		}
	}

	const std::vector<RHIVertexAttribute> *VulkanCommandList::CurrentVertexLayout()
	{
		if (m_MeshChannelCount > 0 || m_HasInstanceStream)
		{
			BuildMeshLayout(m_MeshLayout);
			return &m_MeshLayout;
		}
		return m_CurrentPipeline != nullptr ? &m_CurrentPipeline->GetDesc().VertexAttributes : nullptr;
	}

	uint32_t VulkanCommandList::HashVertexLayout(const std::vector<RHIVertexAttribute> &attributes)
	{
		uint32_t hash = 2166136261u;
		for (const auto &attr : attributes)
		{
			for (uint32_t value : { static_cast<uint32_t>(attr.Format), attr.Offset, attr.Stride, attr.Divisor,
				static_cast<uint32_t>(attr.Location), attr.Stream, static_cast<uint32_t>(attr.ConstantFill) })
				hash = (hash ^ value) * 16777619u;
		}
		return hash;
	}

	bool VulkanCommandList::EnsurePipeline()
	{
		if (m_NativePipeline != nullptr)
			return true;
		if (m_CurrentModule == nullptr || !m_CurrentModule->IsValid())
			return false;
		const auto *vertexLayout = CurrentVertexLayout();
		PipelineKey key;
		key.Module = m_CurrentModule.get();
		key.Blend = m_Blend;
		key.DepthFunc = m_DepthFunc;
		key.DepthTest = m_DepthTest;
		key.DepthWrite = m_DepthWrite;
		key.Cull = m_Cull;
		key.Primitive = m_Primitive;
		key.Fill = m_Fill;
		key.ColorWrite = m_ColorWrite;
		for (int i = 0; i < 4; ++i)
		{
			key.BlendEnabled[i] = m_BlendEnabled[i];
			key.ColorFormats[i] = static_cast<uint32_t>(m_ColorFormats[i]);
		}
		key.DepthFormat = static_cast<uint32_t>(m_DepthFormat);
		key.VertexHash = vertexLayout != nullptr ? HashVertexLayout(*vertexLayout) : 0;
		auto existing = m_Pipelines.find(key);
		if (existing != m_Pipelines.end())
		{
			m_NativePipeline = &existing->second;
			return true;
		}

		PipelineEntry entry;
		entry.Module = m_CurrentModule;
		VkDevice device = m_Device->GetDevice();
		for (unsigned int set = 0; set < 2; ++set)
		{
			std::vector<VkDescriptorSetLayoutBinding> bindings;
			for (size_t i = 0; i < m_CurrentModule->ResourceCount(); ++i)
			{
				const auto &resource = m_CurrentModule->GetResource(i);
				if (resource.set != set)
					continue;
				VkDescriptorSetLayoutBinding binding = {};
				binding.binding = resource.binding;
				binding.descriptorType = DescriptorType(resource.type);
				binding.descriptorCount = std::max(1u, resource.arraySize);
				binding.stageFlags = resource.stages;
				bindings.push_back(binding);
			}
			VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			layoutInfo.bindingCount = static_cast<unsigned int>(bindings.size());
			layoutInfo.pBindings = bindings.data();
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &entry.SetLayouts[set]) != VK_SUCCESS)
				return false;
		}
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.setLayoutCount = 2;
		pipelineLayoutInfo.pSetLayouts = entry.SetLayouts;
		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &entry.Layout) != VK_SUCCESS)
			return false;

		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;
		if (vertexLayout != nullptr)
		{
			for (const auto &attr : *vertexLayout)
			{
				auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto &binding) { return binding.binding == attr.Stream; });
				if (found == bindings.end())
				{
					VkVertexInputBindingDescription binding = {};
					binding.binding = attr.Stream;
					binding.stride = attr.Stride;
					binding.inputRate = attr.Divisor > 0 ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
					bindings.push_back(binding);
				}
				VkVertexInputAttributeDescription out = {};
				out.location = attr.Location;
				out.binding = attr.Stream;
				out.format = VulkanEnumMap::VertexFormat(attr.Format);
				out.offset = attr.Offset;
				attributes.push_back(out);
			}
		}
		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = m_CurrentModule->GetVertexModule();
		stages[0].pName = "main";
		stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = m_CurrentModule->GetFragmentModule();
		stages[1].pName = "main";
		VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInput.vertexBindingDescriptionCount = static_cast<unsigned int>(bindings.size());
		vertexInput.pVertexBindingDescriptions = bindings.data();
		vertexInput.vertexAttributeDescriptionCount = static_cast<unsigned int>(attributes.size());
		vertexInput.pVertexAttributeDescriptions = attributes.data();
		VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		inputAssembly.topology = VulkanEnumMap::Primitive(m_Primitive);
		VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = viewport.scissorCount = 1;
		VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		raster.polygonMode = VulkanEnumMap::Fill(m_Fill);
		raster.cullMode = VulkanEnumMap::Cull(m_Cull);
		raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster.depthBiasEnable = VK_TRUE;
		raster.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depth.depthTestEnable = m_DepthTest && m_DepthFormat != VK_FORMAT_UNDEFINED;
		depth.depthWriteEnable = m_DepthTest && m_DepthWrite && m_DepthFormat != VK_FORMAT_UNDEFINED;
		depth.depthCompareOp = m_DepthTest ? VulkanEnumMap::Compare(m_DepthFunc) : VK_COMPARE_OP_ALWAYS;
		unsigned int colorCount = m_Backbuffer ? 1 : (m_Targets != nullptr ? m_Targets->GetColorCount() : 0);
		std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(colorCount);
		VkFormat formats[4] = {};
		for (unsigned int i = 0; i < colorCount; ++i)
		{
			formats[i] = m_ColorFormats[i];
			blendAttachments[i].blendEnable = VulkanEnumMap::BlendEnabled(m_Blend) && m_BlendEnabled[i];
			blendAttachments[i].srcColorBlendFactor = VulkanEnumMap::BlendModeSrc(m_Blend);
			blendAttachments[i].dstColorBlendFactor = VulkanEnumMap::BlendModeDest(m_Blend);
			blendAttachments[i].colorBlendOp = VulkanEnumMap::BlendModeOp(m_Blend);
			blendAttachments[i].srcAlphaBlendFactor = VulkanEnumMap::BlendModeSrc(m_Blend);
			blendAttachments[i].dstAlphaBlendFactor = VulkanEnumMap::BlendModeDest(m_Blend);
			blendAttachments[i].alphaBlendOp = VulkanEnumMap::BlendModeOp(m_Blend);
			blendAttachments[i].colorWriteMask = m_ColorWrite ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0;
		}
		VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = colorCount;
		blend.pAttachments = blendAttachments.data();
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_FRONT_FACE };
		VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamic.dynamicStateCount = 4;
		dynamic.pDynamicStates = dynamicStates;
		VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		rendering.colorAttachmentCount = colorCount;
		rendering.pColorAttachmentFormats = formats;
		rendering.depthAttachmentFormat = m_DepthFormat;
		rendering.stencilAttachmentFormat = (m_DepthFormat == VK_FORMAT_D24_UNORM_S8_UINT || m_DepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) ? m_DepthFormat : VK_FORMAT_UNDEFINED;
		VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		info.pNext = &rendering;
		info.stageCount = 2;
		info.pStages = stages;
		info.pVertexInputState = &vertexInput;
		info.pInputAssemblyState = &inputAssembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState = &multisample;
		info.pDepthStencilState = m_DepthFormat != VK_FORMAT_UNDEFINED ? &depth : nullptr;
		info.pColorBlendState = &blend;
		info.pDynamicState = &dynamic;
		info.layout = entry.Layout;
		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &entry.Pipeline) != VK_SUCCESS)
		{
			FURYE << "Vulkan pipeline creation failed: " << m_CurrentModule->GetName();
			vkDestroyPipelineLayout(device, entry.Layout, nullptr);
			for (auto layout : entry.SetLayouts) vkDestroyDescriptorSetLayout(device, layout, nullptr);
			return false;
		}
		auto inserted = m_Pipelines.emplace(key, std::move(entry));
		m_NativePipeline = &inserted.first->second;
		return true;
	}

	VulkanBuffer *VulkanCommandList::GetConstantsBuffer()
	{
		if (m_ConstantsBuffer == nullptr)
		{
			static const float values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			RHIBufferDesc desc;
			desc.Name = "Vulkan vertex constants";
			desc.Type = RHIBufferType::VERTEX;
			desc.Size = sizeof(values);
			m_ConstantsBuffer = VulkanBuffer::Create(m_Device->GetDevice(), m_Device->GetAllocator(), desc, values);
		}
		return m_ConstantsBuffer.get();
	}

	bool VulkanCommandList::FlushDrawState()
	{
		if (!BeginRendering(ClearMode::NONE, Color(0, 0, 0, 1)) || !EnsurePipeline() || !m_Rendering)
		{
			static unsigned int rejectionLogs = 0;
			if (rejectionLogs++ < 8)
				FURYE << "Vulkan draw rejected: pipeline=" << (m_NativePipeline != nullptr) << " rendering=" << m_Rendering;
			return false;
		}
		VkCommandBuffer cmd = m_Device->GetCommandBuffer();
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_NativePipeline->Pipeline);
		vkCmdSetFrontFace(cmd, m_Backbuffer ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
		for (int stage = 0; stage < 2; ++stage)
			if (m_GlobalsBuffers[stage] != nullptr && !m_GlobalsShadow[stage].empty())
				m_GlobalsBuffers[stage]->Update(m_GlobalsShadow[stage].data(), m_GlobalsShadow[stage].size(), 0);

		VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
		VkDescriptorSetAllocateInfo allocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocate.descriptorPool = m_DescriptorPool;
		allocate.descriptorSetCount = 2;
		allocate.pSetLayouts = m_NativePipeline->SetLayouts;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocate, sets) != VK_SUCCESS)
		{
			FURYE << "Vulkan descriptor set allocation failed";
			return false;
		}
		for (size_t i = 0; i < m_CurrentModule->ResourceCount(); ++i)
		{
			const auto &resource = m_CurrentModule->GetResource(i);
			if (resource.set > 1)
				continue;
			VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = sets[resource.set];
			write.dstBinding = resource.binding;
			write.descriptorType = DescriptorType(resource.type);
			write.descriptorCount = std::max(1u, resource.arraySize);
			VkDescriptorBufferInfo bufferInfo = {};
			std::vector<VkDescriptorImageInfo> images;
			if (resource.type == VulkanShaderResourceType::SampledImage)
			{
				auto found = m_Textures.find(resource.name);
				if (found == m_Textures.end() || found->second == nullptr)
					continue;
				images.resize(write.descriptorCount);
				for (auto &image : images)
				{
					image.sampler = found->second->GetSampler();
					image.imageView = found->second->GetView();
					image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				}
				write.pImageInfo = images.data();
			}
			else
			{
				VulkanBufferRef buffer;
				if (resource.type == VulkanShaderResourceType::UniformBuffer)
					buffer = m_GlobalsBuffers[resource.set];
				else
				{
					auto named = m_NamedStorageBuffers.find(resource.name);
					if (named != m_NamedStorageBuffers.end()) buffer = named->second;
					else
					{
						auto numeric = m_StorageBuffers.find(resource.binding);
						if (numeric != m_StorageBuffers.end()) buffer = numeric->second;
					}
				}
				if (buffer == nullptr)
				{
					if (resource.type == VulkanShaderResourceType::StorageBuffer)
					{
						static unsigned int missingStorageLogs = 0;
						if (missingStorageLogs++ < 8)
							FURYE << "Vulkan storage buffer is unbound: " << resource.name;
					}
					continue;
				}
				bufferInfo.buffer = buffer->Get();
				bufferInfo.offset = 0;
				bufferInfo.range = buffer->GetDesc().Size;
				write.pBufferInfo = &bufferInfo;
			}
			vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
		}
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_NativePipeline->Layout, 0, 2, sets, 0, nullptr);

		const auto *layout = CurrentVertexLayout();
		if (layout != nullptr)
		{
			for (const auto &attr : *layout)
			{
				VulkanBuffer *buffer = nullptr;
				if (attr.Stream < MAX_STREAMS) buffer = m_VertexStreams[attr.Stream].get();
				else if (attr.Stream >= MESH_STREAM_BASE && attr.Stream < MESH_STREAM_BASE + m_MeshChannelCount) buffer = static_cast<VulkanBuffer *>(m_MeshChannels[attr.Stream - MESH_STREAM_BASE].Buffer.get());
				else if (attr.Stream == INSTANCE_STREAM) buffer = m_InstanceBuffer.get();
				else if (attr.Stream == CONSTANTS_STREAM) buffer = GetConstantsBuffer();
				if (buffer == nullptr)
					continue;
				const VkDeviceSize offset = 0;
				const VkBuffer native = buffer->Get();
				vkCmdBindVertexBuffers(cmd, attr.Stream, 1, &native, &offset);
			}
		}
		return true;
	}

	void VulkanCommandList::Draw(unsigned int vertexCount, unsigned int firstVertex)
	{
		if (FlushDrawState()) vkCmdDraw(m_Device->GetCommandBuffer(), vertexCount, 1, firstVertex, 0);
	}

	void VulkanCommandList::DrawIndexed(unsigned int indexCount, unsigned int firstIndex, int vertexOffset)
	{
		DrawIndexedInstanced(indexCount, 1, firstIndex, vertexOffset);
	}

	void VulkanCommandList::DrawInstanced(unsigned int vertexCount, unsigned int instanceCount, unsigned int firstVertex)
	{
		if (FlushDrawState()) vkCmdDraw(m_Device->GetCommandBuffer(), vertexCount, instanceCount, firstVertex, 0);
	}

	void VulkanCommandList::DrawIndexedInstanced(unsigned int indexCount, unsigned int instanceCount, unsigned int firstIndex, int vertexOffset)
	{
		if (m_IndexBuffer == nullptr || !FlushDrawState())
			return;
		const VkIndexType type = m_IndexBuffer->GetDesc().Index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
		vkCmdBindIndexBuffer(m_Device->GetCommandBuffer(), m_IndexBuffer->Get(), 0, type);
		vkCmdDrawIndexed(m_Device->GetCommandBuffer(), indexCount, instanceCount, firstIndex, vertexOffset, 0);
	}

	void VulkanCommandList::Dispatch(unsigned int, unsigned int, unsigned int)
	{
		FURYW << "Vulkan compute dispatch requires a bound compute pipeline";
	}

	void VulkanCommandList::Barrier()
	{
		EndRendering();
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(m_Device->GetCommandBuffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	void VulkanCommandList::Blit(const RHITextureRef &src, const RHITextureRef &dst, int srcWidth, int srcHeight, int dstWidth, int dstHeight, bool linearFilter)
	{
		if (src == nullptr || dst == nullptr)
			return;
		EndRendering();
		Retain(src);
		Retain(dst);
		VulkanTexture *source = static_cast<VulkanTexture *>(src.get());
		VulkanTexture *target = static_cast<VulkanTexture *>(dst.get());
		VkImageBlit region = {};
		region.srcSubresource = { source->GetAspectMask(), 0, 0, 1 };
		region.dstSubresource = { target->GetAspectMask(), 0, 0, 1 };
		region.srcOffsets[1] = { srcWidth, srcHeight, 1 };
		region.dstOffsets[1] = { dstWidth, dstHeight, 1 };
		VkFilter filter = linearFilter && region.srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		vkCmdBlitImage(m_Device->GetCommandBuffer(), source->Get(), VK_IMAGE_LAYOUT_GENERAL, target->Get(), VK_IMAGE_LAYOUT_GENERAL,
			1, &region, filter);
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(m_Device->GetCommandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 1, &barrier, 0, nullptr, 0, nullptr);
	}
}

#endif // FURY_RHI_VULKAN
