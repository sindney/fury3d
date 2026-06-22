#ifndef _FURY_CLI_H_
#define _FURY_CLI_H_

#include "Fury/Macros.h"

namespace fury
{
	// The fury binary's CLI surface. Invoked from examples/main.cpp when
	// argv[1] is a known subcommand token; bypasses SFML window creation,
	// engine initialization, and the Lua VM (none of these are needed for
	// asset conversion or asset inspection).
	//
	// The CLI uses stable exit codes:
	//   0 — success
	//   1 — user error (bad arguments, unsupported input, file not found,
	//       rejected glTF feature)
	//   2 — internal error (uncaught exception, assertion failure)
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
		// Recognized tokens: convert, info, help, --help, -h, version, --version.
		static bool LooksLikeSubcommand(const char *arg0);

		// Dispatch the CLI on argv. Returns a process exit code (see above).
		// Called with the full argc / argv from main(), not a slice.
		static int Run(int argc, char **argv);
	};
}

#endif // _FURY_CLI_H_
