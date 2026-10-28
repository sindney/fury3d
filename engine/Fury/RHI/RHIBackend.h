#ifndef _FURY_RHI_BACKEND_H_
#define _FURY_RHI_BACKEND_H_

#include <vector>

#include "Fury/RHI/RHIDevice.h"

// CMake defines FURY_RHI_VULKAN / FURY_RHI_METAL; default off when absent.
#ifndef FURY_RHI_VULKAN
	#define FURY_RHI_VULKAN 0
#endif
#ifndef FURY_RHI_METAL
	#define FURY_RHI_METAL 0
#endif

namespace fury
{
	namespace RHI
	{
		FURY_API std::string BackendToString(RHIBackend backend);

		// Case-insensitive; false on unknown name.
		FURY_API bool BackendFromString(const std::string &name, RHIBackend &backend);

		// Built into the binary (CMake options), independent of platform support.
		FURY_API bool IsBackendCompiled(RHIBackend backend);

		// Compiled AND supported on this platform (win: gl+vk, mac: gl+vk+metal, linux: gl+vk).
		FURY_API bool IsBackendSupported(RHIBackend backend);

		FURY_API std::vector<RHIBackend> GetSupportedBackends();

		FURY_API std::vector<std::string> GetSupportedBackendNames();

		// Selection sources. Empty string = unset; resolution order in
		// ResolveBackend. SetCommandLineBackend also forces headless capture
		// tools onto OpenGL (they must ignore every other source).
		FURY_API void SetCommandLineBackend(const std::string &name);

		FURY_API void SetIniBackend(const std::string &name);

		// CLI > FURY_RHI env > luaOption (Engine.run { rhi = ... }) > editor
		// ini > platform default (opengl). The first set source wins; an
		// unknown or unsupported value logs a warning and resolves to opengl.
		FURY_API RHIBackend ResolveBackend(const std::string &luaOption);

		// Basic sanity check after device creation: one texture upload, one
		// trivial pipeline, one cleared+presented frame. Logs the failing step.
		FURY_API bool ProbeActiveDevice(int width, int height);

		// Creates the device, runs the init probe, and sets GRHIDevice on
		// success. Returns nullptr (device unusable / not compiled / probe
		// failed) without touching GRHIDevice on failure.
		FURY_API RHIDevice *CreateDevice(RHIBackend backend, const RHIWindowInfo &window);

		FURY_API void DestroyDevice();
	}
}

#endif // _FURY_RHI_BACKEND_H_
