#if WITH_EDITOR

#include "Fury/Editor/EditorRenderJobs.h"

#include "Fury/EnumUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/RenderThread.h"
#include "Fury/Texture.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			// GL-thread-owned: surfaces are created/resized/used here only.
			std::unordered_map<std::string, OffscreenSurface> g_Surfaces;

			std::unordered_map<std::string, DisplaySlot> g_DisplaySlots;

			std::mutex g_SlotMapMutex;
		}

		OffscreenSurface &AcquireSurface(const std::string &key, int w, int h)
		{
			ASSERT_MSG(RenderThread::Get().MayUseGL(), "AcquireSurface off the GL thread");
			auto &surface = g_Surfaces[key];
			if (surface.fbo != 0 && surface.w == w && surface.h == h)
				return surface;

			if (surface.fbo != 0)
			{
				unsigned int fbo = surface.fbo;
				RenderThread::Get().EnqueueJob([fbo]() { glDeleteFramebuffers(1, &fbo); });
				surface.fbo = 0;
			}
			surface.color.reset();
			surface.depth.reset();

			surface.color = Texture::Create("EditorOffscreen_" + key + "_color");
			surface.color->SetFilterMode(FilterMode::LINEAR);
			surface.color->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
			surface.color->CreateEmpty(w, h, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D, false);

			surface.depth = Texture::Create("EditorOffscreen_" + key + "_depth");
			surface.depth->SetFilterMode(FilterMode::NEAREST);
			surface.depth->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
			surface.depth->CreateEmpty(w, h, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D, false);

			glGenFramebuffers(1, &surface.fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, surface.fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_2D, surface.color->GetID(), 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				GL_TEXTURE_2D, surface.depth->GetID(), 0);
			GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
			glDrawBuffers(1, drawBufs);
			GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			if (status != GL_FRAMEBUFFER_COMPLETE)
			{
				FURYW << "Editor offscreen surface '" << key << "' incomplete, status=0x"
					<< std::hex << status;
				unsigned int fbo = surface.fbo;
				RenderThread::Get().EnqueueJob([fbo]() { glDeleteFramebuffers(1, &fbo); });
				surface.fbo = 0;
				surface.w = surface.h = 0;
				return surface;
			}

			surface.w = w;
			surface.h = h;
			return surface;
		}

		void PublishSurface(const std::string &key, const std::shared_ptr<Texture> &color)
		{
			std::lock_guard<std::mutex> mapLock(g_SlotMapMutex);
			auto &slot = g_DisplaySlots[key];
			std::lock_guard<std::mutex> slotLock(slot.mutex);
			slot.color = color;
			++slot.seq;
		}

		unsigned int DisplayTextureId(const std::string &key)
		{
			std::lock_guard<std::mutex> mapLock(g_SlotMapMutex);
			auto it = g_DisplaySlots.find(key);
			if (it == g_DisplaySlots.end())
				return 0;
			std::lock_guard<std::mutex> slotLock(it->second.mutex);
			return it->second.color ? it->second.color->GetID() : 0;
		}

		void ResetDisplaySlot(const std::string &key)
		{
			std::lock_guard<std::mutex> mapLock(g_SlotMapMutex);
			auto it = g_DisplaySlots.find(key);
			if (it == g_DisplaySlots.end())
				return;
			std::lock_guard<std::mutex> slotLock(it->second.mutex);
			it->second.color.reset();
		}
	}
}

#endif // WITH_EDITOR
