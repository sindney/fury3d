#ifndef _FURY_EDITOR_RENDER_JOBS_H_
#define _FURY_EDITOR_RENDER_JOBS_H_

#if WITH_EDITOR

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Fury/Macros.h"

namespace fury
{
	class Texture;

	namespace Editor
	{
		// Render-thread offscreen surface for editor previews/thumbnails.
		// Lives in a GL-thread-owned registry; the game thread never
		// touches it directly.
		struct OffscreenSurface
		{
			unsigned int fbo = 0;

			std::shared_ptr<Texture> color;

			std::shared_ptr<Texture> depth;

			int w = 0;

			int h = 0;
		};

		// Get-or-create at size (recreates on size change). GL thread only.
		OffscreenSurface &AcquireSurface(const std::string &key, int w, int h);

		// Display slot: the latest published color texture, mutex-guarded
		// so the game thread can sample while the render thread produces
		// the next one.
		struct DisplaySlot
		{
			std::mutex mutex;

			std::shared_ptr<Texture> color;

			std::uint64_t seq = 0;
		};

		// GL thread: publish a surface's color texture for display.
		void PublishSurface(const std::string &key, const std::shared_ptr<Texture> &color);

		// Game thread: the current displayable texture id (0 until the
		// first publish lands; pre-render frames show the fallback).
		unsigned int DisplayTextureId(const std::string &key);

		// Game thread: drop one slot (window closed / asset deleted).
		void ResetDisplaySlot(const std::string &key);
	}
}

#endif // WITH_EDITOR
#endif // _FURY_EDITOR_RENDER_JOBS_H_
