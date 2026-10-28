#ifndef _FURY_GL_RESOURCES_H_
#define _FURY_GL_RESOURCES_H_

#include "Fury/RHI/RHIResources.h"

namespace fury
{
	struct RHIPixelData;

	class FURY_API GLTexture : public RHITexture
	{
	protected:

		RHITextureDesc m_Desc;

		unsigned int m_ID = 0;

		unsigned int m_Target = 0;

	public:

		// Allocates immutable storage and applies the sampler state.
		// Returns nullptr when the format is unusable (unsupported BC, ...).
		static std::shared_ptr<GLTexture> Create(const RHITextureDesc &desc);

		~GLTexture();

		const RHITextureDesc &GetDesc() const override;

		uintptr_t GetNativeHandle() const override;

		unsigned int GetID() const;

		unsigned int GetTarget() const;

		// Uncompressed u8 upload. layer >= 0 selects an array layer / 3D
		// slice; -1 uploads the whole level (all depth for 3D/arrays).
		void Upload(const void *data, size_t size, int mipLevel, int layer);

		// Pre-blocked BC upload for one mip level.
		void UploadCompressed(const void *data, size_t size, int mipLevel);

		void GenerateMipmaps();

		void ApplySamplerState(const RHISamplerDesc &sampler);

		void SetSamplerDesc(const RHISamplerDesc &sampler) { m_Desc.Sampler = sampler; }

		bool ReadPixels(RHIPixelData &out) const;
	};

	typedef std::shared_ptr<GLTexture> GLTextureRef;

	class FURY_API GLBuffer : public RHIBuffer
	{
	protected:

		RHIBufferDesc m_Desc;

		unsigned int m_ID = 0;

		unsigned int m_Target = 0;

	public:

		static std::shared_ptr<GLBuffer> Create(const RHIBufferDesc &desc, const void *initialData);

		~GLBuffer();

		const RHIBufferDesc &GetDesc() const override;

		uintptr_t GetNativeHandle() const override;

		unsigned int GetID() const;

		unsigned int GetTarget() const;

		void Update(const void *data, size_t size, size_t offset);
	};

	typedef std::shared_ptr<GLBuffer> GLBufferRef;

	class FURY_API GLRenderTargetSet : public RHIRenderTargetSet
	{
	protected:

		RHIRenderTargetSetDesc m_Desc;

		std::vector<RHITextureRef> m_Colors;

		RHITextureRef m_Depth;

		unsigned int m_FBO = 0;

		int m_Width = 0;

		int m_Height = 0;

	public:

		static std::shared_ptr<GLRenderTargetSet> Create(const RHIRenderTargetSetDesc &desc);

		~GLRenderTargetSet();

		int GetWidth() const override;

		int GetHeight() const override;

		unsigned int GetColorCount() const override;

		RHITextureRef GetColorTexture(unsigned int index) const override;

		RHITextureRef GetDepthTexture() const override;

		uintptr_t GetNativeHandle() const override;

		unsigned int GetFBO() const;

		// Per-cascade / per-face re-attachment (CSM, point shadows).
		void SetAttachmentLayer(unsigned int index, int layer) override;

		void SetAttachmentCubeFace(unsigned int index, int face) override;

		// Clears every layer/face of layered attachments (plain glClear only
		// covers the attached layer, leaving siblings undefined).
		void ClearAllLayers(ClearMode mode, const Color &color);

		bool HasLayeredAttachments() const;

	protected:

		void Attach();
	};

	typedef std::shared_ptr<GLRenderTargetSet> GLRenderTargetSetRef;
}

#endif // _FURY_GL_RESOURCES_H_
