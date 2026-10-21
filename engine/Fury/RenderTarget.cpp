#include "Fury/RenderTarget.h"
#include "Fury/RenderThread.h"

#include "Fury/Log.h"
#include "Fury/Texture.h"
#include "Fury/RHI/RHIDevice.h"

namespace fury
{
	namespace
	{
		RHIRenderTargetSetRef CreateTargetSet(const std::shared_ptr<Texture> &color, const std::shared_ptr<Texture> &depth)
		{
			RHIRenderTargetSetDesc desc;

			RHIRenderTargetAttachment colorAttach;
			colorAttach.Texture = color->GetRHITexture();
			desc.Colors.push_back(colorAttach);

			RHIRenderTargetAttachment depthAttach;
			depthAttach.Texture = depth->GetRHITexture();
			desc.Depth = depthAttach;
			desc.HasDepth = true;

			return GRHIDevice->CreateRenderTargetSet(desc);
		}
	}

	RenderTarget::Ptr RenderTarget::Create(const std::string &name)
	{
		return std::make_shared<RenderTarget>(name);
	}

	RenderTarget::RenderTarget(const std::string &name)
		: m_Name(name)
	{
	}

	RenderTarget::~RenderTarget()
	{
		Release();
	}

	bool RenderTarget::Resize(int width, int height)
	{
		if (width <= 0 || height <= 0)
			return false;

		if (m_TargetSet != nullptr && width == m_Width && height == m_Height)
			return true;

		// Foreign-thread callers (editor game thread): rebuild the CPU
		// side now (texture objects exist immediately and self-marshal
		// their GL uploads), then build the target set on the GL thread
		// after those uploads land (FIFO job order guarantees it).
		if (!RenderThread::Get().MayUseGL())
		{
			if (m_TargetPending.load() && width == m_Width && height == m_Height)
				return true;

			Release();
			m_Color = Texture::Create(m_Name + "_color");
			m_Color->SetFilterMode(FilterMode::LINEAR);
			m_Color->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
			m_Color->CreateEmpty(width, height, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D, false);
			m_Depth = Texture::Create(m_Name + "_depth");
			m_Depth->SetFilterMode(FilterMode::NEAREST);
			m_Depth->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
			m_Depth->CreateEmpty(width, height, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D, false);
			m_Width = width;
			m_Height = height;
			m_TargetPending.store(true);

			// RenderTarget is owned by the editor viewport static (process
			// lifetime); textures ride as shared_ptr captures.
			auto color = m_Color;
			auto depth = m_Depth;
			RenderThread::Get().EnqueueJob([this, color, depth]() {
				if (GRHIDevice != nullptr)
					m_TargetSet = CreateTargetSet(color, depth);
				m_TargetPending.store(false);
			});
			return true;
		}
		FURY_RHI_THREAD_GUARD();
		// Tear down any existing allocation so we can rebuild at the
		// new size. Texture::CreateEmpty re-uploads at the new dims.
		Release();

		m_Color = Texture::Create(m_Name + "_color");
		m_Color->SetFilterMode(FilterMode::LINEAR);
		m_Color->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
		// Non-sRGB (RGBA8): the lambert final shader gamma-encodes its
		// output via the u_gamma_correct uniform when rendering to this
		// RT, so ImGui::Image can sample and pass through the sRGB-encoded
		// values without double-decode. (An sRGB-texture RT would
		// auto-decode on ImGui sample and render too dark.)
		m_Color->CreateEmpty(width, height, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D, false);

		m_Depth = Texture::Create(m_Name + "_depth");
		m_Depth->SetFilterMode(FilterMode::NEAREST);
		m_Depth->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
		m_Depth->CreateEmpty(width, height, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D, false);

		if (GRHIDevice != nullptr)
		{
			m_TargetSet = CreateTargetSet(m_Color, m_Depth);
			if (m_TargetSet == nullptr)
			{
				FURYE << "RenderTarget '" << m_Name << "' target set creation failed";
				Release();
				return false;
			}
		}

		m_Width = width;
		m_Height = height;
		return true;
	}

	void RenderTarget::Release()
	{
		if (m_TargetSet != nullptr)
		{
			RHIRenderTargetSetRef set = std::move(m_TargetSet);
			RenderThread::Get().EnqueueJob([set]() mutable { set.reset(); });
		}
		m_Color.reset();
		m_Depth.reset();
		m_Width = 0;
		m_Height = 0;
	}
}
