#ifndef _FURY_GLTF_IMPORTER_H_
#define _FURY_GLTF_IMPORTER_H_

#include <memory>
#include <string>

#include "Fury/Macros.h"

namespace fury
{
	class Scene;

	// Translates a glTF 2.0 file into an engine `Scene`.
	//
	// CPU-only: no GL context required at call time. The returned scene has
	// all CPU-side data populated (Mesh::Positions etc., Joint matrices,
	// AnimationClip keyframes, Material uniforms with engine-shaped slots).
	// GPU upload happens lazily when the renderer first samples the data.
	//
	// Used by both the `fury convert gltf` CLI subcommand and the runtime
	// `Importer.LoadGltf` Lua binding. One source of truth for glTF -> engine
	// translation.
	//
	// Material mapping is lossy: glTF PBR metallic-roughness -> engine Lambert.
	// `baseColorFactor` -> `diffuse_color`, `baseColorTexture` -> `diffuse_texture`,
	// `emissiveFactor` -> `emissive_color`. Other PBR fields (metallic, roughness,
	// normal, occlusion) are read but discarded with a one-shot warning per
	// source material. An HDR/PBR pipeline + PBR material variant is a deferred
	// follow-up; until then the Lambert pipeline is the engine's only path.
	//
	// Animation time-base: glTF stores keyframe times as float seconds; the
	// engine's AnimationClip uses integer ticks at fixed 24 fps. Channels are
	// resampled at 24 Hz; CUBICSPLINE -> LINEAR with a one-shot warning per
	// sampler.
	//
	// Rejections (return nullptr with a clear log message):
	//   * morph targets (primitive.targets non-empty)
	//   * sparse accessors (accessor.sparse.isSparse)
	//   * non-default buffer-view byte stride (bufferView.byteStride != 0)
	//   * non-triangle primitives (primitive.mode != 4)
	//   * non-empty extensionsRequired
	class FURY_API GltfImporter final
	{
	public:

		struct Options
		{
			// glTF stores time as seconds; engine AnimationClip is ticks-based.
			// 24 matches the engine's FBX-heritage default.
			float anim_ticks_per_second;

			// Base path (no extension) used to synthesize URIs for images
			// embedded inside .glb files. For example, if output_basename is
			// "/tmp/foo" and the .glb has 2 embedded images, the importer
			// writes "/tmp/foo_image0.png" / "/tmp/foo_image1.jpg" and the
			// engine Material's diffuse_texture references those paths.
			// For .gltf inputs (external image URIs) this is unused — the
			// importer passes URIs through verbatim.
			//
			// Empty string is acceptable if the input has no embedded images
			// or if the caller doesn't intend to extract them.
			std::string output_basename_no_ext;

			Options() : anim_ticks_per_second(24.0f) {}
		};

		// Returns nullptr on any error (file not found, parse failure,
		// unsupported feature). All errors are logged via FURYE before return.
		//
		// scene_name: name used for the returned Scene entity.
		// working_dir: base path the returned Scene uses to resolve relative
		//              resource references (texture URIs etc.).
		static std::shared_ptr<Scene> Import(
			const std::string &input_path,
			const std::string &scene_name,
			const std::string &working_dir,
			const Options &opts = Options());
	};
}

#endif // _FURY_GLTF_IMPORTER_H_
