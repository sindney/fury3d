#ifndef _FURY_EDITOR_ASSET_WINDOWS_H_
#define _FURY_EDITOR_ASSET_WINDOWS_H_

#include <memory>
#include <string>
#include <unordered_set>

#include "Fury/Macros.h"

namespace fury
{
	class Mesh;
	class Material;

	namespace Editor
	{
#if WITH_EDITOR
		// Open the per-asset mesh editor window (idempotent).
		void FURY_API OpenMeshEditor(const std::shared_ptr<Mesh>& mesh);

		// Open the per-asset material editor window.
		void FURY_API OpenMaterialEditor(const std::shared_ptr<Material>& mat);

		// Render every open asset editor window. Called from Editor::Tick.
		void FURY_API RenderAllOpenAssetEditors();

		// Lookup or allocate a 128x128 thumbnail texture for `mesh`.
		unsigned int FURY_API GetMeshThumbnail(const std::shared_ptr<Mesh>& mesh);

		// Evict thumbnail entries whose BufferId is not in `liveIds`.
		void FURY_API EvictStaleMeshThumbnails(const std::unordered_set<size_t>& liveIds);

		// Periodic refresh poll; re-hashes + re-renders on dirty transitions.
		void FURY_API RefreshMeshThumbnailCache();

		// Force a re-hash + re-render of `mesh`'s thumbnail on next poll.
		void FURY_API RefreshMeshThumbnailNow(const std::shared_ptr<Mesh>& mesh);

		// Scan Resource/.thumbcache/ and populate the in-memory hit index.
		void FURY_API WarmDiskCacheIndex();
#else
		inline void OpenMeshEditor(const std::shared_ptr<Mesh>&) {}
		inline void OpenMaterialEditor(const std::shared_ptr<Material>&) {}
		inline void RenderAllOpenAssetEditors() {}
		inline void RefreshMeshThumbnailCache() {}
		inline void RefreshMeshThumbnailNow(const std::shared_ptr<Mesh>&) {}
		inline void WarmDiskCacheIndex() {}
#endif
	}
}

#endif // _FURY_EDITOR_ASSET_WINDOWS_H_
