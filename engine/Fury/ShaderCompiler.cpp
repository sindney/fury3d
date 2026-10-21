#include "Fury/ShaderCompiler.h"

#if defined(FURY_WITH_SLANG) && FURY_WITH_SLANG

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "Fury/FileUtil.h"

#include "SPIRV-Cross/spirv_msl.hpp"

namespace fury
{
	namespace
	{
		std::mutex s_CompileMutex;

		std::string ShellQuote(const std::string &s)
		{
			std::string out = "'";
			for (char c : s)
			{
				if (c == '\'') out += "'\\''";
				else out += c;
			}
			out += "'";
			return out;
		}

		// slangc resolution: FURY_SLANG_CLI env first, then <exe dir>/slang/bin/slangc
		// (the vendored package layout staged beside each binary).
		std::string ResolveSlangc()
		{
			if (const char *env = std::getenv("FURY_SLANG_CLI"))
			{
				if (env[0] != '\0')
					return env;
			}
			const std::string exe = FileUtil::GetExecutablePath();
			if (exe.empty())
				return "slangc";
			std::filesystem::path bin = std::filesystem::path(exe).parent_path() / "slang" / "bin" / "slangc";
		#if PLATFORM_WINDOWS
			bin += ".exe";
		#endif
			return bin.string();
		}

		std::filesystem::path MakeCompileTempDir()
		{
			std::filesystem::path base = std::filesystem::temp_directory_path() / "fury_shaderc";
			std::error_code ec;
			std::filesystem::create_directories(base, ec);
			return base;
		}

		// Injects defines right after the #version line (GLSL requires it first).
		std::string InjectDefines(const std::string &source, const std::string &stageDefine,
			const std::vector<std::pair<std::string, std::string>> &defines)
		{
			std::string injection = "#define " + stageDefine + "\n";
			for (const auto &d : defines)
			{
				injection += "#define " + d.first;
				if (!d.second.empty())
					injection += " " + d.second;
				injection += "\n";
			}

			const size_t versionPos = source.find("#version");
			if (versionPos == std::string::npos)
				return injection + source;
			const size_t lineEnd = source.find('\n', versionPos);
			if (lineEnd == std::string::npos)
				return source + "\n" + injection;
			return source.substr(0, lineEnd + 1) + injection + source.substr(lineEnd + 1);
		}

		std::string Sanitize(const std::string &name)
		{
			std::string out;
			out.reserve(name.size());
			for (char c : name)
				out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
			return out;
		}

		// SPIRV-Cross keeps the MSL-aware struct sizing protected; expose it.
		struct CompilerMSLEx : spirv_cross::CompilerMSL
		{
			using spirv_cross::CompilerMSL::CompilerMSL;

			uint32_t StructSizeMSL(const spirv_cross::SPIRType &type) const
			{
				return get_declared_struct_size_msl(type);
			}
		};

		bool CompileStage(const ShaderCompileInput &in, const std::string &source,
			const char *stageDefine, const char *stageName, const char *entryName,
			ShaderStageCompile &out, std::string &error)
		{
			const auto tmp = MakeCompileTempDir();
			const std::string stem = Sanitize(in.name) + "_" + stageName;
			const auto srcPath = tmp / (stem + ".glsl");
			const auto spvPath = tmp / (stem + ".spv");
			const auto logPath = tmp / (stem + ".log");

			{
				std::ofstream src(srcPath);
				src << InjectDefines(source, stageDefine, in.defines);
			}

			std::string cmd = ShellQuote(ResolveSlangc()) + " -target spirv -stage " + stageName + " -entry main";
			for (const auto &p : in.searchPaths)
				cmd += " -I " + ShellQuote(p);
			cmd += " " + ShellQuote(srcPath.string()) + " -o " + ShellQuote(spvPath.string())
				+ " > " + ShellQuote(logPath.string()) + " 2>&1";

			const int rc = std::system(cmd.c_str());
			std::string log;
			{
				std::ifstream inLog(logPath);
				log.assign(std::istreambuf_iterator<char>(inLog), std::istreambuf_iterator<char>());
			}
			if (rc != 0)
			{
				error = in.name + ": slangc failed (" + stageDefine + ", exit " + std::to_string(rc) + ")\n" + log;
				return false;
			}

			std::vector<unsigned char> spirv;
			{
				std::ifstream spv(spvPath, std::ios::binary);
				if (!spv.good())
				{
					error = in.name + ": slangc wrote no SPIR-V (" + stageDefine + ")\n" + log;
					return false;
				}
				spirv.assign(std::istreambuf_iterator<char>(spv), std::istreambuf_iterator<char>());
			}

			// Slang packs loose GLSL uniforms into a row-major globals UBO and
			// ignores -matrix-layout-column-major there; the engine's matrices
			// are GL column-major. Flip the decorations so SPIRV-Cross emits the
			// column-major multiply idiom against the engine's layout.
			{
				auto *words = reinterpret_cast<uint32_t *>(spirv.data());
				const size_t wordCount = spirv.size() / 4;
				size_t wi = 5;   // header
				while (wi < wordCount)
				{
					const uint32_t wc = words[wi] >> 16;
					const uint32_t op = words[wi] & 0xFFFF;
					if (wc == 0)
						break;
					if (op == 71 && wc >= 3 && words[wi + 2] == 4)        // OpDecorate RowMajor
						words[wi + 2] = 5;                                 // -> ColumnMajor
					else if (op == 72 && wc >= 4 && words[wi + 3] == 4)    // OpMemberDecorate RowMajor
						words[wi + 3] = 5;                                 // -> ColumnMajor
					wi += wc;
				}
			}
			if (!log.empty() && log.find("error") != std::string::npos)
			{
				error = in.name + ": slangc diagnostics (" + stageDefine + ")\n" + log;
				return false;
			}

			try
			{
				CompilerMSLEx compiler(
					reinterpret_cast<const uint32_t *>(spirv.data()), spirv.size() / 4);

				// Normalize the emitted entry to the engine's convention.
				for (const auto &ep : compiler.get_entry_points_and_stages())
				{
					if (ep.name != entryName)
						compiler.rename_entry_point(ep.name, entryName, ep.execution_model);
				}

				spirv_cross::CompilerMSL::Options options;
				options.set_msl_version(2, 3, 0);
				compiler.set_msl_options(options);
				out.msl = compiler.compile();

				auto resources = compiler.get_shader_resources();
				if (!resources.uniform_buffers.empty())
				{
					const auto &block = resources.uniform_buffers[0];
					out.globalsBlockName = compiler.get_name(block.id);
					out.globalsBlockIndex = static_cast<int>(compiler.get_decoration(block.id, spv::DecorationBinding));
					const auto &type = compiler.get_type(block.base_type_id);
					// MSL-side struct size (the validation target); member
					// offsets are std140 (the emitter pads to match).
					out.globalsBlockSize = compiler.StructSizeMSL(type);
					const size_t memberCount = type.member_types.size();
					out.globals.reserve(memberCount);
					for (size_t i = 0; i < memberCount; ++i)
					{
						ShaderStageCompile::Member member;
						member.name = compiler.get_member_name(block.base_type_id, static_cast<uint32_t>(i));
						member.offset = compiler.type_struct_member_offset(type, static_cast<uint32_t>(i));
						member.size = compiler.get_declared_struct_member_size(type, static_cast<uint32_t>(i));
						out.globals.push_back(std::move(member));
					}
				}

				for (const auto &input : resources.stage_inputs)
				{
					ShaderStageCompile::StageInput si;
					si.name = compiler.get_name(input.id);
					si.location = static_cast<int>(compiler.get_decoration(input.id, spv::DecorationLocation));
					out.stageInputs.push_back(std::move(si));
				}
			}
			catch (const std::exception &e)
			{
				error = in.name + ": SPIRV-Cross failed (" + stageDefine + "): " + e.what();
				return false;
			}

			std::error_code ec;
			std::filesystem::remove(srcPath, ec);
			std::filesystem::remove(spvPath, ec);
			std::filesystem::remove(logPath, ec);
			return true;
		}
	}

	bool CompileGlslToMSL(const ShaderCompileInput &in, ShaderModuleCompile &out)
	{
		std::lock_guard<std::mutex> lock(s_CompileMutex);

		if (!CompileStage(in, in.vertexSource, "VERTEX_SHADER", "vertex", "vertexMain", out.vertex, out.error))
			return false;
		if (!CompileStage(in, in.fragmentSource, "FRAGMENT_SHADER", "fragment", "fragmentMain", out.fragment, out.error))
			return false;

		out.ok = true;
		return true;
	}
}

#else

namespace fury
{
	bool CompileGlslToMSL(const ShaderCompileInput &in, ShaderModuleCompile &out)
	{
		out.error = in.name + ": built without Slang (FURY_WITH_SLANG=OFF)";
		return false;
	}
}

#endif // FURY_WITH_SLANG
