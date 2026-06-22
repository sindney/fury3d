#ifndef _FURY_FBX_CONVERTER_H_
#define _FURY_FBX_CONVERTER_H_

#include <string>

#include "Fury/Macros.h"

namespace fury
{
	// Subprocess wrapper around the vendored FBX2glTF binary.
	//
	// Why subprocess: the engine deliberately does not link the FBX SDK
	// (Autodesk EULA + binary blob). FBX2glTF (Facebook Incubator) is a
	// self-contained binary that ingests .fbx and writes a .glb (or .gltf).
	// We exec it, capture stdout/stderr, and read the produced .glb back
	// through the engine's regular glTF importer.
	//
	// Platform support: macOS x86_64 (runs on arm64 via Rosetta), Linux x86_64,
	// Windows x86_64. The CMake build copies the platform-appropriate binary
	// next to the `fury` executable; LocateBinary() walks from the executable's
	// own directory rather than getcwd() so the converter works regardless of
	// where the user invokes it from.
	class FURY_API FbxConverter final
	{
	public:

		struct Result
		{
			// Absolute path to the produced glTF/glb file. Empty on failure.
			std::string output_path;

			// Captured subprocess output. Always populated; useful for the
			// CLI handler to print on success-with-warnings, or to surface
			// in the runtime error path so a Lua user can see what failed.
			std::string stdout_capture;
			std::string stderr_capture;

			// Subprocess exit code. -1 means we never reached posix_spawn /
			// CreateProcess (binary not found, fork failed, etc.).
			int exit_code = -1;

			bool ok() const { return exit_code == 0 && !output_path.empty(); }
		};

		// Convert <input_fbx_path> into a glb file inside <output_dir>.
		// The produced file is named <output_dir>/<input_stem>.glb (FBX2glTF
		// default). Returns Result::ok() == true on success.
		//
		// On failure: stderr_capture contains the subprocess's stderr (or a
		// synthetic message when the subprocess couldn't be started at all);
		// the caller should propagate it via FURYE (runtime path) or the
		// CLI's stderr (offline path).
		static Result Convert(const std::string &input_fbx_path,
			const std::string &output_dir);

		// Returns the absolute path to the platform-appropriate FBX2glTF
		// binary. Empty if not found. The lookup tries:
		//   1. Next to the running executable.
		//   2. engine/ThirdParty/FBX2glTF/ relative to the engine source root
		//      (for in-tree dev builds before POST_BUILD has run).
		static std::string LocateBinary();
	};
}

#endif // _FURY_FBX_CONVERTER_H_
