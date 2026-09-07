#ifndef _FURY_KRAUT_CONVERTER_H_
#define _FURY_KRAUT_CONVERTER_H_

#include <string>
#include <vector>

#include "Fury/Macros.h"

namespace fury
{
	// Subprocess wrapper around the KrautCLI / KrautPreview tool binaries
	// (vendored engine/ThirdParty/Kraut, copied next to the fury executable
	// POST_BUILD -- the FBX2glTF distribution pattern).
	//
	// `fury kraut generate` composes these: KrautCLI exports the glb,
	// KrautPreview bakes the billboard atlas + per-LOD preview screenshots.
	// Exit codes propagate from the tools (0 ok, 1 usage, 2 load, 3 generate,
	// 4 export/render); -1 means the subprocess never started.
	class FURY_API KrautConverter final
	{
	public:

		struct Result
		{
			std::string output_path;   // produced file (empty on failure)
			std::string stdout_capture;
			std::string stderr_capture;
			int exit_code = -1;

			bool ok() const { return exit_code == 0 && !output_path.empty(); }
		};

		// KrautCLI export --format glb <descriptor> --out <output_dir>/<stem>.glb.
		// seedGiven=false uses the descriptor's stored seed.
		static Result ExportGlb(const std::string &descriptor_path,
			unsigned int seed, bool seedGiven, const std::string &output_dir);

		// KrautPreview --atlas <descriptor> -> <output_dir>/<stem>_BillboardAtlas.png.
		static Result BakeAtlas(const std::string &descriptor_path,
			unsigned int seed, bool seedGiven, const std::string &output_dir, int atlasCols);

		// KrautPreview --screenshot per mesh LOD tier ->
		// <output_dir>/<stem>_LOD<n>.png. Non-fatal when the preview binary is
		// missing (exit_code stays -1, stderr names it) -- previews are a nicety.
		static Result PreviewScreenshots(const std::string &descriptor_path,
			unsigned int seed, bool seedGiven, const std::string &output_dir);

		// Returns the absolute path to a Kraut tool binary ("KrautCLI" /
		// "KrautPreview"). Empty if not found. Lookup: next to the running
		// executable, then engine/ThirdParty/Kraut/Output/Bin/<tag>/ relative
		// walks (in-tree dev before POST_BUILD).
		static std::string LocateBinary(const char* toolName);
	};
}

#endif // _FURY_KRAUT_CONVERTER_H_
