#ifndef _FURY_RHI_RESOURCES_H_
#define _FURY_RHI_RESOURCES_H_

#include <cstdint>

#include "Fury/RHI/RHITypes.h"

namespace fury
{
	// Base classes for RHI resources. GetNativeHandle() exposes the
	// backend's raw object (GL name, VkImage, id<MTLTexture>) for interop
	// paths that have not migrated yet (editor ImGui images, legacy binds).
	class FURY_API RHITexture
	{
	public:

		virtual ~RHITexture() = default;

		virtual const RHITextureDesc &GetDesc() const = 0;

		virtual uintptr_t GetNativeHandle() const = 0;
	};

	class FURY_API RHIBuffer
	{
	public:

		virtual ~RHIBuffer() = default;

		virtual const RHIBufferDesc &GetDesc() const = 0;

		virtual uintptr_t GetNativeHandle() const = 0;
	};

	class FURY_API RHISampler
	{
	public:

		virtual ~RHISampler() = default;

		virtual uintptr_t GetNativeHandle() const = 0;
	};

	class FURY_API RHIShaderModule
	{
	public:

		virtual ~RHIShaderModule() = default;

		// False when compile/link failed; binding such a module is a no-op.
		virtual bool IsValid() const = 0;

		virtual RHIUniformHandle ResolveUniform(const std::string &name) = 0;

		virtual int ResolveAttribLocation(const std::string &name) = 0;

		virtual uintptr_t GetNativeHandle() const = 0;
	};

	class FURY_API RHIGraphicsPipeline
	{
	public:

		virtual ~RHIGraphicsPipeline() = default;

		virtual const RHIGraphicsPipelineDesc &GetDesc() const = 0;

		virtual RHIShaderModuleRef GetShaders() const = 0;
	};

	class FURY_API RHIRenderTargetSet
	{
	public:

		virtual ~RHIRenderTargetSet() = default;

		virtual int GetWidth() const = 0;

		virtual int GetHeight() const = 0;

		virtual unsigned int GetColorCount() const = 0;

		virtual RHITextureRef GetColorTexture(unsigned int index) const = 0;

		virtual RHITextureRef GetDepthTexture() const = 0;

		// Per-cascade / per-face re-attachment (CSM, point shadows).
		// Attachment indices are color attachments in order; the depth
		// attachment is index GetColorCount().
		virtual void SetAttachmentLayer(unsigned int index, int layer) = 0;

		virtual void SetAttachmentCubeFace(unsigned int index, int face) = 0;

		virtual uintptr_t GetNativeHandle() const = 0;
	};
}

#endif // _FURY_RHI_RESOURCES_H_
