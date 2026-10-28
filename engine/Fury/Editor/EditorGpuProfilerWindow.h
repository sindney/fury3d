#ifndef _FURY_EDITOR_GPU_PROFILER_WINDOW_H_
#define _FURY_EDITOR_GPU_PROFILER_WINDOW_H_

#include <string>

#include "Fury/Macros.h"

namespace fury
{
	namespace Editor
	{
#if WITH_EDITOR
		// Open the GPU capture viewer on a --gpu-dump output directory
		// (rendergraph.json + textures/*.png + backbuffer.png). Idempotent:
		// a second Open call for the same directory focuses the existing
		// window; an empty dir opens the window without a capture loaded.
		void FURY_API OpenGpuProfilerWindow(const std::string &captureDir);

		// Render the capture viewer window when open. Call once per frame
		// from the editor tick, next to RenderAllOpenAssetEditors.
		void FURY_API RenderGpuProfilerWindow();

		void FURY_API ShutdownGpuProfilerWindow();
#else
		inline void OpenGpuProfilerWindow(const std::string &) {}
		inline void RenderGpuProfilerWindow() {}
		inline void ShutdownGpuProfilerWindow() {}
#endif
	}
}

#endif // _FURY_EDITOR_GPU_PROFILER_WINDOW_H_
