#pragma once

// Tracy profiler wrapper. Engine code includes ONLY this header - never
// <tracy/...> directly. When the build lacks TRACY_ENABLE (CMake option
// FURY_WITH_TRACY=OFF, the default), every macro expands to nothing and no
// Tracy code is compiled. FURY_TRACY_GPU (CMake FURY_WITH_TRACY_GPU) gates
// the OpenGL GPU zones separately so flaky drivers can opt out.
//
// Runtime control: Tracy's Startup/ShutdownProfiler (manual lifetime) is
// NOT safe here - zones after shutdown dereference freed state. Instead a
// process-wide atomic `armed` flag feeds every zone's `active` parameter;
// a disarmed zone short-circuits before touching Tracy or GL state, even
// when a client is connected (FURY_TRACY=0 / editor Tracy=0|1 / Lua
// Engine.SetTracyEnabled).

#ifdef TRACY_ENABLE

#include <atomic>
#include <cstring>

#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>

namespace fury
{
	namespace profiler
	{
		inline std::atomic<bool>& ArmedFlag() { static std::atomic<bool> f{true}; return f; }
		inline bool Armed() { return ArmedFlag().load(std::memory_order_relaxed); }
		inline void SetArmed(bool v) { ArmedFlag().store(v, std::memory_order_relaxed); }
	}
}

#define FURY_ZONE ZoneNamed(___tracy_scoped_zone, fury::profiler::Armed())
#define FURY_ZONE_NAMED(name) ZoneNamedN(___tracy_scoped_zone, name, fury::profiler::Armed())
// Two statements by design (Tracy's own ZoneScoped + ZoneName pattern);
// use at block scope, never braceless under an if. ZoneName no-ops when
// the zone is inactive.
#define FURY_ZONE_DYNAMIC(name) ZoneNamed(___tracy_scoped_zone, fury::profiler::Armed()); ZoneName(name, std::strlen(name))
#define FURY_FRAME if (!fury::profiler::Armed()) {} else FrameMark
#define FURY_SET_THREAD_NAME(name) tracy::SetThreadName(name)

#ifdef FURY_TRACY_GPU
#define FURY_GPU_CONTEXT() TracyGpuContext
#define FURY_GPU_ZONE(name) TracyGpuNamedZone(___fury_gpu_zone, name, fury::profiler::Armed())
#define FURY_GPU_ZONE_DYNAMIC(name) TracyGpuZoneTransient(___fury_gpu_zone, name, fury::profiler::Armed())
#define FURY_GPU_COLLECT() TracyGpuCollect
#else
#define FURY_GPU_CONTEXT()
#define FURY_GPU_ZONE(name)
#define FURY_GPU_ZONE_DYNAMIC(name)
#define FURY_GPU_COLLECT()
#endif

#else

#define FURY_ZONE
#define FURY_ZONE_NAMED(name)
#define FURY_ZONE_DYNAMIC(name)
#define FURY_FRAME
#define FURY_SET_THREAD_NAME(name)
#define FURY_GPU_CONTEXT()
#define FURY_GPU_ZONE(name)
#define FURY_GPU_ZONE_DYNAMIC(name)
#define FURY_GPU_COLLECT()

#endif
