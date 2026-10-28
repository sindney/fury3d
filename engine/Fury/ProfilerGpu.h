#pragma once

#include "Fury/Profiler.h"

#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)

#include <memory>

#include "Fury/RHI/RHIBackend.h"

namespace tracy
{
	class GpuCtxScope;
	class VkCtxScope;
}

namespace fury
{
	namespace profiler
	{
		void GpuContext();
		void GpuCollect();

		class GpuZoneScope
		{
			std::unique_ptr<tracy::GpuCtxScope> m_OpenGL;
#if FURY_RHI_VULKAN
			std::unique_ptr<tracy::VkCtxScope> m_Vulkan;
#endif

		public:
			GpuZoneScope(const tracy::SourceLocationData *sourceLocation, bool active);
			GpuZoneScope(unsigned int line, const char *source, size_t sourceSize,
				const char *function, size_t functionSize, const char *name, size_t nameSize, bool active);
			~GpuZoneScope();
		};
	}
}

#undef FURY_GPU_CONTEXT
#undef FURY_GPU_ZONE
#undef FURY_GPU_ZONE_DYNAMIC
#undef FURY_GPU_COLLECT

#define FURY_GPU_CONTEXT() fury::profiler::GpuContext()
#define FURY_GPU_ZONE(name) static constexpr tracy::SourceLocationData TracyConcat(__fury_gpu_source_location,TracyLine) { name, TracyFunction, TracyFile, (uint32_t)TracyLine, 0 }; fury::profiler::GpuZoneScope ___fury_gpu_zone(&TracyConcat(__fury_gpu_source_location,TracyLine), fury::profiler::Armed())
#define FURY_GPU_ZONE_DYNAMIC(name) fury::profiler::GpuZoneScope ___fury_gpu_zone(TracyLine, TracyFile, std::strlen(TracyFile), TracyFunction, std::strlen(TracyFunction), name, std::strlen(name), fury::profiler::Armed())
#define FURY_GPU_COLLECT() fury::profiler::GpuCollect()

#endif
