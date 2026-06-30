#include "Fury/RenderTarget.h"

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Texture.h"

namespace fury
{
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

		if (m_FBO != 0 && width == m_Width && height == m_Height)
			return true;

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

		glGenFramebuffers(1, &m_FBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, m_Color->GetID(), 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_TEXTURE_2D, m_Depth->GetID(), 0);

		GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
		glDrawBuffers(1, drawBufs);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			FURYE << "RenderTarget '" << m_Name << "' FBO incomplete, status=0x" << std::hex << status;
			Release();
			return false;
		}

		m_Width = width;
		m_Height = height;
		return true;
	}

	void RenderTarget::Release()
	{
		if (m_FBO != 0)
		{
			glDeleteFramebuffers(1, &m_FBO);
			m_FBO = 0;
		}
		m_Color.reset();
		m_Depth.reset();
		m_Width = 0;
		m_Height = 0;
	}
}
