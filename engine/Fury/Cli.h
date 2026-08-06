#ifndef _FURY_CLI_H_
#define _FURY_CLI_H_

#include <memory>
#include <string>

#include "Fury/Macros.h"

namespace fury
{
	class Scene;

	// The fury binary's CLI surface. Invoked from examples/main.cpp when
	// argv[1] is a known subcommand token; bypasses SFML window creation,
	// engine initialization, and the Lua VM (none of these are needed for
	// asset conversion or asset inspection).
	//
	// The CLI uses stable exit codes:
	//   0 -- success
	//   1 -- user error (bad arguments, unsupported input, file not found,
	//       rejected glTF feature)
	//   2 -- internal error (uncaught exception, assertion failure)
	//
	// Subcommands are documented inline as `kTopHelp` / `kConvertHelp` /
	// `kInfoHelp` / `kVersionHelp` constants and mirrored in docs/CLI.md.
	class FURY_API Cli final
	{
	public:

		// Returns true if `arg0` matches a known CLI subcommand token. The
		// router in examples/main.cpp uses this to decide whether to take
		// the CLI path or fall through to the Lua launcher path.
		//
		// Recognized tokens: convert, info, exec, help, --help, -h, version, --version.
		static bool LooksLikeSubcommand(const char *arg0);

		// Dispatch the CLI on argv. Returns a process exit code (see above).
		// Called with the full argc / argv from main(), not a slice.
		static int Run(int argc, char **argv);

		// Load a scene from `<path>` by extension (.json/.bin/.gltf/.glb/.fbx),
		// reusing the `fury exec` dispatch. Does NOT set Scene::Active.
		static std::shared_ptr<Scene> LoadSceneForExec(const std::string &path);

		// `fury render-mesh <scene> <mesh_name> <output.png> [--lod N]` -- render
		// a mesh to a 256x256 PNG. Without --lod, renders LOD 0 (the mesh
		// itself). With --lod N, renders mesh->GetLodMesh(N). Needs a GL
		// context (caller sets it up).
		static int RenderMesh(int argc, char **argv);
	};
}

#endif // _FURY_CLI_H_
