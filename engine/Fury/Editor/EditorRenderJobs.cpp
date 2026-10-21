#if WITH_EDITOR

#include "Fury/Editor/EditorRenderJobs.h"

#include "Fury/EnumUtil.h"
#include "Fury/Log.h"
#include "Fury/RenderThread.h"
#include "Fury/Texture.h"
#include "Fury/RHI/RHIDevice.h"

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
			if (surface.targetSet != nullptr && surface.w == w && surface.h == h)
				return surface;

			surface.targetSet = nullptr;
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

			RHIRenderTargetSetDesc desc;
			RHIRenderTargetAttachment colorAt;
			colorAt.Texture = surface.color->GetRHITexture();
			desc.Colors.push_back(colorAt);
			RHIRenderTargetAttachment depthAt;
			depthAt.Texture = surface.depth->GetRHITexture();
			desc.Depth = depthAt;
			desc.HasDepth = true;
			surface.targetSet = GRHIDevice->CreateRenderTargetSet(desc);
			if (surface.targetSet == nullptr)
			{
				FURYW << "Editor offscreen surface '" << key << "' incomplete";
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

		uintptr_t DisplayTextureId(const std::string &key)
		{
			std::lock_guard<std::mutex> mapLock(g_SlotMapMutex);
			auto it = g_DisplaySlots.find(key);
			if (it == g_DisplaySlots.end())
				return 0;
			std::lock_guard<std::mutex> slotLock(it->second.mutex);
			return it->second.color ? it->second.color->GetUiTextureId() : 0;
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
