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
#ifdef WITH_EDITOR
		// Open the per-asset editor window for `mesh`. Idempotent: if
		// the editor is already open for this mesh, no second window
		// opens. Called from the Content Browser's double-click handler.
		void FURY_API OpenMeshEditor(const std::shared_ptr<Mesh>& mesh);

		// Open the per-asset editor window for `mat`.
		void FURY_API OpenMaterialEditor(const std::shared_ptr<Material>& mat);

		// Render every currently-open asset editor window. Called from
		// Editor::Tick every frame, after the Content Browser (so a
		// double-click this frame opens the editor on the same frame).
		// These are regular dockable ImGui windows (NOT modal popups),
		// matching the Content Browser / Scene Inspector pattern.
		void FURY_API RenderAllOpenAssetEditors();

		// Lookup or allocate a 128×128 thumbnail texture for `mesh`.
		// Returns the color RT's GL texture ID (cast to ImTextureID by
		// the caller: `(ImTextureID)(intptr_t)id`). The mesh is
		// rendered once offscreen with a flat-shaded shader; the
		// result is cached on the mesh's BufferId and reused until
		// the mesh is re-uploaded.
		unsigned int FURY_API GetMeshThumbnail(const std::shared_ptr<Mesh>& mesh);

		// Evict mesh-thumbnail cache entries whose BufferId is not in
		// `liveIds`. Called by RenderContentBrowserWindow after the
		// ForEach pass (task 7.3).
		void FURY_API EvictStaleMeshThumbnails(const std::unordered_set<size_t>& liveIds);
#else
		inline void OpenMeshEditor(const std::shared_ptr<Mesh>&) {}
		inline void OpenMaterialEditor(const std::shared_ptr<Material>&) {}
		inline void RenderAllOpenAssetEditors() {}
#endif
	}
}

#endif // _FURY_EDITOR_ASSET_WINDOWS_H_
