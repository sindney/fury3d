#ifndef _FURY_EDITOR_PARTICLE_WINDOW_H_
#define _FURY_EDITOR_PARTICLE_WINDOW_H_

#include <memory>
#include <string>

#include "Fury/Macros.h"

namespace fury
{
	class ParticleSystem;

	namespace Editor
	{
#ifdef WITH_EDITOR
		// Open the per-asset particle editor window (idempotent -- a
		// second Open call for the same system focuses the existing
		// window rather than opening a duplicate).
		void FURY_API OpenParticleEditor(const std::shared_ptr<ParticleSystem> &system);

		// Render every open particle editor window. Called from
		// RenderAllOpenAssetEditors (EditorAssetWindows.cpp).
		void FURY_API RenderAllOpenParticleEditors();

		// Render a single editor window. Public so external code can
		// embed a particle inspector without going through the
		// popup-id-keyed map.
		void FURY_API RenderParticleEditorWindow(
			const std::shared_ptr<ParticleSystem> &system, bool *p_open);
#else
		inline void OpenParticleEditor(const std::shared_ptr<ParticleSystem> &) {}
		inline void RenderAllOpenParticleEditors() {}
		inline void RenderParticleEditorWindow(
			const std::shared_ptr<ParticleSystem> &, bool *) {}
#endif
	}
}

#endif // _FURY_EDITOR_PARTICLE_WINDOW_H_