// FbxConverter.cpp -- invoke the vendored FBX2glTF binary as a subprocess.
//
// Platform layout:
//   macOS:   posix_spawn + pipes  (binary: FBX2glTF-darwin-x64)
//   Linux:   posix_spawn + pipes  (binary: FBX2glTF-linux-x64)
//   Windows: CreateProcess + pipes (binary: FBX2glTF-windows-x64.exe)
//
// The subprocess code paths are #ifdef-gated. The capture/error-handling
// shape is the same across platforms so the caller (Cli / Lua binding)
// doesn't care which OS is hosting.

#include "Fury/FbxConverter.h"
#include "Fury/FileUtil.h"
#include "Fury/Log.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <vector>

#if defined(__APPLE__)
	#include <mach-o/dyld.h>
	#include <spawn.h>
	#include <sys/stat.h>
	#include <sys/wait.h>
	#include <unistd.h>
	extern char **environ;
	#define FURY_FBX2GLTF_BINARY "FBX2glTF-darwin-x64"
#elif defined(__linux__)
	#include <spawn.h>
	#include <sys/stat.h>
	#include <sys/wait.h>
	#include <unistd.h>
	extern char **environ;
	#define FURY_FBX2GLTF_BINARY "FBX2glTF-linux-x64"
#elif defined(_WIN32)
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
	#define FURY_FBX2GLTF_BINARY "FBX2glTF-windows-x64.exe"
#else
	#define FURY_FBX2GLTF_BINARY ""
#endif

namespace fury
{
	namespace
	{
		bool FileExists(const std::string &p)
		{
#if defined(_WIN32)
			DWORD attr = GetFileAttributesA(p.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
			struct stat st;
			return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
		}

		std::string DirOf(const std::string &p)
		{
			auto pos = p.find_last_of("/\\");
			return (pos == std::string::npos) ? std::string{} : p.substr(0, pos);
		}

		std::string Stem(const std::string &p)
		{
			auto slash = p.find_last_of("/\\");
			auto base = (slash == std::string::npos) ? p : p.substr(slash + 1);
			auto dot = base.find_last_of('.');
			return (dot == std::string::npos) ? base : base.substr(0, dot);
		}
	}

	std::string FbxConverter::LocateBinary()
	{
		const std::string exe = FileUtil::GetExecutablePath();
		const std::string exedir = exe.empty() ? "" : DirOf(exe);

		// 1. Adjacent to the running executable (post-build copy).
		if (!exedir.empty())
		{
			std::string candidate = exedir + "/" + FURY_FBX2GLTF_BINARY;
			if (FileExists(candidate)) return candidate;
		}

		// 2. engine/ThirdParty/FBX2glTF/ relative to the engine source root.
		//    For in-tree development before the POST_BUILD copy runs (or in
		//    cases where the user runs ./build-engine/fury directly).
		if (!exedir.empty())
		{
			// Walk up to a few likely roots: build-engine/, examples/bin/,
			// other build directories. We don't try infinitely -- just two
			// levels up -- because anything deeper indicates the layout has
			// drifted and we should fail visibly.
			const std::vector<std::string> tries = {
				exedir + "/../engine/ThirdParty/FBX2glTF/" + FURY_FBX2GLTF_BINARY,
				exedir + "/../../engine/ThirdParty/FBX2glTF/" + FURY_FBX2GLTF_BINARY,
				exedir + "/../../../engine/ThirdParty/FBX2glTF/" + FURY_FBX2GLTF_BINARY,
			};
			for (const auto &c : tries)
				if (FileExists(c)) return c;
		}

		return {};
	}

	FbxConverter::Result FbxConverter::Convert(
		const std::string &input_fbx_path,
		const std::string &output_dir)
	{
		Result r;

#if !defined(__APPLE__) && !defined(__linux__) && !defined(_WIN32)
		r.stderr_capture = "FbxConverter: this build doesn't support the host platform";
		return r;
#else
		const std::string binary = LocateBinary();
		if (binary.empty())
		{
			r.stderr_capture = "FbxConverter: could not locate '" FURY_FBX2GLTF_BINARY
				"' (expected next to the fury executable; ensure CMake POST_BUILD copy succeeded)";
			return r;
		}

		if (!FileExists(input_fbx_path))
		{
			r.stderr_capture = "FbxConverter: input '" + input_fbx_path + "' does not exist";
			return r;
		}

		const std::string stem = Stem(input_fbx_path);
		const std::string out_basename = output_dir.empty()
			? stem
			: output_dir + "/" + stem;
		r.output_path = out_basename + ".glb";

#if defined(_WIN32)
		// CreateProcess with stdout/stderr capture via anonymous pipes.
		SECURITY_ATTRIBUTES sa{};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		HANDLE stdout_r = nullptr, stdout_w = nullptr;
		HANDLE stderr_r = nullptr, stderr_w = nullptr;
		if (!CreatePipe(&stdout_r, &stdout_w, &sa, 0)
			|| !CreatePipe(&stderr_r, &stderr_w, &sa, 0))
		{
			r.stderr_capture = "FbxConverter: CreatePipe failed";
			return r;
		}
		SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(stderr_r, HANDLE_FLAG_INHERIT, 0);

		// Build the command line. CreateProcess wants a writable buffer.
		// --binary forces .glb output (default is a directory of .gltf+.bin);
		// --anim-framerate bake24 matches the engine's tick-based AnimationClip.
		std::string cmd = "\"" + binary + "\" --binary --anim-framerate bake24"
			" --input \"" + input_fbx_path
			+ "\" --output \"" + out_basename + "\"";
		std::vector<char> cmdbuf(cmd.begin(), cmd.end());
		cmdbuf.push_back('\0');

		STARTUPINFOA si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = stdout_w;
		si.hStdError = stderr_w;
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		PROCESS_INFORMATION pi{};
		if (!CreateProcessA(binary.c_str(), cmdbuf.data(), nullptr, nullptr,
				TRUE, 0, nullptr, nullptr, &si, &pi))
		{
			r.stderr_capture = "FbxConverter: CreateProcess failed (error "
				+ std::to_string(GetLastError()) + ")";
			CloseHandle(stdout_r); CloseHandle(stdout_w);
			CloseHandle(stderr_r); CloseHandle(stderr_w);
			return r;
		}
		CloseHandle(stdout_w); CloseHandle(stderr_w);

		auto drain = [](HANDLE h, std::string &dest) {
			char buf[4096]; DWORD n = 0;
			while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
				dest.append(buf, buf + n);
		};
		drain(stdout_r, r.stdout_capture);
		drain(stderr_r, r.stderr_capture);
		CloseHandle(stdout_r); CloseHandle(stderr_r);

		WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD exit_code = 1;
		GetExitCodeProcess(pi.hProcess, &exit_code);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		r.exit_code = static_cast<int>(exit_code);

#else
		// POSIX path (macOS + Linux): posix_spawn + pipes for stdout/stderr.
		int out_pipe[2] = { -1, -1 };
		int err_pipe[2] = { -1, -1 };
		if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
		{
			r.stderr_capture = "FbxConverter: pipe() failed (";
			r.stderr_capture += std::strerror(errno);
			r.stderr_capture += ")";
			if (out_pipe[0] != -1) { close(out_pipe[0]); close(out_pipe[1]); }
			if (err_pipe[0] != -1) { close(err_pipe[0]); close(err_pipe[1]); }
			return r;
		}

		posix_spawn_file_actions_t actions;
		posix_spawn_file_actions_init(&actions);
		posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
		posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
		posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
		posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
		posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
		posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

		// argv layout. const_cast is conventional for posix_spawn (the call
		// doesn't actually mutate them). --binary forces .glb output (default
		// is a directory of .gltf+.bin); --anim-framerate bake24 matches the
		// engine's tick-based AnimationClip.
		std::vector<std::string> arg_strings = {
			binary,
			"--binary",
			"--anim-framerate", "bake24",
			"--input", input_fbx_path,
			"--output", out_basename,
		};
		std::vector<char*> argv;
		for (auto &s : arg_strings) argv.push_back(const_cast<char*>(s.c_str()));
		argv.push_back(nullptr);

		pid_t pid = 0;
		int rc = posix_spawn(&pid, binary.c_str(), &actions, nullptr, argv.data(), environ);
		posix_spawn_file_actions_destroy(&actions);
		close(out_pipe[1]);
		close(err_pipe[1]);

		if (rc != 0)
		{
			close(out_pipe[0]);
			close(err_pipe[0]);
			r.stderr_capture = "FbxConverter: posix_spawn failed (";
			r.stderr_capture += std::strerror(rc);
			r.stderr_capture += ")";
			return r;
		}

		auto drain = [](int fd, std::string &dest) {
			char buf[4096]; ssize_t n;
			while ((n = read(fd, buf, sizeof(buf))) > 0) dest.append(buf, buf + n);
		};
		drain(out_pipe[0], r.stdout_capture);
		drain(err_pipe[0], r.stderr_capture);
		close(out_pipe[0]);
		close(err_pipe[0]);

		int status = 0;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
			r.exit_code = WEXITSTATUS(status);
		else
			r.exit_code = -1;
#endif

		// macOS arm64 + no Rosetta: the binary is x86_64, posix_spawn succeeds
		// but the child exits non-zero with a "Bad CPU type" message. Annotate
		// for the user so the error is actionable.
#if defined(__APPLE__)
		if (r.exit_code != 0
			&& r.stderr_capture.find("Bad CPU type") != std::string::npos)
		{
			r.stderr_capture += "\nFbxConverter: the FBX2glTF binary is x86_64. "
				"If you're on Apple Silicon, install Rosetta: "
				"softwareupdate --install-rosetta";
		}
#endif

		if (r.exit_code != 0 || !FileExists(r.output_path))
		{
			if (r.exit_code == 0 && !FileExists(r.output_path))
				r.stderr_capture += "\nFbxConverter: subprocess exited 0 but '"
					+ r.output_path + "' was not written";
			r.output_path.clear();
		}
		return r;
#endif
	}
}
