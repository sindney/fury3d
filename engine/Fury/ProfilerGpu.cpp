#include "Fury/ProfilerGpu.h"

#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)

#include "Fury/GLLoader.h"
#include <tracy/TracyOpenGL.hpp>

#include "Fury/RHI/RHIDevice.h"
#if FURY_RHI_VULKAN
#include "Fury/RHI/Vulkan/RHIVulkanDevice.h"
#include <tracy/TracyVulkan.hpp>
#endif

namespace fury
{
	namespace profiler
	{
		void GpuContext()
		{
			if (GRHIDevice != nullptr && GRHIDevice->GetBackend() == RHIBackend::VULKAN)
			{
#if FURY_RHI_VULKAN
				static_cast<RHIVulkanDevice *>(GRHIDevice)->GetTracyContext();
#endif
				return;
			}
			if (GRHIDevice == nullptr || GRHIDevice->GetBackend() == RHIBackend::OPENGL)
				TracyGpuContext;
		}

		void GpuCollect()
		{
			if (GRHIDevice == nullptr || GRHIDevice->GetBackend() == RHIBackend::OPENGL)
				TracyGpuCollect;
		}

		GpuZoneScope::GpuZoneScope(const tracy::SourceLocationData *sourceLocation, bool active)
		{
			if (!active)
				return;
			if (GRHIDevice != nullptr && GRHIDevice->GetBackend() == RHIBackend::VULKAN)
			{
#if FURY_RHI_VULKAN
				auto *device = static_cast<RHIVulkanDevice *>(GRHIDevice);
				if (TracyVkCtx context = device->GetTracyContext())
					m_Vulkan = std::make_unique<tracy::VkCtxScope>(context, sourceLocation, device->GetCommandBuffer(), active);
#endif
				return;
			}
			if (GRHIDevice == nullptr || GRHIDevice->GetBackend() == RHIBackend::OPENGL)
				m_OpenGL = std::make_unique<tracy::GpuCtxScope>(sourceLocation, active);
		}

		GpuZoneScope::GpuZoneScope(unsigned int line, const char *source, size_t sourceSize,
			const char *function, size_t functionSize, const char *name, size_t nameSize, bool active)
		{
			if (!active)
				return;
			if (GRHIDevice != nullptr && GRHIDevice->GetBackend() == RHIBackend::VULKAN)
			{
#if FURY_RHI_VULKAN
				auto *device = static_cast<RHIVulkanDevice *>(GRHIDevice);
				if (TracyVkCtx context = device->GetTracyContext())
					m_Vulkan = std::make_unique<tracy::VkCtxScope>(context, line, source, sourceSize,
						function, functionSize, name, nameSize, device->GetCommandBuffer(), active);
#endif
				return;
			}
			if (GRHIDevice == nullptr || GRHIDevice->GetBackend() == RHIBackend::OPENGL)
			{
				// Tracy stubs out its OpenGL GPU zones on Apple, and the stub
				// has no source-location constructor.
#if !defined(TRACY_OPENGL_DISABLE)
				m_OpenGL = std::make_unique<tracy::GpuCtxScope>(line, source, sourceSize,
					function, functionSize, name, nameSize, active);
#endif
			}
		}

		GpuZoneScope::~GpuZoneScope() = default;
	}
}

#endif
