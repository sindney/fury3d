#include "Fury/KrautConverter.h"

#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Macros.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>

#if PLATFORM_MACOS || PLATFORM_LINUX
	#include <spawn.h>
	#include <sys/stat.h>
	#include <sys/wait.h>
	#include <unistd.h>
	extern char **environ;
	#define FURY_KRAUTCLI_BINARY "KrautCLI"
	#define FURY_KRAUTPREVIEW_BINARY "KrautPreview"
#elif PLATFORM_WINDOWS
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
	#define FURY_KRAUTCLI_BINARY "KrautCLI.exe"
	#define FURY_KRAUTPREVIEW_BINARY "KrautPreview.exe"
#else
	#define FURY_KRAUTCLI_BINARY ""
	#define FURY_KRAUTPREVIEW_BINARY ""
#endif

namespace fury
{
	namespace
	{
		bool FileExists(const std::string &p)
		{
			std::error_code ec;
			return std::filesystem::exists(p, ec);
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

		// Runs `binary` with `args`, capturing stdout/stderr. Returns the
		// exit code, or -1 when the process never started. Mirrors the
		// FbxConverter subprocess machinery (pipe + posix_spawn /
		// CreateProcess).
		int RunSubprocessCapture(const std::string &binary,
			const std::vector<std::string> &args,
			std::string &outStdout, std::string &outStderr)
		{
#if !PLATFORM_MACOS && !PLATFORM_LINUX && !PLATFORM_WINDOWS
			outStderr = "KrautConverter: this build doesn't support the host platform";
			return -1;
#elif PLATFORM_WINDOWS
			SECURITY_ATTRIBUTES sa{};
			sa.nLength = sizeof(sa);
			sa.bInheritHandle = TRUE;
			HANDLE stdout_r = nullptr, stdout_w = nullptr;
			HANDLE stderr_r = nullptr, stderr_w = nullptr;
			if (!CreatePipe(&stdout_r, &stdout_w, &sa, 0)
				|| !CreatePipe(&stderr_r, &stderr_w, &sa, 0))
			{
				outStderr = "KrautConverter: CreatePipe failed";
				return -1;
			}
			SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);
			SetHandleInformation(stderr_r, HANDLE_FLAG_INHERIT, 0);

			std::string cmd = "\"" + binary + "\"";
			for (const auto &a : args)
				cmd += " \"" + a + "\"";
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
				outStderr = "KrautConverter: CreateProcess failed (error "
					+ std::to_string(GetLastError()) + ")";
				CloseHandle(stdout_r); CloseHandle(stdout_w);
				CloseHandle(stderr_r); CloseHandle(stderr_w);
				return -1;
			}

			CloseHandle(stdout_w);
			CloseHandle(stderr_w);

			char buf[4096];
			DWORD n;
			while (ReadFile(stdout_r, buf, sizeof(buf), &n, nullptr) && n > 0)
				outStdout.append(buf, buf + n);
			while (ReadFile(stderr_r, buf, sizeof(buf), &n, nullptr) && n > 0)
				outStderr.append(buf, buf + n);
			CloseHandle(stdout_r);
			CloseHandle(stderr_r);

			WaitForSingleObject(pi.hProcess, INFINITE);
			DWORD code = 0;
			GetExitCodeProcess(pi.hProcess, &code);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			return static_cast<int>(code);
#else
			int out_pipe[2] = { -1, -1 };
			int err_pipe[2] = { -1, -1 };
			if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
			{
				outStderr = "KrautConverter: pipe() failed (";
				outStderr += std::strerror(errno);
				outStderr += ")";
				if (out_pipe[0] != -1) { close(out_pipe[0]); close(out_pipe[1]); }
				if (err_pipe[0] != -1) { close(err_pipe[0]); close(err_pipe[1]); }
				return -1;
			}

			posix_spawn_file_actions_t actions;
			posix_spawn_file_actions_init(&actions);
			posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
			posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
			posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
			posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
			posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
			posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

			std::vector<std::string> arg_strings;
			arg_strings.push_back(binary);
			for (const auto &a : args)
				arg_strings.push_back(a);
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
				outStderr = "KrautConverter: posix_spawn failed (";
				outStderr += std::strerror(rc);
				outStderr += ")";
				return -1;
			}

			auto drain = [](int fd, std::string &dest) {
				char buf[4096]; ssize_t n;
				while ((n = read(fd, buf, sizeof(buf))) > 0) dest.append(buf, buf + n);
			};
			drain(out_pipe[0], outStdout);
			drain(err_pipe[0], outStderr);
			close(out_pipe[0]);
			close(err_pipe[0]);

			int status = 0;
			waitpid(pid, &status, 0);
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			return -1;
#endif
		}
	}

	std::string KrautConverter::LocateBinary(const char* toolName)
	{
		const std::string expected =
			(std::strcmp(toolName, "KrautPreview") == 0) ? FURY_KRAUTPREVIEW_BINARY : FURY_KRAUTCLI_BINARY;
		if (expected.empty())
			return {};

		const std::string exe = FileUtil::GetExecutablePath();
		const std::string exedir = exe.empty() ? "" : DirOf(exe);
		if (exedir.empty())
			return {};

		// 1. Adjacent to the running executable (post-build copy).
		{
			std::string candidate = exedir + "/" + expected;
			if (FileExists(candidate)) return candidate;
		}

		// 2. In-tree dev: the submodule's Output/Bin/<tag>/, a few levels up
		//    (examples/, build*/, examples/bin/).
		std::vector<std::string> roots = {
			exedir + "/../engine/ThirdParty/Kraut/Output/Bin",
			exedir + "/../../engine/ThirdParty/Kraut/Output/Bin",
			exedir + "/../../../engine/ThirdParty/Kraut/Output/Bin",
		};
		std::error_code ec;
		for (const auto &root : roots)
		{
			if (!std::filesystem::is_directory(root, ec))
				continue;
			for (auto it = std::filesystem::directory_iterator(root, ec);
				 !ec && it != std::filesystem::end(it); ++it)
			{
				if (!it->is_directory())
					continue;
				std::string candidate = it->path().string() + "/" + expected;
				if (FileExists(candidate))
					return candidate;
			}
		}

		return {};
	}

	KrautConverter::Result KrautConverter::ExportGlb(const std::string &descriptor_path,
		unsigned int seed, bool seedGiven, const std::string &output_dir)
	{
		Result r;
		const std::string binary = LocateBinary("KrautCLI");
		if (binary.empty())
		{
			r.stderr_capture = "KrautConverter: could not locate '" FURY_KRAUTCLI_BINARY
				"' (expected next to the fury executable; build the kraut_tools target)";
			return r;
		}
		if (!FileExists(descriptor_path))
		{
			r.stderr_capture = "KrautConverter: descriptor '" + descriptor_path + "' does not exist";
			return r;
		}

		const std::string stem = Stem(descriptor_path);
		r.output_path = (output_dir.empty() ? stem : output_dir + "/" + stem) + ".glb";

		// Texture roots for the export: the descriptor's dir + parent
		// (repo descriptors live in Data/Content/Trees), and the vendored
		// submodule's Data/Content (for the committed sample descriptors,
		// which are byte copies of the submodule's).
		std::vector<std::string> args = {
			"export", descriptor_path,
			"--format", "glb",
			"--out", r.output_path,
			"--json",
		};
		const std::string descDir = DirOf(descriptor_path);
		if (!descDir.empty())
		{
			args.push_back("--data");
			args.push_back(descDir);
			const std::string descParent = DirOf(descDir);
			if (!descParent.empty())
			{
				args.push_back("--data");
				args.push_back(descParent);
			}
		}
		const std::string exe = FileUtil::GetExecutablePath();
		const std::string exedir = exe.empty() ? "" : DirOf(exe);
		if (!exedir.empty())
		{
			// examples/fury -> repo/engine/ThirdParty/Kraut/Data/Content
			const std::string krautData = exedir + "/../engine/ThirdParty/Kraut/Data/Content";
			if (FileExists(krautData))
			{
				args.push_back("--data");
				args.push_back(krautData);
			}
		}
		if (seedGiven)
		{
			args.push_back("--seed");
			args.push_back(std::to_string(seed));
		}
		r.exit_code = RunSubprocessCapture(binary, args, r.stdout_capture, r.stderr_capture);

		if (r.exit_code != 0 || !FileExists(r.output_path))
		{
			if (r.exit_code == 0 && !FileExists(r.output_path))
				r.stderr_capture += "\nKrautConverter: subprocess exited 0 but '"
					+ r.output_path + "' was not written";
			r.output_path.clear();
		}
		return r;
	}

	KrautConverter::Result KrautConverter::BakeAtlas(const std::string &descriptor_path,
		unsigned int seed, bool seedGiven, const std::string &output_dir, int atlasCols)
	{
		Result r;
		const std::string binary = LocateBinary("KrautPreview");
		if (binary.empty())
		{
			r.stderr_capture = "KrautConverter: could not locate '" FURY_KRAUTPREVIEW_BINARY
				"' (expected next to the fury executable; build kraut_tools with FURY_WITH_KRAUT_PREVIEW=ON)";
			return r;
		}

		const std::string stem = Stem(descriptor_path);
		r.output_path = (output_dir.empty() ? stem : output_dir + "/" + stem) + "_BillboardAtlas.png";

		// Texture roots for the preview subprocess: the descriptor's parent
		// dir (covers repo descriptors under Data/Content/Trees ->
		// Data/Content) plus the vendored submodule's Data/Content (for the
		// committed sample descriptors, which are byte copies of the
		// submodule's) plus the output dir (textures copied there by the
		// generate flow).
		const std::string descParent = DirOf(DirOf(descriptor_path));
		std::vector<std::string> args = {
			descriptor_path,
			"--atlas", r.output_path,
			"--atlas-cols", std::to_string(atlasCols),
		};
		if (!descParent.empty())
		{
			args.push_back("--data");
			args.push_back(descParent);
		}
		const std::string exe = FileUtil::GetExecutablePath();
		const std::string exedir = exe.empty() ? "" : DirOf(exe);
		if (!exedir.empty())
		{
			const std::string krautData = exedir + "/../engine/ThirdParty/Kraut/Data/Content";
			if (FileExists(krautData))
			{
				args.push_back("--data");
				args.push_back(krautData);
			}
		}
		if (seedGiven)
		{
			args.push_back("--seed");
			args.push_back(std::to_string(seed));
		}
		r.exit_code = RunSubprocessCapture(binary, args, r.stdout_capture, r.stderr_capture);

		if (r.exit_code != 0 || !FileExists(r.output_path))
		{
			if (r.exit_code == 0 && !FileExists(r.output_path))
				r.stderr_capture += "\nKrautConverter: subprocess exited 0 but '"
					+ r.output_path + "' was not written";
			r.output_path.clear();
		}
		return r;
	}

	KrautConverter::Result KrautConverter::PreviewScreenshots(const std::string &descriptor_path,
		unsigned int seed, bool seedGiven, const std::string &output_dir)
	{
		Result r;
		const std::string binary = LocateBinary("KrautPreview");
		if (binary.empty())
		{
			r.stderr_capture = "KrautConverter: could not locate '" FURY_KRAUTPREVIEW_BINARY
				"' (previews skipped; atlas/generation are unaffected)";
			return r;
		}

		const std::string stem = Stem(descriptor_path);
		const std::string prefix = output_dir.empty() ? stem : output_dir + "/" + stem;

		// one screenshot per Kraut LOD slot (slot 0 = full detail; the glb's
		// tier numbering matches the slot order). KrautPreview's --lod takes
		// "none" for slot 0 and 0..4 for slots 1..5.
		const std::string descParent = DirOf(DirOf(descriptor_path));
		const std::string exe = FileUtil::GetExecutablePath();
		const std::string exedir = exe.empty() ? "" : DirOf(exe);
		std::string krautData;
		if (!exedir.empty())
			krautData = exedir + "/../engine/ThirdParty/Kraut/Data/Content";

		// one screenshot per Kraut LOD slot (slot 0 = full detail; the glb's
		// tier numbering matches the slot order). KrautPreview's --lod takes
		// "none" for slot 0 and 0..4 for slots 1..5.
		std::vector<std::string> produced;
		for (int slot = 0; slot < 6; ++slot)
		{
			const std::string out = prefix + "_tier" + std::to_string(slot) + ".png";
			std::vector<std::string> args = {
				descriptor_path,
				"--lod", slot == 0 ? "none" : std::to_string(slot - 1),
				"--screenshot", out,
				"--width", "512", "--height", "512",
			};
			if (!descParent.empty())
			{
				args.push_back("--data");
				args.push_back(descParent);
			}
			if (!krautData.empty() && FileExists(krautData))
			{
				args.push_back("--data");
				args.push_back(krautData);
			}
			if (seedGiven)
			{
				args.push_back("--seed");
				args.push_back(std::to_string(seed));
			}
			std::string outS, errS;
			const int code = RunSubprocessCapture(binary, args, outS, errS);
			if (code == 0 && FileExists(out))
				produced.push_back(out);
			else
			{
				// first failure ends the series (a missing/failed LOD means
				// the descriptor has no further full-mesh tiers)
				if (!errS.empty())
					r.stderr_capture += errS;
				break;
			}
		}
		r.output_path = produced.empty() ? "" : produced.front();
		r.exit_code = produced.empty() ? 4 : 0;
		return r;
	}
}
