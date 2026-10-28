#include "Fury/RHI/Metal/RHIMetalDevice.h"

#if FURY_RHI_METAL

#include <SFML/Window/WindowBase.hpp>

#include <atomic>

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_metal.h"

#include "Fury/InputUtil.h"
#include "Fury/Log.h"
#include "Fury/RHI/Metal/MetalEnumMap.h"
#include "Fury/RHI/Metal/MetalWindowBridge.h"

#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fury
{
	namespace
	{
		float HalfToFloat(unsigned short h)
		{
			const unsigned int sign = (h >> 15) & 1;
			const unsigned int exp = (h >> 10) & 0x1F;
			const unsigned int mant = h & 0x3FF;
			if (exp == 0)
				return (sign ? -1.0f : 1.0f) * (mant / 1024.0f) * 0.00006103515625f;
			if (exp == 31)
				return mant != 0 ? 0.0f : (sign ? -65504.0f : 65504.0f);
			const float v = (1.0f + mant / 1024.0f) * exp2f(static_cast<float>(static_cast<int>(exp) - 15));
			return sign ? -v : v;
		}

		unsigned char To8(float v)
		{
			if (!(v > 0.0f))
				return 0;
			return static_cast<unsigned char>(v >= 1.0f ? 255 : v * 255.0f);
		}
	}

	bool RHIMetalDevice::PSOKey::operator<(const PSOKey &o) const
	{
		return std::tie(Module, Blend, ColorWrite,
			BlendEnabled[0], BlendEnabled[1], BlendEnabled[2], BlendEnabled[3],
			ColorFormats[0], ColorFormats[1], ColorFormats[2], ColorFormats[3],
			DepthFormat, VertexHash)
			< std::tie(o.Module, o.Blend, o.ColorWrite,
			o.BlendEnabled[0], o.BlendEnabled[1], o.BlendEnabled[2], o.BlendEnabled[3],
			o.ColorFormats[0], o.ColorFormats[1], o.ColorFormats[2], o.ColorFormats[3],
			o.DepthFormat, o.VertexHash);
	}

	RHIDevice *CreateMetalDevice(const RHIWindowInfo &window)
	{
		auto *device = new RHIMetalDevice(window);
		if (!device->IsUsable())
		{
			delete device;
			return nullptr;
		}
		return device;
	}

	RHIMetalDevice::RHIMetalDevice(const RHIWindowInfo &window)
		: m_Window(window.Window)
		, m_CommandList(this)
	{
		m_Device = MTL::CreateSystemDefaultDevice();
		if (m_Device == nullptr)
		{
			FURYE << "RHIMetalDevice: no system default Metal device";
			return;
		}
		m_Queue = m_Device->newCommandQueue();
		if (m_Queue == nullptr)
		{
			FURYE << "RHIMetalDevice: command queue creation failed";
			m_Device->release();
			m_Device = nullptr;
			return;
		}

		m_Layer = static_cast<CA::MetalLayer *>(MetalAttachLayer(m_Window));
		if (m_Layer == nullptr)
		{
			FURYE << "RHIMetalDevice: failed to attach a CAMetalLayer to the window";
			m_Queue->release();
			m_Device->release();
			m_Queue = nullptr;
			m_Device = nullptr;
			return;
		}
		m_Layer->setDevice(m_Device);
		m_Layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
		// ReadbackBackbuffer samples the drawable texture.
		m_Layer->setFramebufferOnly(false);
		if (m_Window != nullptr)
		{
			// Main thread: SFML's size getter does AppKit calls.
			const auto size = m_Window->getSize();
			m_LayerWidth = size.x;
			m_LayerHeight = size.y;
		}
		SyncLayerSize();

		m_Caps.NdcDepthZeroToOne = true;
		// Metal NDC is y-up like GL; only framebuffer/texture addressing is
		// top-left (viewport + scissor conversions handle that).
		m_Caps.ClipSpaceYDown = false;
		m_Caps.SupportsSSBO = true;
		m_Caps.SupportsCompute = true;
		m_Caps.SupportsTexture3D = true;
		m_Caps.SupportsCompressedBC = true;
		m_Caps.MaxTextureSize = 16384;
		m_Caps.MaxTextureArrayLayers = 2048;

		FURYI << "Metal device: " << m_Device->name()->utf8String();
	}

	RHIMetalDevice::~RHIMetalDevice()
	{
		for (auto &kv : m_PSOCache)
			kv.second.PSO->release();
		m_PSOCache.clear();
		for (auto &kv : m_DepthCache)
			kv.second->release();
		m_DepthCache.clear();

		if (m_LastPresented != nullptr) m_LastPresented->release();
		if (m_FramePool != nullptr) m_FramePool->release();
		if (m_Layer != nullptr) m_Layer->release();
		if (m_Queue != nullptr) m_Queue->release();
		if (m_Device != nullptr) m_Device->release();
	}

	void RHIMetalDevice::SyncLayerSize()
	{
		if (m_Layer == nullptr)
			return;
		const unsigned int w = m_LayerWidth.load();
		const unsigned int h = m_LayerHeight.load();
		if (w == 0 || h == 0)
			return;
		const CGSize current = m_Layer->drawableSize();
		if (static_cast<unsigned int>(current.width) != w || static_cast<unsigned int>(current.height) != h)
		{
			const CGSize next = { static_cast<CGFloat>(w), static_cast<CGFloat>(h) };
			m_Layer->setDrawableSize(next);
		}
	}

	void RHIMetalDevice::SetBackbufferSize(unsigned int width, unsigned int height)
	{
		if (width == 0 || height == 0)
			return;
		if (m_LayerWidth.exchange(width) != width || m_LayerHeight.exchange(height) != height)
			SyncLayerSize();
	}

	void RHIMetalDevice::BeginFrame()
	{
		BeginFrameTracking();
		// Window-size tracking without touching SFML off the main thread:
		// InputUtil's size pair is event-handler written (main thread).
		{
			int winW = 0, winH = 0;
			InputUtil::Instance()->GetWindowSize(winW, winH);
			if (winW > 0 && winH > 0)
				SetBackbufferSize(static_cast<unsigned int>(winW), static_cast<unsigned int>(winH));
		}
		// The pool covers this frame's autoreleased objects (command buffer,
		// drawable, pass descriptors); drained in Present on the same thread.
		m_FramePool = NS::AutoreleasePool::alloc()->init();
		m_CommandBuffer = m_Queue->commandBuffer();
		m_Drawable = nullptr;
		SyncLayerSize();
		m_CommandList.OnBeginFrame();
	}

	void RHIMetalDevice::EndFrame()
	{
		m_CommandList.OnEndFrame();
	}

	void RHIMetalDevice::Present()
	{
		EndEncoders();
		if (m_CommandBuffer != nullptr)
		{
			if (m_Drawable != nullptr)
			{
				m_CommandBuffer->presentDrawable(m_Drawable);
				if (m_LastPresented != nullptr) m_LastPresented->release();
				m_LastPresented = m_Drawable;
				m_LastPresented->retain();
			}
			m_CommandBuffer->commit();
			m_CommandBuffer = nullptr;
			m_Drawable = nullptr;
		}
		if (m_FramePool != nullptr)
		{
			m_FramePool->release();
			m_FramePool = nullptr;
		}
	}

	CA::MetalDrawable *RHIMetalDevice::GetDrawable()
	{
		if (m_Drawable == nullptr && m_Layer != nullptr)
			m_Drawable = m_Layer->nextDrawable();
		return m_Drawable;
	}

	bool RHIMetalDevice::InitUiRenderer()
	{
		// The ImGui context must exist before the renderer backend is
		// initialized (it registers itself on the context).
		if (ImGui::GetCurrentContext() == nullptr)
			return false;
		if (!ImGui_ImplMetal_Init(m_Device))
		{
			FURYE << "Metal: ImGui Metal backend init failed";
			return false;
		}
		return true;
	}

	void RHIMetalDevice::ShutdownUiRenderer()
	{
		if (ImGui::GetCurrentContext() != nullptr)
			ImGui_ImplMetal_Shutdown();
	}

	void RHIMetalDevice::DrawUiOverlay(void *drawData)
	{
		m_CommandList.DrawUiOverlay(drawData, m_LayerWidth.load(), m_LayerHeight.load());
	}

	void RHIMetalDevice::EndEncoders()
	{
		m_CommandList.EndEncoder();
		EndBlitEncoder();
	}

	MTL::BlitCommandEncoder *RHIMetalDevice::GetBlitEncoder()
	{
		if (m_CommandBuffer == nullptr)
			return nullptr;
		m_CommandList.EndEncoder();
		if (m_BlitEncoder == nullptr)
			m_BlitEncoder = m_CommandBuffer->blitCommandEncoder();
		return m_BlitEncoder;
	}

	void RHIMetalDevice::EndBlitEncoder()
	{
		if (m_BlitEncoder != nullptr)
		{
			m_BlitEncoder->endEncoding();
			m_BlitEncoder = nullptr;
		}
	}

	RHITextureRef RHIMetalDevice::CreateTexture(const RHITextureDesc &desc)
	{
		return MetalTexture::Create(m_Device, desc);
	}

	void RHIMetalDevice::UpdateTexture(const RHITextureRef &texture, const void *data, size_t size, int mipLevel, int layer)
	{
		if (texture == nullptr)
			return;
		MetalTexture *tex = static_cast<MetalTexture *>(texture.get());
		const unsigned int edge = MetalEnumMap::BlockEdge(tex->GetDesc().Format);
		if (edge > 1)
			tex->UploadCompressed(data, size, mipLevel);
		else
			tex->Upload(data, size, mipLevel, layer);
	}

	void RHIMetalDevice::UpdateTextureFloat32(const RHITextureRef &texture, const float *data, size_t componentCount, int mipLevel, int layer)
	{
		if (texture == nullptr)
			return;
		static_cast<MetalTexture *>(texture.get())->UploadFloat32(data, componentCount, mipLevel, layer);
	}

	void RHIMetalDevice::GenerateMipmaps(const RHITextureRef &texture)
	{
		if (texture == nullptr || m_Queue == nullptr)
			return;
		MTL::Texture *mtlTex = static_cast<MetalTexture *>(texture.get())->Get();
		// Single-level textures have nothing to generate (cloud-noise cache
		// loads hit this); Metal validation aborts on the call, GL no-ops.
		if (mtlTex == nullptr || mtlTex->mipmapLevelCount() <= 1)
			return;
		// Texture loads drain as render-thread jobs between frames, where no
		// frame command buffer exists; without a one-shot buffer the blit is
		// silently dropped and mips stay zero (black albedo at distance).
		if (m_CommandBuffer == nullptr)
		{
			NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			MTL::CommandBuffer *cb = m_Queue->commandBuffer();
			if (cb == nullptr)
				return;
			MTL::BlitCommandEncoder *blit = cb->blitCommandEncoder();
			blit->generateMipmaps(mtlTex);
			blit->endEncoding();
			cb->commit();
			return;
		}
		MTL::BlitCommandEncoder *blit = GetBlitEncoder();
		if (blit != nullptr)
			blit->generateMipmaps(mtlTex);
		EndBlitEncoder();
	}

	void RHIMetalDevice::UpdateSampler(const RHITextureRef &texture, const RHISamplerDesc &sampler)
	{
		if (texture != nullptr)
			static_cast<MetalTexture *>(texture.get())->ApplySampler(m_Device, sampler);
	}

	RHIBufferRef RHIMetalDevice::CreateBuffer(const RHIBufferDesc &desc, const void *initialData)
	{
		return MetalBuffer::Create(m_Device, desc, initialData);
	}

	void RHIMetalDevice::UpdateBuffer(const RHIBufferRef &buffer, const void *data, size_t size, size_t offset)
	{
		if (buffer != nullptr)
			static_cast<MetalBuffer *>(buffer.get())->Update(m_Device, data, size, offset);
	}

	RHIShaderModuleRef RHIMetalDevice::CreateShaderModule(const std::string &name, const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry)
	{
		return MetalShaderModule::Create(m_Device, name, vertex, fragment, geometry);
	}

	RHIShaderModuleRef RHIMetalDevice::CreateComputeModule(const std::string &name, const RHIShaderBlob &compute)
	{
		return MetalShaderModule::CreateCompute(m_Device, name, compute);
	}

	RHIGraphicsPipelineRef RHIMetalDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const RHIShaderModuleRef &shaders)
	{
		if (shaders == nullptr)
			return nullptr;
		return std::make_shared<MetalGraphicsPipeline>(desc, std::static_pointer_cast<MetalShaderModule>(shaders));
	}

	RHIRenderTargetSetRef RHIMetalDevice::CreateRenderTargetSet(const RHIRenderTargetSetDesc &desc)
	{
		return MetalRenderTargetSet::Create(desc);
	}

	const RHIMetalDevice::PSOEntry *RHIMetalDevice::GetOrCreatePSO(const PSOKey &key,
		const std::vector<RHIVertexAttribute> *vertexLayout)
	{
		const auto it = m_PSOCache.find(key);
		if (it != m_PSOCache.end())
			return &it->second;

		const MetalShaderModule *module = key.Module;
		if (module == nullptr || !module->IsValid())
			return nullptr;

		NS::SharedPtr<MTL::RenderPipelineDescriptor> desc = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
		desc->setVertexFunction(const_cast<MTL::Function *>(module->GetVertex()));
		desc->setFragmentFunction(const_cast<MTL::Function *>(module->GetFragment()));

		for (int i = 0; i < 4; ++i)
		{
			const MTL::PixelFormat format = static_cast<MTL::PixelFormat>(key.ColorFormats[i]);
			if (format == MTL::PixelFormatInvalid)
				continue;
			auto *att = desc->colorAttachments()->object(i);
			att->setPixelFormat(format);
			const bool blendOn = MetalEnumMap::BlendEnabled(key.Blend) && key.BlendEnabled[i];
			att->setBlendingEnabled(blendOn);
			if (blendOn)
			{
				att->setSourceRGBBlendFactor(MetalEnumMap::BlendModeSrc(key.Blend));
				att->setDestinationRGBBlendFactor(MetalEnumMap::BlendModeDest(key.Blend));
				att->setRgbBlendOperation(MetalEnumMap::BlendModeOp(key.Blend));
				att->setSourceAlphaBlendFactor(MetalEnumMap::BlendModeSrc(key.Blend));
				att->setDestinationAlphaBlendFactor(MetalEnumMap::BlendModeDest(key.Blend));
				att->setAlphaBlendOperation(MetalEnumMap::BlendModeOp(key.Blend));
			}
			att->setWriteMask(key.ColorWrite ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
		}
		if (key.DepthFormat != static_cast<uint8_t>(MTL::PixelFormatInvalid))
			desc->setDepthAttachmentPixelFormat(static_cast<MTL::PixelFormat>(key.DepthFormat));

		if (vertexLayout != nullptr && !vertexLayout->empty())
		{
			NS::SharedPtr<MTL::VertexDescriptor> vd = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
			// One layout per stream; attribute index = MSL [[attribute(n)]]
			// location (not declaration order -- sparse locations stay aligned).
			for (const auto &attr : *vertexLayout)
			{
				auto *layout = vd->layouts()->object(attr.Stream);
				layout->setStride(attr.Stride);
				if (attr.ConstantFill)
				{
					layout->setStepFunction(MTL::VertexStepFunctionConstant);
					layout->setStepRate(0);
				}
				else if (attr.Divisor > 0)
				{
					layout->setStepFunction(MTL::VertexStepFunctionPerInstance);
					layout->setStepRate(attr.Divisor);
				}
				else
				{
					layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
				}
				auto *att = vd->attributes()->object(attr.Location);
				att->setFormat(MetalEnumMap::VertexFormat(attr.Format));
				att->setOffset(attr.Offset);
				att->setBufferIndex(attr.Stream);
			}
			desc->setVertexDescriptor(vd.get());
		}

		NS::Error *error = nullptr;
		MTL::RenderPipelineReflection *reflection = nullptr;
		MTL::RenderPipelineState *pso = m_Device->newRenderPipelineState(desc.get(),
			MTL::PipelineOptionArgumentInfo, &reflection, &error);
		if (pso == nullptr)
		{
			FURYE << "RHIMetalDevice: PSO creation failed"
				<< (module != nullptr ? " for '" + module->GetName() + "'" : "") << ": "
				<< (error != nullptr ? error->localizedDescription()->utf8String() : "unknown");
			return nullptr;
		}

		PSOEntry entry;
		entry.PSO = pso;
		if (reflection != nullptr)
		{
			auto readArgs = [&](NS::Array *args, int stage) {
				if (args == nullptr)
					return;
				for (NS::UInteger i = 0; i < args->count(); ++i)
				{
					MTL::Argument *arg = static_cast<MTL::Argument *>(args->object(i));
					if (arg == nullptr || !arg->active())
						continue;
					MetalBindingEntry binding;
					binding.Stage = stage;
					binding.Index = static_cast<int>(arg->index());
					const std::string name = arg->name()->utf8String();
					if (arg->type() == MTL::ArgumentTypeBuffer)
					{
						entry.Maps.Uniforms[name] = binding;
						entry.Maps.UniformsByStage[stage][name] = binding;
					}
					else if (arg->type() == MTL::ArgumentTypeTexture)
					{
						entry.Maps.Textures[name] = binding;
						entry.Maps.TexturesByStage[stage][name] = binding;
					}
				}
			};
			readArgs(reflection->vertexArguments(), 0);
			readArgs(reflection->fragmentArguments(), 1);
		}

		return &m_PSOCache.emplace(key, entry).first->second;
	}

	MTL::DepthStencilState *RHIMetalDevice::GetDepthState(CompareMode func, bool write)
	{
		const uint32_t key = (static_cast<uint32_t>(func) << 1) | (write ? 1u : 0u);
		const auto it = m_DepthCache.find(key);
		if (it != m_DepthCache.end())
			return it->second;

		NS::SharedPtr<MTL::DepthStencilDescriptor> desc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
		desc->setDepthCompareFunction(MetalEnumMap::Compare(func));
		desc->setDepthWriteEnabled(write);
		MTL::DepthStencilState *state = m_Device->newDepthStencilState(desc.get());
		m_DepthCache.emplace(key, state);
		return state;
	}

	bool RHIMetalDevice::Readback(const RHITextureRef &texture, RHIPixelData &out)
	{
		MetalTexture *tex = static_cast<MetalTexture *>(texture.get());
		if (tex == nullptr || tex->Get() == nullptr)
			return false;

		const auto &desc = tex->GetDesc();
		if (desc.Type == TextureType::TEXTURE_CUBE_MAP || desc.Type == TextureType::TEXTURE_1D)
			return false;
		const MTL::PixelFormat format = MetalEnumMap::PixelFormat(desc.Format);

		// Output is always 8-bit RGBA, GL-normalized (float/depth clamp to
		// [0,1] like GL's UNSIGNED_BYTE readback).
		const int slices = (desc.Type == TextureType::TEXTURE_2D_ARRAY || desc.Type == TextureType::TEXTURE_3D)
			? std::max(1, desc.Depth) : 1;
		out.Width = desc.Width;
		out.Height = desc.Height;
		out.Depth = slices;
		out.Format = TextureFormat::RGBA8;
		out.Pixels.resize(static_cast<size_t>(desc.Width) * desc.Height * 4 * slices);

		NS::UInteger srcBpp = 4;
		switch (format)
		{
		case MTL::PixelFormatRGBA16Float: srcBpp = 8; break;
		case MTL::PixelFormatRGBA16Unorm: srcBpp = 8; break;
		case MTL::PixelFormatRGBA32Float: srcBpp = 16; break;
		case MTL::PixelFormatR16Float: srcBpp = 2; break;
		case MTL::PixelFormatR16Unorm: srcBpp = 2; break;
		case MTL::PixelFormatR8Unorm: srcBpp = 1; break;
		default: break;
		}
		std::vector<unsigned char> raw(static_cast<size_t>(desc.Width) * desc.Height * srcBpp);

		for (int slice = 0; slice < slices; ++slice)
		{
			// 3D depth planes address through the region's z (slice arg stays
			// 0); array layers use the slice arg with z=0.
			tex->Get()->getBytes(raw.data(), static_cast<NS::UInteger>(desc.Width) * srcBpp,
				static_cast<NS::UInteger>(desc.Width) * desc.Height * srcBpp,
				desc.Type == TextureType::TEXTURE_3D
					? MTL::Region::Make3D(0, 0, slice, desc.Width, desc.Height, 1)
					: MTL::Region::Make3D(0, 0, 0, desc.Width, desc.Height, 1),
				0, desc.Type == TextureType::TEXTURE_3D ? 0 : slice);

			unsigned char *dst = out.Pixels.data() + static_cast<size_t>(slice) * desc.Width * desc.Height * 4;
			const size_t texels = static_cast<size_t>(desc.Width) * desc.Height;
			for (size_t i = 0; i < texels; ++i, dst += 4)
			{
				switch (format)
				{
				case MTL::PixelFormatR8Unorm:
				{
					const unsigned char v = raw[i];
					dst[0] = v; dst[1] = v; dst[2] = v; dst[3] = 255;
					break;
				}
				case MTL::PixelFormatRGBA16Float:
				case MTL::PixelFormatRG16Float:
				case MTL::PixelFormatR16Float:
				{
					const unsigned short *p = reinterpret_cast<const unsigned short *>(raw.data() + i * srcBpp);
					const int ch = srcBpp / 2;
					dst[0] = To8(HalfToFloat(p[0]));
					dst[1] = ch > 1 ? To8(HalfToFloat(p[1])) : dst[0];
					dst[2] = ch > 2 ? To8(HalfToFloat(p[2])) : dst[0];
					dst[3] = 255;
					break;
				}
				case MTL::PixelFormatRGBA32Float:
				case MTL::PixelFormatRG32Float:
				case MTL::PixelFormatR32Float:
				{
					const float *p = reinterpret_cast<const float *>(raw.data() + i * srcBpp);
					const int ch = srcBpp / 4;
					dst[0] = To8(p[0]);
					dst[1] = ch > 1 ? To8(p[1]) : dst[0];
					dst[2] = ch > 2 ? To8(p[2]) : dst[0];
					dst[3] = 255;
					break;
				}
				case MTL::PixelFormatRGBA16Unorm:
				case MTL::PixelFormatRG16Unorm:
				case MTL::PixelFormatR16Unorm:
				{
					const unsigned short *p = reinterpret_cast<const unsigned short *>(raw.data() + i * srcBpp);
					const int ch = srcBpp / 2;
					dst[0] = static_cast<unsigned char>(p[0] >> 8);
					dst[1] = ch > 1 ? static_cast<unsigned char>(p[1] >> 8) : dst[0];
					dst[2] = ch > 2 ? static_cast<unsigned char>(p[2] >> 8) : dst[0];
					dst[3] = 255;
					break;
				}
				case MTL::PixelFormatDepth32Float:
				{
					// Depth as grayscale (dumps stay readable).
					const float v = reinterpret_cast<const float *>(raw.data() + i * srcBpp)[0];
					dst[0] = dst[1] = dst[2] = To8(v);
					dst[3] = 255;
					break;
				}
				default:
				{
					const unsigned char *p = raw.data() + i * 4;
					dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2]; dst[3] = p[3];
					if (format == MTL::PixelFormatBGRA8Unorm) { dst[0] = p[2]; dst[2] = p[0]; }
					break;
				}
				}
			}
		}
		return true;
	}

	bool RHIMetalDevice::ReadbackBackbuffer(RHIPixelData &out)
	{
		// Self-contained: capture calls land between frames (no frame pool,
		// no frame command buffer), so use the retained last-presented
		// drawable and a private pool + command buffer.
		if (m_LastPresented == nullptr)
			return false;

		NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		MTL::Texture *texture = m_LastPresented->texture();
		const NS::UInteger w = texture->width();
		const NS::UInteger h = texture->height();
		const NS::UInteger rowBytes = w * 4;

		MTL::Buffer *staging = m_Device->newBuffer(rowBytes * h, MTL::ResourceStorageModeShared);
		if (staging == nullptr)
			return false;

		MTL::CommandBuffer *commandBuffer = m_Queue->commandBuffer();
		MTL::BlitCommandEncoder *blit = commandBuffer->blitCommandEncoder();
		blit->copyFromTexture(texture, 0, 0, MTL::Origin::Make(0, 0, 0),
			MTL::Size::Make(w, h, 1), staging, 0, rowBytes, rowBytes * h);
		blit->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();

		out.Width = static_cast<int>(w);
		out.Height = static_cast<int>(h);
		out.Format = TextureFormat::RGBA8;
		out.Pixels.resize(static_cast<size_t>(w) * h * 4);
		const unsigned char *raw = static_cast<const unsigned char *>(staging->contents());
		// The drawable is BGRA and renders Metal-native (NDC-up at row 0), so
		// flip rows into the engine's GL bottom-up readback contract; the PNG
		// writer applies the final flip back.
		for (NS::UInteger y = 0; y < h; ++y)
		{
			unsigned char *dst = out.Pixels.data() + (h - 1 - y) * rowBytes;
			const unsigned char *src = raw + y * rowBytes;
			for (NS::UInteger x = 0; x < w; ++x, src += 4, dst += 4)
			{
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = src[3];
			}
		}
		staging->release();
		return true;
	}

	bool RHIMetalDevice::ReadbackPixelUInt(const RHITextureRef &texture, int x, int y, unsigned int &out)
	{
		MetalTexture *tex = static_cast<MetalTexture *>(texture.get());
		if (tex == nullptr || tex->Get() == nullptr)
			return false;
		const auto &desc = tex->GetDesc();
		if (MetalEnumMap::PixelFormat(desc.Format) != MTL::PixelFormatR32Uint)
			return false;
		if (x < 0 || y < 0 || x >= desc.Width || y >= desc.Height)
			return false;

		// GL y-up convention: flip the row.
		tex->Get()->getBytes(&out, 4, MTL::Region::Make2D(x, desc.Height - 1 - y, 1, 1), 0);
		return true;
	}
}

#endif // FURY_RHI_METAL
