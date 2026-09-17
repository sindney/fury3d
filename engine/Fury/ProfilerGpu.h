#pragma once

// GPU-zone companion to Profiler.h. Only TUs that emit GL GPU zones include
// this (everything else takes Profiler.h's no-op GPU macros). GLLoader must
// come first: TracyOpenGL.hpp calls GL entry points (glQueryCounter,
// glGetQueryObjectui64v) directly.
//
// CMake still passes FURY_TRACY_GPU (FURY_WITH_TRACY_GPU) to gate the zones.

#include "Fury/Profiler.h"

#if defined(TRACY_ENABLE) && defined(FURY_TRACY_GPU)

#include "Fury/GLLoader.h"
#include <tracy/TracyOpenGL.hpp>

#undef FURY_GPU_CONTEXT
#undef FURY_GPU_ZONE
#undef FURY_GPU_ZONE_DYNAMIC
#undef FURY_GPU_COLLECT

#define FURY_GPU_CONTEXT() TracyGpuContext
#define FURY_GPU_ZONE(name) TracyGpuNamedZone(___fury_gpu_zone, name, fury::profiler::Armed())
#define FURY_GPU_ZONE_DYNAMIC(name) TracyGpuZoneTransient(___fury_gpu_zone, name, fury::profiler::Armed())
#define FURY_GPU_COLLECT() TracyGpuCollect

#endif
