#include "Fury/RHI/Metal/MetalResources.h"

#if FURY_RHI_METAL

#include <cstring>

#include "Fury/Log.h"
#include "Fury/RHI/Metal/MetalEnumMap.h"

namespace fury
{
	namespace
	{
		// IEEE 754 binary32 -> binary16, round to nearest even.
		unsigned short FloatToHalf(float value)
		{
			unsigned int bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));
			const unsigned int sign = (bits >> 16) & 0x8000u;
			const unsigned int rawExponent = (bits >> 23) & 0xFFu;
			const int exponent = static_cast<int>(rawExponent) - 127 + 15;
			unsigned int mantissa = bits & 0x007FFFFFu;
			if (rawExponent == 0xFFu)
				return static_cast<unsigned short>(sign | 0x7C00u | (mantissa != 0 ? 0x0200u : 0u));
			if (exponent >= 31)
				return static_cast<unsigned short>(sign | 0x7C00u);
			if (exponent <= 0)
			{
				if (exponent < -10)
					return static_cast<unsigned short>(sign);
				mantissa |= 0x00800000u;
				const unsigned int shift = static_cast<unsigned int>(14 - exponent);
				unsigned int result = mantissa >> shift;
				const unsigned int rest = mantissa & ((1u << shift) - 1u);
				const unsigned int halfway = 1u << (shift - 1);
				if (rest > halfway || (rest == halfway && (result & 1u)))
					++result;
				return static_cast<unsigned short>(sign | result);
			}
			unsigned int result = sign | (static_cast<unsigned int>(exponent) << 10) | (mantissa >> 13);
			const unsigned int rest = mantissa & 0x1FFFu;
			if (rest > 0x1000u || (rest == 0x1000u && (result & 1u)))
				++result;
			return static_cast<unsigned short>(result);
		}
	}

	std::shared_ptr<MetalTexture> MetalTexture::Create(MTL::Device *device, const RHITextureDesc &desc)
	{
		const MTL::PixelFormat format = MetalEnumMap::PixelFormat(desc.Format);
		if (device == nullptr || format == MTL::PixelFormatInvalid || desc.Width <= 0 || desc.Height <= 0)
			return nullptr;

		NS::SharedPtr<MTL::TextureDescriptor> td = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
		td->setTextureType(MetalEnumMap::MTLTextureType(desc.Type));
		td->setPixelFormat(format);
		td->setWidth(static_cast<NS::UInteger>(desc.Width));
		td->setHeight(static_cast<NS::UInteger>(desc.Height));
		td->setDepth(static_cast<NS::UInteger>(desc.Type == TextureType::TEXTURE_3D ? desc.Depth : 1));
		td->setArrayLength(static_cast<NS::UInteger>(desc.Type == TextureType::TEXTURE_2D_ARRAY ? desc.Depth : 1));
		// MipLevels is the literal level count (GL allocates it verbatim);
		// 0 is meaningless, floor at 1.
		const unsigned int mipCount = desc.MipLevels > 0 ? static_cast<unsigned int>(desc.MipLevels) : 1;
		td->setMipmapLevelCount(mipCount);
		td->setUsage(desc.RenderTarget
			? MTL::TextureUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead)
			: MTL::TextureUsageShaderRead);
		// Shared (CPU+GPU coherent) on Apple silicon Macs: uploads are plain
		// replaceRegion calls and readbacks need no staging copy helper.
		td->setStorageMode(MTL::StorageModeShared);
		td->setCpuCacheMode(MTL::CPUCacheModeDefaultCache);

		MTL::Texture *texture = device->newTexture(td.get());
		if (texture == nullptr)
		{
			FURYE << "MetalTexture: newTexture failed for '" << desc.Name << "'";
			return nullptr;
		}

		auto out = std::make_shared<MetalTexture>();
		out->m_Desc = desc;
		out->m_Texture = texture;
		out->ApplySampler(device, desc.Sampler);
		return out;
	}

	MetalTexture::~MetalTexture()
	{
		if (m_Sampler != nullptr) m_Sampler->release();
		if (m_Texture != nullptr) m_Texture->release();
	}

	const RHITextureDesc &MetalTexture::GetDesc() const
	{
		return m_Desc;
	}

	uintptr_t MetalTexture::GetNativeHandle() const
	{
		return reinterpret_cast<uintptr_t>(m_Texture);
	}

	void MetalTexture::Upload(const void *data, size_t size, int mipLevel, int layer)
	{
		// Raw byte copy: the data must already match the texture's layout
		// (float32 sources go through UploadFloat32).
		if (m_Texture == nullptr || data == nullptr || size == 0)
			return;
		if (mipLevel < 0 || mipLevel >= static_cast<int>(m_Texture->mipmapLevelCount()))
		{
			FURYW << "MetalTexture::Upload: '" << m_Desc.Name << "' mip " << mipLevel
				<< " out of range (" << m_Texture->mipmapLevelCount() << " levels); skipped";
			return;
		}

		// CPU-side expansion to 4 channels for 3-channel formats.
		std::vector<unsigned char> expanded;
		const TextureFormat expandedFormat = MetalEnumMap::ExpandTo4Channels(m_Desc.Format);
		if (expandedFormat != m_Desc.Format)
		{
			const unsigned int srcBpp = MetalEnumMap::BytesPerBlock(m_Desc.Format) * 3 / 4;
			const unsigned int dstBpp = MetalEnumMap::BytesPerBlock(expandedFormat);
			if (srcBpp == 0 || dstBpp == 0)
				return;
			const size_t texelCount = size / srcBpp;
			expanded.resize(texelCount * dstBpp);
			const unsigned char *src = static_cast<const unsigned char *>(data);
			unsigned char *dst = expanded.data();
			for (size_t i = 0; i < texelCount; ++i, src += srcBpp, dst += dstBpp)
			{
				std::memcpy(dst, src, srcBpp);
				// Opaque alpha for the added channel (max per storage type).
				switch (dstBpp - srcBpp)
				{
				case 1: dst[srcBpp] = 255; break;
				case 2: dst[srcBpp] = 0xFF; dst[srcBpp + 1] = 0xFF; break;
				case 4:
					if (expandedFormat == TextureFormat::RGBA32F)
					{
						const float one = 1.0f;
						std::memcpy(dst + srcBpp, &one, 4);
					}
					else
					{
						dst[srcBpp] = 0xFF; dst[srcBpp + 1] = 0xFF;
						dst[srcBpp + 2] = 0xFF; dst[srcBpp + 3] = 0xFF;
					}
					break;
				default: break;
				}
			}
			data = expanded.data();
			size = expanded.size();
		}

		UploadNative(data, mipLevel, layer);
	}

	void MetalTexture::UploadFloat32(const float *data, size_t componentCount, int mipLevel, int layer)
	{
		if (m_Texture == nullptr || data == nullptr || componentCount == 0)
			return;
		if (mipLevel < 0 || mipLevel >= static_cast<int>(m_Texture->mipmapLevelCount()))
		{
			FURYW << "MetalTexture::UploadFloat32: '" << m_Desc.Name << "' mip " << mipLevel
				<< " out of range (" << m_Texture->mipmapLevelCount() << " levels); skipped";
			return;
		}

		const unsigned int channels = MetalEnumMap::ChannelCount(m_Desc.Format);
		const unsigned int componentBytes = MetalEnumMap::FloatComponentBytes(m_Desc.Format);
		if (channels == 0 || componentBytes == 0)
			return;

		unsigned int w = static_cast<unsigned int>(m_Desc.Width) >> mipLevel;
		unsigned int h = static_cast<unsigned int>(m_Desc.Height) >> mipLevel;
		if (w == 0) w = 1;
		if (h == 0) h = 1;
		const unsigned int sliceCount = (layer >= 0 || m_Desc.Type == TextureType::TEXTURE_2D)
			? 1u : static_cast<unsigned int>(std::max(m_Desc.Depth, 1));
		const size_t texelCount = static_cast<size_t>(w) * h * sliceCount;
		if (componentCount != texelCount * channels)
		{
			FURYW << "MetalTexture::UploadFloat32: '" << m_Desc.Name << "' got " << componentCount
				<< " components, expected " << texelCount * channels;
			return;
		}

		// Metal has no 3-channel float format: the spare channel is opaque.
		const unsigned int storageChannels = channels == 3 ? 4u : channels;
		std::vector<unsigned char> bytes(texelCount * storageChannels * componentBytes);
		for (size_t t = 0; t < texelCount; t++)
		{
			const float *src = data + t * channels;
			unsigned char *dst = bytes.data() + t * storageChannels * componentBytes;
			for (unsigned int c = 0; c < channels; c++)
			{
				if (componentBytes == 2)
				{
					const unsigned short half = FloatToHalf(src[c]);
					std::memcpy(dst + c * 2, &half, 2);
				}
				else
				{
					std::memcpy(dst + c * 4, &src[c], 4);
				}
			}
			if (storageChannels > channels)
			{
				if (componentBytes == 2)
				{
					const unsigned short one = 0x3C00;
					std::memcpy(dst + channels * 2, &one, 2);
				}
				else
				{
					const float one = 1.0f;
					std::memcpy(dst + channels * 4, &one, 4);
				}
			}
		}

		UploadNative(bytes.data(), mipLevel, layer);
	}

	void MetalTexture::UploadNative(const void *data, int mipLevel, int layer)
	{
		unsigned int w = static_cast<unsigned int>(m_Desc.Width) >> mipLevel;
		unsigned int h = static_cast<unsigned int>(m_Desc.Height) >> mipLevel;
		unsigned int d = (m_Desc.Type == TextureType::TEXTURE_3D ? static_cast<unsigned int>(m_Desc.Depth) : 1) >> mipLevel;
		if (w == 0) w = 1;
		if (h == 0) h = 1;
		if (d == 0) d = 1;

		const unsigned int bpp = MetalEnumMap::BytesPerBlock(m_Desc.Format);
		const NS::UInteger bytesPerRow = w * bpp;
		const NS::UInteger bytesPerImage = bytesPerRow * h;
		if (m_Desc.Type == TextureType::TEXTURE_3D)
		{
			const NS::UInteger z = layer >= 0 ? static_cast<NS::UInteger>(layer) : 0;
			const NS::UInteger depth = layer >= 0 ? 1 : d;
			m_Texture->replaceRegion(MTL::Region::Make3D(0, 0, z, w, h, depth),
				static_cast<NS::UInteger>(mipLevel), 0, data, bytesPerRow, bytesPerImage);
			return;
		}

		// 2D / array / cube: one replaceRegion per slice. layer >= 0 selects a
		// single slice; -1 covers desc.Depth slices (GL parity: a lone cube
		// face upload has Depth 1 and writes exactly one face).
		NS::UInteger firstSlice = 0;
		NS::UInteger sliceCount = m_Desc.Type == TextureType::TEXTURE_2D
			? 1 : static_cast<NS::UInteger>(std::max(m_Desc.Depth, 1));
		if (layer >= 0)
		{
			firstSlice = static_cast<NS::UInteger>(layer);
			sliceCount = 1;
		}
		const unsigned char *src = static_cast<const unsigned char *>(data);
		for (NS::UInteger slice = 0; slice < sliceCount; ++slice)
		{
			m_Texture->replaceRegion(MTL::Region::Make3D(0, 0, 0, w, h, 1),
				static_cast<NS::UInteger>(mipLevel), firstSlice + slice,
				src + slice * bytesPerImage, bytesPerRow, bytesPerImage);
		}
	}

	void MetalTexture::UploadCompressed(const void *data, size_t size, int mipLevel)
	{
		if (m_Texture == nullptr || data == nullptr || size == 0)
			return;
		if (mipLevel < 0 || mipLevel >= static_cast<int>(m_Texture->mipmapLevelCount()))
		{
			FURYW << "MetalTexture::UploadCompressed: '" << m_Desc.Name << "' mip " << mipLevel
				<< " out of range (" << m_Texture->mipmapLevelCount() << " levels); skipped";
			return;
		}

		unsigned int w = static_cast<unsigned int>(m_Desc.Width) >> mipLevel;
		unsigned int h = static_cast<unsigned int>(m_Desc.Height) >> mipLevel;
		if (w == 0) w = 1;
		if (h == 0) h = 1;

		const unsigned int edge = MetalEnumMap::BlockEdge(m_Desc.Format);
		const unsigned int bpp = MetalEnumMap::BytesPerBlock(m_Desc.Format);
		const NS::UInteger bytesPerRow = ((w + edge - 1) / edge) * bpp;
		const NS::UInteger bytesPerImage = bytesPerRow * ((h + edge - 1) / edge);
		m_Texture->replaceRegion(MTL::Region::Make3D(0, 0, 0, w, h, 1),
			static_cast<NS::UInteger>(mipLevel), 0, data, bytesPerRow, bytesPerImage);
	}

	void MetalTexture::ApplySampler(MTL::Device *device, const RHISamplerDesc &sampler)
	{
		NS::SharedPtr<MTL::SamplerDescriptor> sd = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());
		sd->setMinFilter(MetalEnumMap::SamplerFilter(sampler.Filter));
		sd->setMagFilter(MetalEnumMap::SamplerFilter(sampler.Filter));
		sd->setMipFilter(MetalEnumMap::SamplerMipFilter(sampler.Filter));
		sd->setSAddressMode(MetalEnumMap::SamplerWrap(sampler.Wrap));
		sd->setTAddressMode(MetalEnumMap::SamplerWrap(sampler.Wrap));
		sd->setRAddressMode(MetalEnumMap::SamplerWrap(sampler.Wrap));
		if (sampler.ShadowCompare)
			sd->setCompareFunction(MTL::CompareFunctionLess);
		if (sampler.Wrap == WrapMode::CLAMP_TO_BORDER)
		{
			// Metal offers three fixed border colors; snap to the closest.
			// Shadow maps use white: out-of-range depth reads 1.0 = lit.
			const Color &bc = sampler.BorderColor;
			MTL::SamplerBorderColor border = MTL::SamplerBorderColorTransparentBlack;
			if (bc.r >= 0.5f && bc.g >= 0.5f && bc.b >= 0.5f && bc.a >= 0.5f)
				border = MTL::SamplerBorderColorOpaqueWhite;
			else if (bc.a >= 0.5f)
				border = MTL::SamplerBorderColorOpaqueBlack;
			sd->setBorderColor(border);
		}

		MTL::SamplerState *state = device->newSamplerState(sd.get());
		if (state == nullptr)
			FURYE << "MetalTexture: newSamplerState failed for '" << m_Desc.Name << "'";
		if (m_Sampler != nullptr) m_Sampler->release();
		m_Sampler = state;
		m_Desc.Sampler = sampler;
	}

	std::shared_ptr<MetalBuffer> MetalBuffer::Create(MTL::Device *device, const RHIBufferDesc &desc, const void *initialData)
	{
		if (device == nullptr)
			return nullptr;

		auto out = std::make_shared<MetalBuffer>();
		out->m_Desc = desc;
		// DYNAMIC buffers start empty and grow via Update; floor the
		// allocation at 1 (newBufferWithLength:0 is not useful).
		out->m_Buffer = device->newBuffer(desc.Size > 0 ? desc.Size : 1, MTL::ResourceStorageModeShared);
		if (out->m_Buffer == nullptr)
		{
			FURYE << "MetalBuffer: newBuffer failed for '" << desc.Name << "'";
			return nullptr;
		}
		if (initialData != nullptr && desc.Size > 0)
			std::memcpy(out->m_Buffer->contents(), initialData, desc.Size);
		return out;
	}

	MetalBuffer::~MetalBuffer()
	{
		if (m_Buffer != nullptr) m_Buffer->release();
	}

	const RHIBufferDesc &MetalBuffer::GetDesc() const
	{
		return m_Desc;
	}

	uintptr_t MetalBuffer::GetNativeHandle() const
	{
		return reinterpret_cast<uintptr_t>(m_Buffer);
	}

	void MetalBuffer::Update(MTL::Device *device, const void *data, size_t size, size_t offset)
	{
		if (m_Buffer == nullptr || data == nullptr || size == 0)
			return;

		// Streaming full writes (DYNAMIC, offset 0) take a fresh allocation
		// per call: the GPU executes this frame's draws at commit time, so
		// an in-place write would let every earlier draw of the frame read
		// the FINAL contents (GL's buffer orphaning semantics). In-flight
		// command buffers retain the old allocation until completion.
		const bool streamWrite = offset == 0 && m_Desc.Usage == RHIBufferUsage::DYNAMIC;
		if (offset + size > m_Desc.Size || streamWrite)
		{
			MTL::Buffer *next = device->newBuffer(offset + size, MTL::ResourceStorageModeShared);
			if (next == nullptr)
			{
				FURYE << "MetalBuffer: realloc failed for '" << m_Desc.Name << "'";
				return;
			}
			m_Buffer->release();
			m_Buffer = next;
			m_Desc.Size = offset + size;
		}
		std::memcpy(static_cast<unsigned char *>(m_Buffer->contents()) + offset, data, size);
	}

	std::shared_ptr<MetalRenderTargetSet> MetalRenderTargetSet::Create(const RHIRenderTargetSetDesc &desc)
	{
		if (desc.Colors.empty() && !desc.HasDepth)
		{
			FURYE << "MetalRenderTargetSet: empty attachment list";
			return nullptr;
		}

		auto out = std::make_shared<MetalRenderTargetSet>();
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
				FURYE << "MetalRenderTargetSet: null color attachment";
				return nullptr;
			}
			out->m_Colors.push_back(state);
		}

		if (desc.HasDepth && !adopt(desc.Depth, out->m_Depth))
		{
			FURYE << "MetalRenderTargetSet: null depth attachment";
			return nullptr;
		}

		const RHITextureRef &extentTex = out->m_Colors.empty() ? out->m_Depth.Texture : out->m_Colors[0].Texture;
		out->m_Width = extentTex->GetDesc().Width;
		out->m_Height = extentTex->GetDesc().Height;
		for (const auto &state : out->m_Colors)
		{
			if (state.Texture->GetDesc().Width != out->m_Width || state.Texture->GetDesc().Height != out->m_Height)
			{
				FURYE << "MetalRenderTargetSet: attachment size mismatch";
				return nullptr;
			}
		}
		return out;
	}

	int MetalRenderTargetSet::GetWidth() const { return m_Width; }

	int MetalRenderTargetSet::GetHeight() const { return m_Height; }

	unsigned int MetalRenderTargetSet::GetColorCount() const
	{
		return static_cast<unsigned int>(m_Colors.size());
	}

	RHITextureRef MetalRenderTargetSet::GetColorTexture(unsigned int index) const
	{
		return index < m_Colors.size() ? m_Colors[index].Texture : nullptr;
	}

	RHITextureRef MetalRenderTargetSet::GetDepthTexture() const
	{
		return m_HasDepth ? m_Depth.Texture : nullptr;
	}

	void MetalRenderTargetSet::SetAttachmentLayer(unsigned int index, int layer)
	{
		// index == GetColorCount() addresses the depth attachment (GL parity).
		if (index >= m_Colors.size())
			m_Depth.Layer = layer;
		else
			m_Colors[index].Layer = layer;
	}

	void MetalRenderTargetSet::SetAttachmentCubeFace(unsigned int index, int face)
	{
		if (index >= m_Colors.size())
			m_Depth.CubeFace = face;
		else
			m_Colors[index].CubeFace = face;
	}

	void MetalRenderTargetSet::ResolveSlice(const AttachmentState &state, TextureType type, unsigned int &outLevel, unsigned int &outSlice)
	{
		outLevel = static_cast<unsigned int>(state.MipLevel);
		if (type == TextureType::TEXTURE_CUBE_MAP)
			outSlice = state.CubeFace >= 0 ? static_cast<unsigned int>(state.CubeFace) : 0;
		else if (state.Layer >= 0)
			outSlice = static_cast<unsigned int>(state.Layer);
		else
			outSlice = 0;
	}
}

#endif // FURY_RHI_METAL
