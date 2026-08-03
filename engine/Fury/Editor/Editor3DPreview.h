#ifndef _FURY_EDITOR_3D_PREVIEW_H_
#define _FURY_EDITOR_3D_PREVIEW_H_

#include <string>
#include <unordered_map>

#include "Fury/Macros.h"
#include "Fury/Matrix4.h"
#include "Fury/Vector4.h"

namespace fury { class Texture; }

#ifdef WITH_EDITOR

struct ImGuiIO;

namespace fury
{
	namespace Editor
	{
		// Per-editor orbit-camera state. Keyed by popup ID ("MeshEditor:<name>"
		// / "ParticleEditor:<name>") so multiple editors side-by-side
		// have independent cameras. Originally defined in
		// EditorAssetWindows.cpp — extracted into this helper so the
		// particle editor can share the same mesh-editor preview
		// pipeline (see asset-editor-windows delta spec).
		struct FURY_API OrbitState
		{
			Vector4 target{0, 0, 0, 1};
			float distance = 0.0f;
			float initialDistance = 0.0f;
			float yaw = 30.0f * 0.0174532925f;
			float pitch = 20.0f * 0.0174532925f;
			int preview_lod_override = -1;
			void *framed_mesh = nullptr; // Mesh* cast — owned externally
			bool initialized = false;
		};

		// Keyed access to the orbit map. Allocates a default state on
		// first call so callers don't need to handle the "not found"
		// case.
		FURY_API OrbitState &OrbitFor(const std::string &popup_id);

		// Per-window FBO + color/depth attachments. Reallocated on
		// resize. Returns 0 if the FBO is incomplete (caller must
		// skip the draw).
		struct FURY_API PreviewRT
		{
			unsigned int fbo = 0;
			std::shared_ptr<::fury::Texture> colorRT;
			std::shared_ptr<::fury::Texture> depthRT;
			int width = 0;
			int height = 0;
		};

		// Returns the FBO for `popup_id`, creating it on first call
		// and resizing on width/height change. The caller binds it
		// (BindFramebuffer), draws into it, then binds framebuffer 0
		// again. `out_resized` (optional) is set true when this call
		// (re)allocated — callers reframe their orbit on it.
		FURY_API PreviewRT &EnsureRT(const std::string &popup_id, int w, int h,
			bool *out_resized = nullptr);

		struct FURY_API ViewProj
		{
			Matrix4 view;
			Matrix4 proj;
			Vector4 eye;
		};

		// Compute view + projection + eye position from the orbit
		// state + the framed target's AABB radius. The aspect ratio
		// comes from the FBO size (`(float)w / h`).
		FURY_API ViewProj ComputeViewProj(const OrbitState &orbit,
			const Vector4 &aabb_center, float aabb_radius, float aspect);

		// Draws the ground grid + AABB wireframe at the framed target.
		// `aabb_min_y` is the bottom of the AABB; the grid sits on
		// that plane. Grid extent = `aabb_radius * 2` (matches the
		// mesh editor's formula).
		FURY_API void DrawGroundGrid(const Matrix4 &view, const Matrix4 &proj,
			const Vector4 &aabb_center, float aabb_min_y, float aabb_radius);

		// Apply LMB-orbit / wheel-zoom / RMB-pan to `orbit`. Caller
		// supplies the current eye position so the pan can compute
		// camera-aligned right/up vectors. Scales with `radius` so
		// any-framed-size handles the same.
		FURY_API void ApplyCameraInput(OrbitState &orbit, const Vector4 &eye,
			const ImGuiIO &io, float radius);

		// Re-frame the orbit on the new AABB center / radius. Called
		// when the framed mesh switches or the FBO resizes. Preserves
		// the user's zoom fraction across mesh switches.
		FURY_API void ReframeOrbit(OrbitState &orbit, const Vector4 &aabb_center,
			float aabb_radius, float aspect, bool mesh_changed);
	}
}

#endif // WITH_EDITOR

#endif // _FURY_EDITOR_3D_PREVIEW_H_