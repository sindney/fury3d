#include "Fury/RHI/Metal/MetalCommandList.h"

#if FURY_RHI_METAL

#include "ImGui/backends/imgui_impl_metal.h"

#include <algorithm>
#include <cstring>

#include "Fury/Log.h"
#include "Fury/RHI/Metal/MetalEnumMap.h"
#include "Fury/RHI/Metal/RHIMetalDevice.h"

namespace fury
{
	namespace
	{
		MTL::LoadAction ColorLoadAction(bool clear)
		{
			return clear ? MTL::LoadActionClear : MTL::LoadActionLoad;
		}

		MTL::ClearColor ToClearColor(const Color &color)
		{
			return MTL::ClearColor::Make(color.r, color.g, color.b, color.a);
		}

		// Binds one attachment texture to a render-pass attachment point:
		// 3D slices go through depthPlane, array layers / cube faces through slice.
		void SetAttachmentTexture(MTL::RenderPassAttachmentDescriptor *att, MetalTexture *tex, unsigned int level, unsigned int slice)
		{
			att->setTexture(tex->Get());
			att->setLevel(level);
			if (tex->GetDesc().Type == TextureType::TEXTURE_3D)
				att->setDepthPlane(slice);
			else
				att->setSlice(slice);
		}
	}

	namespace
	{
	}

	uint64_t MetalCommandList::ShadowKey(int stage, int index)
	{
		return (static_cast<uint64_t>(stage) << 32) | static_cast<uint32_t>(index);
	}

	int MetalCommandList::EncodeHandle(int stage, int index, int kind)
	{
		return ((index + 1) & 0xFFFFFF) | (stage << 24) | (kind << 26);
	}

	void MetalCommandList::DecodeHandle(RHIUniformHandle handle, int &stage, int &index, int &kind)
	{
		// Valid handles encode payload+1, so they are always > 0; a miss
		// (-1) must not decode into a wild index.
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

	void MetalCommandList::OnBeginFrame()
	{
		m_Encoder = nullptr;
		m_Targets = nullptr;
		m_TargetStack.clear();
		m_PSO = nullptr;
		m_PSOValid = false;
	}

	void MetalCommandList::OnEndFrame()
	{
		EndEncoder();
		m_Targets = nullptr;
		m_TargetStack.clear();
		// Bind state (textures, uniforms, mesh channels, streams) persists
		// across frames like GL's global state: the pipeline's draw cache
		// skips redundant re-binds, so dropping it here would orphan frame
		// 2+ draws.
	}

	void MetalCommandList::EndEncoder()
	{
		if (m_Encoder != nullptr)
		{
			m_Encoder->endEncoding();
			m_Encoder = nullptr;
		}
	}

	void MetalCommandList::DrawUiOverlay(void *drawData, unsigned int width, unsigned int height)
	{
		if (drawData == nullptr || width == 0 || height == 0)
			return;

		// The world was composited into the drawable by an earlier pass;
		// the UI draws on top of it in a load-only pass over the same
		// drawable (the drawable is only presented at Present()).
		BeginBackbuffer(static_cast<int>(width), static_cast<int>(height), ClearMode::NONE, Color(0, 0, 0, 1));
		MTL::RenderCommandEncoder *encoder = EnsureEncoder();
		if (encoder == nullptr)
			return;

		ImDrawData *uiData = static_cast<ImDrawData *>(drawData);
		ImGui_ImplMetal_NewFrame(m_RenderPass.get());
		ImGui_ImplMetal_RenderDrawData(uiData,
			m_Device->GetCommandBuffer(), encoder);

		EndRenderTargets();
	}

	void MetalCommandList::ClearAllSlices(MetalTexture *tex, bool depth, const Color &color)
	{
		if (tex == nullptr || tex->Get() == nullptr)
			return;
		const auto &desc = tex->GetDesc();
		unsigned int slices = 1;
		if (desc.Type == TextureType::TEXTURE_CUBE_MAP)
			slices = 6;
		else if (desc.Type == TextureType::TEXTURE_2D_ARRAY || desc.Type == TextureType::TEXTURE_3D)
			slices = static_cast<unsigned int>(std::max(1, desc.Depth));
		if (slices <= 1)
			return;

		MTL::CommandBuffer *commandBuffer = m_Device->GetCommandBuffer();
		if (commandBuffer == nullptr)
			return;

		for (unsigned int i = 0; i < slices; ++i)
		{
			NS::SharedPtr<MTL::RenderPassDescriptor> pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
			if (depth)
			{
				auto *att = pass->depthAttachment();
				att->setTexture(tex->Get());
				att->setLevel(0);
				if (desc.Type == TextureType::TEXTURE_3D)
					att->setDepthPlane(i);
				else
					att->setSlice(i);
				att->setLoadAction(MTL::LoadActionClear);
				att->setClearDepth(1.0);
				att->setStoreAction(MTL::StoreActionStore);
			}
			else
			{
				auto *att = pass->colorAttachments()->object(0);
				att->setTexture(tex->Get());
				att->setLevel(0);
				if (desc.Type == TextureType::TEXTURE_3D)
					att->setDepthPlane(i);
				else
					att->setSlice(i);
				att->setLoadAction(MTL::LoadActionClear);
				att->setClearColor(ToClearColor(color));
				att->setStoreAction(MTL::StoreActionStore);
			}
			MTL::RenderCommandEncoder *clearEncoder = commandBuffer->renderCommandEncoder(pass.get());
			if (clearEncoder != nullptr)
				clearEncoder->endEncoding();
		}
	}

	MTL::RenderCommandEncoder *MetalCommandList::EnsureEncoder()
	{
		return EnsureEncoderWithClear(false, ClearMode::NONE, Color(0, 0, 0, 1), false);
	}

	MTL::RenderCommandEncoder *MetalCommandList::EnsureEncoderWithClear(bool useClear, ClearMode clear, const Color &clearColor, bool clearUInt)
	{
		if (m_Encoder != nullptr && !useClear)
		{
			bool slicesCurrent = true;
			if (m_Targets != nullptr)
			{
				auto stale = [&](const MetalRenderTargetSet::AttachmentState &state, size_t idx) {
					if (idx >= m_EncoderSlices.size())
						return true;
					unsigned int level = 0, slice = 0;
					MetalRenderTargetSet::ResolveSlice(state,
						static_cast<MetalTexture *>(state.Texture.get())->GetDesc().Type, level, slice);
					return m_EncoderSlices[idx].first != level || m_EncoderSlices[idx].second != slice;
				};
				size_t idx = 0;
				for (unsigned int i = 0; i < m_Targets->GetColorCount() && slicesCurrent; ++i)
					slicesCurrent = !stale(m_Targets->GetColorState(i), idx++);
				if (slicesCurrent && m_Targets->HasDepth())
					slicesCurrent = !stale(m_Targets->GetDepthState(), idx);
			}
			if (slicesCurrent)
				return m_Encoder;
		}
		EndEncoder();
		m_Device->EndBlitEncoder();

		MTL::CommandBuffer *commandBuffer = m_Device->GetCommandBuffer();
		if (commandBuffer == nullptr)
			return nullptr;

		NS::SharedPtr<MTL::RenderPassDescriptor> pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
		const bool clearColor0 = useClear && (clear == ClearMode::COLOR || clear == ClearMode::COLOR_DEPTH || clear == ClearMode::COLOR_STENCIL || clear == ClearMode::COLOR_DEPTH_STENCIL);
		const bool clearDepth = useClear && (clear == ClearMode::DEPTH || clear == ClearMode::COLOR_DEPTH || clear == ClearMode::STENCIL_DEPTH || clear == ClearMode::COLOR_DEPTH_STENCIL);

		// Layered attachments: a pass clear covers only the resolved slice, so
		// clear every slice up front (CSM arrays, 3D volumes, shadow cubes
		// keep undefined content otherwise). GL parity: bind-time clear means
		// the whole target.
		if (m_Targets != nullptr && (clearColor0 || clearDepth))
		{
			for (unsigned int i = 0; i < m_Targets->GetColorCount(); ++i)
			{
				const auto &state = m_Targets->GetColorState(i);
				if (clearColor0)
					ClearAllSlices(static_cast<MetalTexture *>(state.Texture.get()), false, clearColor);
			}
			if (m_Targets->HasDepth() && clearDepth)
			{
				const auto &state = m_Targets->GetDepthState();
				ClearAllSlices(static_cast<MetalTexture *>(state.Texture.get()), true, clearColor);
			}
		}

		if (m_Targets != nullptr)
		{
			for (unsigned int i = 0; i < m_Targets->GetColorCount(); ++i)
			{
				const auto &state = m_Targets->GetColorState(i);
				MetalTexture *tex = static_cast<MetalTexture *>(state.Texture.get());
				unsigned int level = 0, slice = 0;
				MetalRenderTargetSet::ResolveSlice(state, tex->GetDesc().Type, level, slice);
				auto *att = pass->colorAttachments()->object(i);
				SetAttachmentTexture(att, tex, level, slice);
				// clearUInt mirrors glClearBufferuiv(GL_COLOR, 0): attachment
				// 0 only.
				const bool clearThis = clearColor0 && (!clearUInt || i == 0);
				att->setLoadAction(ColorLoadAction(clearThis));
				if (clearThis)
					att->setClearColor(clearUInt ? MTL::ClearColor::Make(m_UIntClear, m_UIntClear, m_UIntClear, m_UIntClear) : ToClearColor(clearColor));
				att->setStoreAction(MTL::StoreActionStore);
			}
			if (m_Targets->HasDepth())
			{
				const auto &state = m_Targets->GetDepthState();
				MetalTexture *tex = static_cast<MetalTexture *>(state.Texture.get());
				unsigned int level = 0, slice = 0;
				MetalRenderTargetSet::ResolveSlice(state, tex->GetDesc().Type, level, slice);
				auto *att = pass->depthAttachment();
				SetAttachmentTexture(att, tex, level, slice);
				att->setLoadAction(clearDepth ? MTL::LoadActionClear : MTL::LoadActionLoad);
				att->setClearDepth(1.0);
				att->setStoreAction(MTL::StoreActionStore);
			}
		}
		else
		{
			// Backbuffer.
			CA::MetalDrawable *drawable = m_Device->GetDrawable();
			if (drawable == nullptr)
				return nullptr;
			auto *att = pass->colorAttachments()->object(0);
			att->setTexture(drawable->texture());
			att->setLoadAction(ColorLoadAction(clearColor0));
			if (clearColor0)
				att->setClearColor(ToClearColor(clearColor));
			att->setStoreAction(MTL::StoreActionStore);
		}

		m_Encoder = commandBuffer->renderCommandEncoder(pass.get());
		if (m_Encoder == nullptr)
		{
			FURYE << "MetalCommandList: renderCommandEncoder creation failed";
			return nullptr;
		}
		m_RenderPass = pass;
		m_EncoderSlices.clear();
		if (m_Targets != nullptr)
		{
			for (unsigned int i = 0; i < m_Targets->GetColorCount(); ++i)
			{
				unsigned int level = 0, slice = 0;
				const auto &state = m_Targets->GetColorState(i);
				MetalRenderTargetSet::ResolveSlice(state,
					static_cast<MetalTexture *>(state.Texture.get())->GetDesc().Type, level, slice);
				m_EncoderSlices.push_back({ level, slice });
			}
			if (m_Targets->HasDepth())
			{
				unsigned int level = 0, slice = 0;
				const auto &state = m_Targets->GetDepthState();
				MetalRenderTargetSet::ResolveSlice(state,
					static_cast<MetalTexture *>(state.Texture.get())->GetDesc().Type, level, slice);
				m_EncoderSlices.push_back({ level, slice });
			}
		}
		m_Encoder->setViewport(m_Viewport);
		if (m_ScissorEnabled)
			m_Encoder->setScissorRect(m_Scissor);
		if (m_DepthBiasFactor != 0.0f || m_DepthBiasUnits != 0.0f)
			m_Encoder->setDepthBias(m_DepthBiasUnits, m_DepthBiasFactor, 0.0f);
		return m_Encoder;
	}

	void MetalCommandList::BeginRenderTargets(const RHIRenderTargetSetRef &targets, ClearMode clear, const Color &clearColor)
	{
		if (targets != nullptr)
		{
			for (unsigned int i = 0; i < targets->GetColorCount(); ++i)
				m_Device->TrackRenderTarget(targets->GetColorTexture(i));
			if (targets->GetDepthTexture() != nullptr)
				m_Device->TrackRenderTarget(targets->GetDepthTexture());
		}
		EndEncoder();
		m_Targets = std::static_pointer_cast<MetalRenderTargetSet>(targets);
		m_NativeYViewport = false;
		m_TargetWidth = m_Targets->GetWidth();
		m_TargetHeight = m_Targets->GetHeight();
		for (unsigned int i = 0; i < 4; ++i)
			m_ColorFormats[i] = i < m_Targets->GetColorCount()
				? MetalEnumMap::PixelFormat(m_Targets->GetColorTexture(i)->GetDesc().Format)
				: MTL::PixelFormatInvalid;
		m_DepthFormat = m_Targets->HasDepth()
			? MetalEnumMap::PixelFormat(m_Targets->GetDepthTexture()->GetDesc().Format)
			: MTL::PixelFormatInvalid;
		SetViewport(0, 0, m_TargetWidth, m_TargetHeight);
		InvalidateBindings();
		EnsureEncoderWithClear(clear != ClearMode::NONE, clear, clearColor, false);
	}

	void MetalCommandList::BeginBackbuffer(int width, int height, ClearMode clear, const Color &clearColor)
	{
		EndEncoder();
		m_Device->SetBackbufferSize(static_cast<unsigned int>(width), static_cast<unsigned int>(height));
		m_Targets = nullptr;
		m_NativeYViewport = true;
		m_TargetWidth = width;
		m_TargetHeight = height;
		m_ColorFormats[0] = MTL::PixelFormatBGRA8Unorm;
		for (int i = 1; i < 4; ++i)
			m_ColorFormats[i] = MTL::PixelFormatInvalid;
		m_DepthFormat = MTL::PixelFormatInvalid;
		SetViewport(0, 0, width, height);
		InvalidateBindings();
		EnsureEncoderWithClear(clear != ClearMode::NONE, clear, clearColor, false);
	}

	void MetalCommandList::EndRenderTargets()
	{
		EndEncoder();
		// GL parity: GLCommandList rebinds FBO 0 here, so engine code that
		// saves/restores targets around this point expects the backbuffer to
		// be current (a stale offscreen set would capture into Push/Pop).
		m_Targets = nullptr;
		m_ColorFormats[0] = MTL::PixelFormatBGRA8Unorm;
		for (int i = 1; i < 4; ++i)
			m_ColorFormats[i] = MTL::PixelFormatInvalid;
		m_DepthFormat = MTL::PixelFormatInvalid;
		InvalidateBindings();
	}

	void MetalCommandList::SetViewport(int x, int y, int width, int height)
	{
		if (m_NativeYViewport)
		{
			// Backbuffer: Metal's native transform (GL y-up rect converted to
			// memory rows); NDC-up lands at the display top.
			const int mtlY = m_TargetHeight - (y + height);
			m_Viewport = { static_cast<double>(x), static_cast<double>(mtlY),
				static_cast<double>(width), static_cast<double>(height), 0.0, 1.0 };
		}
		else
		{
			// Offscreen: negative viewport height. Metal's native transform
			// maps NDC-up to memory row 0 (y-flipped vs GL, probe-verified);
			// flipping the height makes the framebuffer layout match GL's
			// readback order, so render-target sampling agrees with GL.
			m_Viewport = { static_cast<double>(x), static_cast<double>(y + height),
				static_cast<double>(width), static_cast<double>(-height), 0.0, 1.0 };
		}
		if (m_Encoder != nullptr)
			m_Encoder->setViewport(m_Viewport);
	}

	void MetalCommandList::SetScissor(bool enabled, int x, int y, int width, int height)
	{
		m_ScissorEnabled = enabled;
		if (enabled)
		{
			// Clamp to the target. Offscreen targets use GL bottom-up coords
			// directly (negative viewport height); the backbuffer's native
			// orientation takes the y-flip into memory rows.
			x = std::max(0, x);
			width = std::max(0, std::min(width, m_TargetWidth - x));
			int scissorY = y;
			if (m_NativeYViewport)
				scissorY = m_TargetHeight - (y + height);
			scissorY = std::max(0, scissorY);
			height = std::max(0, std::min(height, m_TargetHeight - scissorY));
			m_Scissor = { static_cast<NS::UInteger>(x), static_cast<NS::UInteger>(scissorY),
				static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height) };
		}
		if (m_Encoder != nullptr)
		{
			if (enabled)
				m_Encoder->setScissorRect(m_Scissor);
			else
				m_Encoder->setScissorRect({ 0, 0, static_cast<NS::UInteger>(m_TargetWidth), static_cast<NS::UInteger>(m_TargetHeight) });
		}
	}

	void MetalCommandList::Clear(ClearMode mode, const Color &color)
	{
		EnsureEncoderWithClear(true, mode, color, false);
	}

	void MetalCommandList::ClearColorUInt(unsigned int value)
	{
		m_UIntClear = static_cast<double>(value);
		EnsureEncoderWithClear(true, ClearMode::COLOR, Color(0, 0, 0, 1), true);
	}

	void MetalCommandList::SetPipeline(const RHIGraphicsPipelineRef &pipeline)
	{
		m_CurrentPipeline = pipeline;
		AdoptModule(pipeline != nullptr
			? std::static_pointer_cast<MetalShaderModule>(pipeline->GetShaders())
			: nullptr);
		if (pipeline != nullptr)
		{
			const auto &desc = pipeline->GetDesc();
			m_Blend = desc.Blend;
			m_DepthFunc = desc.DepthFunc;
			m_DepthTest = desc.DepthTest;
			m_DepthWrite = desc.DepthWrite;
			m_Cull = desc.Cull;
			m_Fill = desc.Fill;
			m_Primitive = desc.Primitive;
		}
		m_TextureCursor = 0;
		InvalidateBindings();
	}

	void MetalCommandList::BindShaderModule(const RHIShaderModuleRef &module)
	{
		m_CurrentPipeline = nullptr;
		AdoptModule(std::static_pointer_cast<MetalShaderModule>(module));
		m_TextureCursor = 0;
		InvalidateBindings();
	}

	void MetalCommandList::AdoptModule(const MetalShaderModuleRef &module)
	{
		if (m_CurrentModule == module)
			return;
		m_CurrentModule = module;
		m_UniformShadow.clear();
		m_PendingTextures.clear();
		for (int stage = 0; stage < 2; ++stage)
		{
			const MetalStageGlobals emptyGlobals;
			const MetalStageGlobals &globals = module != nullptr ? module->GetStageGlobals(stage) : emptyGlobals;
			m_GlobalsActive[stage] = globals.HasBlock();
			if (globals.HasBlock() && globals.defaultValues.size() == globals.blockSize)
				m_GlobalsShadow[stage] = globals.defaultValues;
			else
				m_GlobalsShadow[stage].assign(globals.HasBlock() ? globals.blockSize : 0, 0);
		}
	}

	void MetalCommandList::SetRasterState(BlendMode blend, CompareMode depthFunc, bool depthTest, bool depthWrite, CullMode cull)
	{
		m_Blend = blend;
		// GL parity: SetBlendMode re-enables blending wholesale (glEnable),
		// so a leaked per-attachment disable (ocean pass restore) never
		// sticks; passes re-apply their own opt-outs after this.
		for (int i = 0; i < 4; ++i)
			m_BlendEnabled[i] = true;
		m_DepthFunc = depthFunc;
		m_DepthTest = depthTest;
		m_DepthWrite = depthWrite;
		m_Cull = cull;
		InvalidateBindings();
	}

	void MetalCommandList::SetBlendMode(BlendMode mode)
	{
		m_Blend = mode;
		for (int i = 0; i < 4; ++i)
			m_BlendEnabled[i] = true;
		InvalidateBindings();
	}

	void MetalCommandList::SetDepthState(CompareMode func, bool test, bool write)
	{
		m_DepthFunc = func;
		m_DepthTest = test;
		m_DepthWrite = write;
	}

	void MetalCommandList::SetCullMode(CullMode mode)
	{
		m_Cull = mode;
	}

	void MetalCommandList::SetColorWriteEnabled(bool enabled)
	{
		m_ColorWrite = enabled;
		InvalidateBindings();
	}

	void MetalCommandList::SetBlendEnabled(unsigned int attachment, bool enabled)
	{
		if (attachment < 4)
		{
			m_BlendEnabled[attachment] = enabled;
			InvalidateBindings();
		}
	}

	void MetalCommandList::SetSRGBWriteEnabled(bool enabled)
	{
		// sRGB on Metal is a backbuffer pixel-format property (BGRA8Unorm
		// here); the legacy LDR sRGB-encode path is GL-only.
		m_SRGBWrite = enabled;
	}

	void MetalCommandList::PushRenderTarget()
	{
		TargetState state;
		state.Targets = m_Targets;
		state.Viewport = m_Viewport;
		state.Width = m_TargetWidth;
		state.Height = m_TargetHeight;
		m_TargetStack.push_back(state);
	}

	void MetalCommandList::PopRenderTarget()
	{
		if (m_TargetStack.empty())
			return;
		EndEncoder();
		const TargetState state = m_TargetStack.back();
		m_TargetStack.pop_back();
		m_Targets = state.Targets;
		m_NativeYViewport = (m_Targets == nullptr);
		m_TargetWidth = state.Width;
		m_TargetHeight = state.Height;
		if (m_Targets != nullptr)
		{
			for (unsigned int i = 0; i < 4; ++i)
				m_ColorFormats[i] = i < m_Targets->GetColorCount()
					? MetalEnumMap::PixelFormat(m_Targets->GetColorTexture(i)->GetDesc().Format)
					: MTL::PixelFormatInvalid;
			m_DepthFormat = m_Targets->HasDepth()
				? MetalEnumMap::PixelFormat(m_Targets->GetDepthTexture()->GetDesc().Format)
				: MTL::PixelFormatInvalid;
		}
		else
		{
			m_ColorFormats[0] = MTL::PixelFormatBGRA8Unorm;
			m_DepthFormat = MTL::PixelFormatInvalid;
		}
		m_Viewport = state.Viewport;
		InvalidateBindings();
		EnsureEncoder();
	}

	void MetalCommandList::SetPrimitiveMode(RHIPrimitiveMode mode)
	{
		m_Primitive = mode;
		InvalidateBindings();
	}

	void MetalCommandList::SetFillMode(RHIFillMode mode)
	{
		m_Fill = mode;
	}

	void MetalCommandList::SetDepthBias(float factor, float units)
	{
		// GL polygonOffset(factor, units) -> MTL depthBias(units, factor, clamp).
		m_DepthBiasFactor = factor;
		m_DepthBiasUnits = units;
		if (m_Encoder != nullptr)
			m_Encoder->setDepthBias(units, factor, 0.0f);
	}

	void MetalCommandList::SetVertexBuffer(const RHIBufferRef &buffer, unsigned int stream)
	{
		if (stream < MAX_STREAMS)
			m_VertexStreams[stream] = std::static_pointer_cast<MetalBuffer>(buffer);
	}

	void MetalCommandList::SetIndexBuffer(const RHIBufferRef &buffer)
	{
		m_IndexBuffer = std::static_pointer_cast<MetalBuffer>(buffer);
	}

	void MetalCommandList::SetMeshGeometry(const RHIMeshGeometryDesc &desc)
	{
		m_MeshChannelCount = desc.ChannelCount;
		for (int i = 0; i < desc.ChannelCount && i < (int)RHIMeshGeometryDesc::MAX_CHANNELS; ++i)
			m_MeshChannels[i] = desc.Channels[i];
		m_MeshName = desc.MeshName;
		InvalidateBindings();
	}

	void MetalCommandList::SetInstanceMatrixStream(const RHIBufferRef &buffer, const char *const attrNames[4])
	{
		m_InstanceBuffer = std::static_pointer_cast<MetalBuffer>(buffer);
		m_HasInstanceStream = buffer != nullptr;
		if (attrNames != nullptr)
		{
			for (int r = 0; r < 4; ++r)
				m_InstanceAttrNames[r] = attrNames[r] != nullptr ? attrNames[r] : "";
		}
		InvalidateBindings();
	}

	RHIVertexFormat MetalCommandList::MeshChannelFormat(int components, bool integer)
	{
		if (integer)
			return RHIVertexFormat::INT4;
		switch (components)
		{
		case 1: return RHIVertexFormat::FLOAT;
		case 2: return RHIVertexFormat::FLOAT2;
		case 4: return RHIVertexFormat::FLOAT4;
		default: return RHIVertexFormat::FLOAT3;
		}
	}

	void MetalCommandList::BuildMeshLayout(std::vector<RHIVertexAttribute> &out)
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
			attr.Offset = 0;

			if (!channel.Dirty && channel.Buffer != nullptr)
			{
				attr.Stream = static_cast<unsigned int>(MESH_STREAM_BASE + i);
				attr.Stride = static_cast<unsigned int>(channel.Components) * 4;
			}
			else if (channel.Empty)
			{
				// GL's disabled attribute reads constant (0,0,0,1): the
				// stride-0 constants stream (bound at CONSTANTS_STREAM).
				attr.Stream = CONSTANTS_STREAM;
				attr.Stride = 0;
				attr.ConstantFill = true;
			}
			else
			{
				FURYW << "Mesh " << m_MeshName << " " << channel.Name << " data dirty!";
				continue;
			}
			out.push_back(std::move(attr));
		}

		if (m_HasInstanceStream && m_InstanceBuffer != nullptr && m_CurrentModule != nullptr)
		{
			for (int r = 0; r < 4; ++r)
			{
				if (m_InstanceAttrNames[r].empty())
					continue;
				const int loc = m_CurrentModule->ResolveAttribLocation(m_InstanceAttrNames[r]);
				if (loc < 0)
					continue;
				RHIVertexAttribute attr;
				attr.Name = m_InstanceAttrNames[r];
				attr.Format = RHIVertexFormat::FLOAT4;
				attr.Location = loc;
				attr.Offset = static_cast<unsigned int>(r) * 16;
				attr.Stream = INSTANCE_STREAM;
				attr.Stride = 64;
				attr.Divisor = 1;
				out.push_back(std::move(attr));
			}
		}
	}

	void MetalCommandList::SetStorageBuffer(unsigned int binding, const RHIBufferRef &buffer)
	{
		if (buffer == nullptr)
			m_StorageBuffers.erase(binding);
		else
			m_StorageBuffers[binding] = std::static_pointer_cast<MetalBuffer>(buffer);
	}

	void MetalCommandList::SetStorageBufferNamed(const char *name, unsigned int binding, const RHIBufferRef &buffer)
	{
		if (name == nullptr)
		{
			SetStorageBuffer(binding, buffer);
			return;
		}
		if (buffer == nullptr)
			m_NamedStorageBuffers.erase(name);
		else
			m_NamedStorageBuffers[name] = std::static_pointer_cast<MetalBuffer>(buffer);
	}

	MTL::Buffer *MetalCommandList::GetConstantsBuffer()
	{
		if (m_ConstantsBuffer == nullptr)
		{
			// (0,0,0,1) float + int pattern; any channel format reads the same
			// 16 bytes as GL's disabled-attribute constant.
			static const float kFill[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			RHIBufferDesc desc;
			desc.Name = "MetalConstants";
			desc.Type = RHIBufferType::VERTEX;
			desc.Usage = RHIBufferUsage::STATIC;
			desc.Size = sizeof(kFill);
			m_ConstantsBuffer = MetalBuffer::Create(m_Device->GetMTLDevice(), desc, kFill);
		}
		return m_ConstantsBuffer->Get();
	}

	RHIUniformHandle MetalCommandList::ResolveUniform(const std::string &name)
	{
		EnsurePSO();
		const auto it = m_UniformBindings.find(name);
		if (it != m_UniformBindings.end())
			return EncodeHandle(it->second.Stage, it->second.Index, 0);
		const auto tt = m_TextureBindings.find(name);
		if (tt != m_TextureBindings.end())
			return EncodeHandle(tt->second.Stage, tt->second.Index, 1);
		if (m_CurrentModule != nullptr)
		{
			const long mi = m_CurrentModule->FindMemberIndex(name);
			if (mi >= 0)
				return EncodeHandle(2, static_cast<int>(mi), 2);
		}
		return -1;
	}

	void MetalCommandList::WriteShadow(const std::string &name, const void *data, size_t size)
	{
		// Cross-compiled GLSL: loose uniforms are globals-block members;
		// write into every stage's shadow that declares the member.
		if (m_CurrentModule != nullptr)
		{
			bool hit = false;
			for (int stage = 0; stage < 2; ++stage)
			{
				if (!m_GlobalsActive[stage])
					continue;
				const auto &globals = m_CurrentModule->GetStageGlobals(stage);
				const long mi = globals.FindMember(name);
				if (mi < 0)
					continue;
				const auto &member = globals.members[mi];
				const size_t n = std::min(size, member.size);
				if (member.offset + n <= m_GlobalsShadow[stage].size())
					std::memcpy(m_GlobalsShadow[stage].data() + member.offset, data, n);
				hit = true;
			}
			if (hit)
				return;
		}

		// Hand-authored MSL: direct argument bindings from PSO reflection.
		// Cross-compiled modules have no direct uniform arguments (a globals
		// miss above means the uniform is absent), and forcing a PSO build
		// here can hit a transiently stale mesh layout and fail creation.
		if (m_CurrentModule != nullptr && m_CurrentModule->IsCrossCompiled())
			return;
		EnsurePSO();
		const auto it = m_UniformBindings.find(name);
		if (it == m_UniformBindings.end())
			return;
		auto &slot = m_UniformShadow[ShadowKey(it->second.Stage, it->second.Index)];
		slot.assign(static_cast<const unsigned char *>(data), static_cast<const unsigned char *>(data) + size);
	}

	void MetalCommandList::WriteShadowAt(RHIUniformHandle handle, const void *data, size_t size)
	{
		int stage = 0, index = -1, kind = -1;
		DecodeHandle(handle, stage, index, kind);
		if (index < 0)
			return;

		if (kind == 3)
		{
			// Deferred-name handle: resolve against the live PSO now.
			if (m_CurrentModule == nullptr)
				return;
			const std::string &name = m_CurrentModule->GetHandleName(static_cast<size_t>(index));
			WriteShadow(name, data, size);
			return;
		}
		if (kind == 2)
		{
			// Globals member: write into every stage shadow that has it.
			if (m_CurrentModule == nullptr)
				return;
			const auto &member = m_CurrentModule->GetMember(static_cast<size_t>(index));
			for (int s = 0; s < 2; ++s)
			{
				if (!m_GlobalsActive[s] || member.offset[s] < 0)
					continue;
				const size_t n = std::min(size, member.size);
				if (static_cast<size_t>(member.offset[s]) + n <= m_GlobalsShadow[s].size())
					std::memcpy(m_GlobalsShadow[s].data() + member.offset[s], data, n);
			}
			return;
		}
		if (kind != 0)
			return;

		auto &slot = m_UniformShadow[ShadowKey(stage, index)];
		slot.assign(static_cast<const unsigned char *>(data), static_cast<const unsigned char *>(data) + size);
	}

	void MetalCommandList::SetUniformFloat(const std::string &name, float v0) { WriteShadow(name, &v0, 4); }

	void MetalCommandList::SetUniformFloat(const std::string &name, float v0, float v1) { const float v[2] = { v0, v1 }; WriteShadow(name, v, 8); }

	void MetalCommandList::SetUniformFloat(const std::string &name, float v0, float v1, float v2) { const float v[3] = { v0, v1, v2 }; WriteShadow(name, v, 12); }

	void MetalCommandList::SetUniformFloat(const std::string &name, float v0, float v1, float v2, float v3) { const float v[4] = { v0, v1, v2, v3 }; WriteShadow(name, v, 16); }

	void MetalCommandList::SetUniformFloatArray(const std::string &name, int size, int count, const float *value) { WriteShadow(name, value, static_cast<size_t>(size) * count * 4); }

	void MetalCommandList::SetUniformInt(const std::string &name, int v0) { WriteShadow(name, &v0, 4); }

	void MetalCommandList::SetUniformInt(const std::string &name, int v0, int v1) { const int v[2] = { v0, v1 }; WriteShadow(name, v, 8); }

	void MetalCommandList::SetUniformInt(const std::string &name, int v0, int v1, int v2) { const int v[3] = { v0, v1, v2 }; WriteShadow(name, v, 12); }

	void MetalCommandList::SetUniformInt(const std::string &name, int v0, int v1, int v2, int v3) { const int v[4] = { v0, v1, v2, v3 }; WriteShadow(name, v, 16); }

	void MetalCommandList::SetUniformIntArray(const std::string &name, int size, int count, const int *value) { WriteShadow(name, value, static_cast<size_t>(size) * count * 4); }

	void MetalCommandList::SetUniformUInt(const std::string &name, unsigned int v0) { WriteShadow(name, &v0, 4); }

	void MetalCommandList::SetUniformMatrix(const std::string &name, const Matrix4 &matrix) { WriteShadow(name, matrix.Raw, 64); }

	void MetalCommandList::SetUniformMatrix(const std::string &name, const float *raw) { WriteShadow(name, raw, 64); }

	void MetalCommandList::SetUniformMatrices(const std::string &name, int count, const float *raw) { WriteShadow(name, raw, static_cast<size_t>(count) * 64); }

	void MetalCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x) { WriteShadowAt(handle, &x, 4); }

	void MetalCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y) { const float v[2] = { x, y }; WriteShadowAt(handle, v, 8); }

	void MetalCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z) { const float v[3] = { x, y, z }; WriteShadowAt(handle, v, 12); }

	void MetalCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z, float w) { const float v[4] = { x, y, z, w }; WriteShadowAt(handle, v, 16); }

	void MetalCommandList::SetUniformIntAt(RHIUniformHandle handle, int x) { WriteShadowAt(handle, &x, 4); }

	void MetalCommandList::SetUniformMatrixAt(RHIUniformHandle handle, const float *raw) { WriteShadowAt(handle, raw, 64); }

	void MetalCommandList::SetUniformMatricesAt(RHIUniformHandle handle, int count, const float *raw) { WriteShadowAt(handle, raw, static_cast<size_t>(count) * 64); }

	void MetalCommandList::SetTexture(const std::string &name, const RHITextureRef &texture)
	{
		if (m_PSO != nullptr)
		{
			bool bound = false;
			for (int stage = 0; stage < 2; ++stage)
			{
				const auto it = m_TexturesByStage[stage].find(name);
				if (it == m_TexturesByStage[stage].end())
					continue;
				m_Textures[ShadowKey(stage, it->second.Index)].Texture = std::static_pointer_cast<MetalTexture>(texture);
				bound = true;
			}
			if (bound)
				return;
		}
		m_PendingTextures[name] = std::static_pointer_cast<MetalTexture>(texture);
		BindTextureUnit(texture);
	}

	int MetalCommandList::SetTextureAt(RHIUniformHandle handle, const RHITextureRef &texture)
	{
		int stage = 0, index = -1, kind = -1;
		DecodeHandle(handle, stage, index, kind);
		if (kind == 3 && index >= 0 && m_CurrentModule != nullptr)
		{
			// Deferred-name handle: resolve against the reflection maps. Cold
			// maps (PSO not yet created for the current layout) must not force
			// a PSO build here -- the mesh layout at bind time can be stale and
			// fail creation transiently. Pend by name and resolve at flush.
			const std::string &name = m_CurrentModule->GetHandleName(static_cast<size_t>(index));
			bool bound = false;
			if (m_PSO != nullptr)
			{
				for (int s = 0; s < 2; ++s)
				{
					const auto it = m_TexturesByStage[s].find(name);
					if (it == m_TexturesByStage[s].end())
						continue;
					m_Textures[ShadowKey(s, it->second.Index)].Texture = std::static_pointer_cast<MetalTexture>(texture);
					bound = true;
				}
			}
			if (!bound)
			{
				m_PendingTextures[name] = std::static_pointer_cast<MetalTexture>(texture);
				++m_TextureCursor;
				return m_TextureCursor;
			}
			++m_TextureCursor;
			return m_TextureCursor;
		}
		if (index >= 0)
			m_Textures[ShadowKey(stage, index)].Texture = std::static_pointer_cast<MetalTexture>(texture);
		++m_TextureCursor;
		return m_TextureCursor;
	}

	void MetalCommandList::SetTextureUnitCursor(int unitOffset)
	{
		m_TextureCursor = unitOffset;
	}

	void MetalCommandList::BindTextureUnit(const RHITextureRef &texture)
	{
		m_Textures[ShadowKey(1, static_cast<int>(m_TextureCursor))].Texture = std::static_pointer_cast<MetalTexture>(texture);
		++m_TextureCursor;
	}

	void MetalCommandList::FlushDrawState()
	{
		m_Encoder->setRenderPipelineState(m_PSO);

		// GL depth-test-off = compare always + no write.
		const CompareMode effFunc = m_DepthTest ? m_DepthFunc : CompareMode::ALWAYS;
		const bool effWrite = m_DepthTest && m_DepthWrite;
		if (m_DepthFormat != MTL::PixelFormatInvalid)
			m_Encoder->setDepthStencilState(m_Device->GetDepthState(effFunc, effWrite));

		m_Encoder->setCullMode(MetalEnumMap::Cull(m_Cull));
		// Winding flips with the viewport sign (the negative-height offscreen
		// viewport inverts the rasterizer's y vs the backbuffer's native one).
		m_Encoder->setFrontFacingWinding(m_NativeYViewport ? MTL::WindingCounterClockwise : MTL::WindingClockwise);
		m_Encoder->setTriangleFillMode(m_Fill == RHIFillMode::WIREFRAME ? MTL::TriangleFillModeLines : MTL::TriangleFillModeFill);

		for (int i = 0; i < MAX_STREAMS; ++i)
		{
			if (m_VertexStreams[i] != nullptr)
				m_Encoder->setVertexBuffer(m_VertexStreams[i]->Get(), 0, i);
		}
		for (int i = 0; i < m_MeshChannelCount; ++i)
		{
			const auto &channel = m_MeshChannels[i];
			if (channel.Attrib >= 0 && !channel.Dirty && channel.Buffer != nullptr)
				m_Encoder->setVertexBuffer(static_cast<MetalBuffer *>(channel.Buffer.get())->Get(), 0, MESH_STREAM_BASE + i);
		}
		if (m_HasInstanceStream && m_InstanceBuffer != nullptr)
			m_Encoder->setVertexBuffer(m_InstanceBuffer->Get(), 0, INSTANCE_STREAM);
		if (m_MeshChannelCount > 0)
			m_Encoder->setVertexBuffer(GetConstantsBuffer(), 0, CONSTANTS_STREAM);
		for (const auto &kv : m_StorageBuffers)
		{
			m_Encoder->setVertexBuffer(kv.second->Get(), 0, kv.first);
			m_Encoder->setFragmentBuffer(kv.second->Get(), 0, kv.first);
		}
		for (const auto &kv : m_NamedStorageBuffers)
		{
			for (int stage = 0; stage < 2; ++stage)
			{
				const auto it = m_UniformsByStage[stage].find(kv.first);
				if (it == m_UniformsByStage[stage].end() || it->second.Index < 0)
					continue;
				if (stage == 0)
					m_Encoder->setVertexBuffer(kv.second->Get(), 0, it->second.Index);
				else
					m_Encoder->setFragmentBuffer(kv.second->Get(), 0, it->second.Index);
			}
		}
		for (const auto &kv : m_UniformShadow)
		{
			const int stage = static_cast<int>(kv.first >> 32);
			const int index = static_cast<int>(kv.first & 0xFFFFFFFF);
			if (kv.second.empty())
				continue;
			if (stage == 0)
				m_Encoder->setVertexBytes(kv.second.data(), kv.second.size(), index);
			else
				m_Encoder->setFragmentBytes(kv.second.data(), kv.second.size(), index);
		}
		for (int stage = 0; stage < 2; ++stage)
		{
			if (!m_GlobalsActive[stage] || m_GlobalsShadow[stage].empty() || m_CurrentModule == nullptr)
				continue;
			// The block's MSL argument index comes from the PSO reflection:
			// SPIRV-Cross reassigns buffer indices when other buffers are
			// present (an SSBO shifts the globals block), so the compiler
			// sidecar's SPIR-V binding is not the argument index.
			const auto &blockName = m_CurrentModule->GetStageGlobals(stage).blockName;
			const auto it = m_UniformsByStage[stage].find(blockName);
			if (it == m_UniformsByStage[stage].end() || it->second.Index < 0)
				continue;
			if (stage == 0)
				m_Encoder->setVertexBytes(m_GlobalsShadow[stage].data(), m_GlobalsShadow[stage].size(), it->second.Index);
			else
				m_Encoder->setFragmentBytes(m_GlobalsShadow[stage].data(), m_GlobalsShadow[stage].size(), it->second.Index);
		}
		// Texture binds that missed cold reflection maps land here once the
		// PSO is valid (same draw); unresolvable names stay parked (GL skips
		// absent uniforms the same way).
		for (auto it = m_PendingTextures.begin(); it != m_PendingTextures.end(); )
		{
			bool bound = false;
			for (int stage = 0; stage < 2; ++stage)
			{
				const auto bind = m_TexturesByStage[stage].find(it->first);
				if (bind == m_TexturesByStage[stage].end())
					continue;
				m_Textures[ShadowKey(stage, bind->second.Index)].Texture = it->second;
				bound = true;
			}
			if (bound)
				it = m_PendingTextures.erase(it);
			else
				++it;
		}
		for (const auto &kv : m_Textures)
		{
			const int stage = static_cast<int>(kv.first >> 32);
			const int index = static_cast<int>(kv.first & 0xFFFFFFFF);
			if (kv.second.Texture == nullptr)
				continue;
			if (stage == 0)
			{
				m_Encoder->setVertexTexture(kv.second.Texture->Get(), index);
				m_Encoder->setVertexSamplerState(kv.second.Texture->GetSampler(), index);
			}
			else
			{
				m_Encoder->setFragmentTexture(kv.second.Texture->Get(), index);
				m_Encoder->setFragmentSamplerState(kv.second.Texture->GetSampler(), index);
			}
		}
	}

	void MetalCommandList::Draw(unsigned int vertexCount, unsigned int firstVertex)
	{
		if (EnsureEncoder() == nullptr || !EnsurePSO())
			return;
		FlushDrawState();
		m_Encoder->drawPrimitives(MetalEnumMap::Primitive(m_Primitive), firstVertex, vertexCount);
	}

	void MetalCommandList::DrawIndexed(unsigned int indexCount, unsigned int firstIndex, int vertexOffset)
	{
		if (EnsureEncoder() == nullptr || !EnsurePSO() || m_IndexBuffer == nullptr)
			return;
		FlushDrawState();
		const MTL::IndexType indexType = m_IndexBuffer->GetDesc().Index32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
		m_Encoder->drawIndexedPrimitives(MetalEnumMap::Primitive(m_Primitive), indexCount, indexType,
			m_IndexBuffer->Get(), firstIndex * (indexType == MTL::IndexTypeUInt32 ? 4 : 2), 1, vertexOffset, 0);
	}

	void MetalCommandList::DrawInstanced(unsigned int vertexCount, unsigned int instanceCount, unsigned int firstVertex)
	{
		if (EnsureEncoder() == nullptr || !EnsurePSO())
			return;
		FlushDrawState();
		m_Encoder->drawPrimitives(MetalEnumMap::Primitive(m_Primitive), firstVertex, vertexCount, instanceCount);
	}

	void MetalCommandList::DrawIndexedInstanced(unsigned int indexCount, unsigned int instanceCount, unsigned int firstIndex, int vertexOffset)
	{
		if (EnsureEncoder() == nullptr || !EnsurePSO() || m_IndexBuffer == nullptr)
			return;
		FlushDrawState();
		const MTL::IndexType indexType = m_IndexBuffer->GetDesc().Index32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
		m_Encoder->drawIndexedPrimitives(MetalEnumMap::Primitive(m_Primitive), indexCount, indexType,
			m_IndexBuffer->Get(), firstIndex * (indexType == MTL::IndexTypeUInt32 ? 4 : 2), instanceCount, vertexOffset, 0);
	}

	void MetalCommandList::Dispatch(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ)
	{
		// No engine compute shaders exist yet; arrives with its first user.
		static bool warned = false;
		if (!warned)
		{
			FURYW << "MetalCommandList::Dispatch: compute path not implemented yet";
			warned = true;
		}
	}

	void MetalCommandList::Barrier()
	{
		// Encoders serialize within a command buffer; nothing to emit.
	}

	void MetalCommandList::Blit(const RHITextureRef &src, const RHITextureRef &dst, int srcWidth, int srcHeight, int dstWidth, int dstHeight, bool linearFilter)
	{
		MetalTexture *srcTex = static_cast<MetalTexture *>(src.get());
		MetalTexture *dstTex = static_cast<MetalTexture *>(dst.get());
		if (srcTex == nullptr || dstTex == nullptr)
			return;
		if (srcWidth != dstWidth || srcHeight != dstHeight)
		{
			// MTL blit copies cannot scale; no current caller scales (ocean
			// depth copy + editor readback paths are 1:1).
			static bool warned = false;
			if (!warned)
			{
				FURYW << "MetalCommandList::Blit: scaled blits are unsupported, copy skipped";
				warned = true;
			}
			return;
		}
		EndEncoder();
		MTL::BlitCommandEncoder *blit = m_Device->GetBlitEncoder();
		if (blit == nullptr)
			return;
		blit->copyFromTexture(srcTex->Get(), 0, 0, MTL::Origin::Make(0, 0, 0),
			MTL::Size::Make(srcWidth, srcHeight, 1), dstTex->Get(), 0, 0, MTL::Origin::Make(0, 0, 0));
		m_Device->EndBlitEncoder();
	}

	void MetalCommandList::InvalidateBindings()
	{
		m_PSO = nullptr;
		m_PSOValid = false;
		m_UniformBindings.clear();
		m_TextureBindings.clear();
		m_UniformShadow.clear();
	}

	uint32_t MetalCommandList::HashVertexLayout(const std::vector<RHIVertexAttribute> &attributes)
	{
		uint32_t hash = 2166136261u;
		for (const auto &attr : attributes)
		{
			hash = (hash ^ static_cast<uint32_t>(attr.Format)) * 16777619u;
			hash = (hash ^ attr.Offset) * 16777619u;
			hash = (hash ^ attr.Stride) * 16777619u;
			hash = (hash ^ attr.Divisor) * 16777619u;
			hash = (hash ^ static_cast<uint32_t>(attr.Location)) * 16777619u;
			hash = (hash ^ attr.Stream) * 16777619u;
			hash = (hash ^ (attr.ConstantFill ? 0x9E37u : 0x85u)) * 16777619u;
		}
		return hash;
	}

	const std::vector<RHIVertexAttribute> *MetalCommandList::CurrentVertexLayout()
	{
		// Mesh geometry wins: passes carry no vertex attributes (GL resolves
		// by name at draw time), so the dynamic mesh desc is the layout.
		if (m_MeshChannelCount > 0 || m_HasInstanceStream)
		{
			BuildMeshLayout(m_MeshLayout);
			return &m_MeshLayout;
		}
		if (m_CurrentPipeline != nullptr)
			return &m_CurrentPipeline->GetDesc().VertexAttributes;
		return nullptr;
	}

	bool MetalCommandList::EnsurePSO()
	{
		if (m_PSOValid)
			return m_PSO != nullptr;
		m_PSOValid = true;
		m_PSO = nullptr;
		m_UniformBindings.clear();
		m_TextureBindings.clear();

		if (m_CurrentModule == nullptr || !m_CurrentModule->IsValid())
			return false;

		RHIMetalDevice::PSOKey key;
		key.Module = m_CurrentModule.get();
		key.Blend = m_Blend;
		key.ColorWrite = m_ColorWrite;
		for (int i = 0; i < 4; ++i)
		{
			key.BlendEnabled[i] = m_BlendEnabled[i];
			key.ColorFormats[i] = static_cast<uint8_t>(m_ColorFormats[i]);
		}
		key.DepthFormat = static_cast<uint8_t>(m_DepthFormat);
		const auto *layout = CurrentVertexLayout();
		key.VertexHash = layout != nullptr ? HashVertexLayout(*layout) : 0;
		// PSO topology class differs for point/line/triangle on some GPUs.
		key.VertexHash = key.VertexHash * 4u + static_cast<uint32_t>(m_Primitive);

		const auto *entry = m_Device->GetOrCreatePSO(key, layout);
		if (entry == nullptr)
			return false;
		m_PSO = entry->PSO;
		m_UniformBindings = entry->Maps.Uniforms;
		m_TextureBindings = entry->Maps.Textures;
		for (int s = 0; s < 2; ++s)
		{
			m_UniformsByStage[s] = entry->Maps.UniformsByStage[s];
			m_TexturesByStage[s] = entry->Maps.TexturesByStage[s];
		}
		return true;
	}
}

#endif // FURY_RHI_METAL
