#include "Fury/ShaderCompiler.h"

#if defined(FURY_WITH_SLANG) && FURY_WITH_SLANG

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <regex>
#include <sstream>
#include <unordered_map>

#include <slang.h>
#include <slang-deprecated.h>
#include <slang-com-ptr.h>

#include "SPIRV-Cross/spirv_msl.hpp"

namespace fury
{
	namespace
	{
		std::mutex s_CompileMutex;

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

		struct UniformDefault
		{
			bool integer = false;
			int components = 1;
			std::vector<float> values;
		};

		std::string StripComments(const std::string &source)
		{
			std::string out = source;
			bool line = false, block = false;
			for (size_t i = 0; i < out.size(); ++i)
			{
				if (!line && !block && i + 1 < out.size() && out[i] == '/' && out[i + 1] == '/')
				{
					line = true;
					out[i] = out[i + 1] = ' ';
					++i;
				}
				else if (!line && !block && i + 1 < out.size() && out[i] == '/' && out[i + 1] == '*')
				{
					block = true;
					out[i] = out[i + 1] = ' ';
					++i;
				}
				else if (line && out[i] == '\n') line = false;
				else if (block && i + 1 < out.size() && out[i] == '*' && out[i + 1] == '/')
				{
					out[i] = out[i + 1] = ' ';
					++i;
					block = false;
				}
				else if ((line || block) && out[i] != '\n') out[i] = ' ';
			}
			return out;
		}

		std::unordered_map<std::string, UniformDefault> ParseUniformDefaults(const std::string &source,
			std::string &error)
		{
			std::unordered_map<std::string, UniformDefault> out;
			const std::string stripped = StripComments(source);
			const std::regex pattern("\\buniform\\s+(float|int|vec[234]|ivec[234])\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^;]+);");
			for (std::sregex_iterator it(stripped.begin(), stripped.end(), pattern), end; it != end; ++it)
			{
				const std::string type = (*it)[1].str();
				const std::string name = (*it)[2].str();
				std::string expression = (*it)[3].str();
				UniformDefault value;
				value.integer = type == "int" || type.compare(0, 4, "ivec") == 0;
				value.components = type.size() > 1 && type.back() >= '2' && type.back() <= '4'
					? type.back() - '0' : 1;
				const size_t open = expression.find('(');
				if (open != std::string::npos)
				{
					const size_t close = expression.rfind(')');
					if (close == std::string::npos || expression.substr(0, open) != type)
					{
						error = "unsupported default initializer for " + name + ": " + expression;
						return {};
					}
					expression = expression.substr(open + 1, close - open - 1);
				}
				std::stringstream stream(expression);
				std::string token;
				while (std::getline(stream, token, ','))
				{
					char *end = nullptr;
					const float v = std::strtof(token.c_str(), &end);
					if (end == token.c_str())
					{
						error = "unsupported default initializer for " + name + ": " + expression;
						return {};
					}
					while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
					if (*end != '\0')
					{
						error = "unsupported default initializer for " + name + ": " + expression;
						return {};
					}
					value.values.push_back(v);
				}
				if (value.values.size() == 1 && value.components > 1)
					value.values.assign(value.components, value.values.front());
				if (value.values.size() != static_cast<size_t>(value.components))
				{
					error = "wrong component count in default initializer for " + name;
					return {};
				}
				const auto existing = out.find(name);
				if (existing != out.end() && (existing->second.integer != value.integer
					|| existing->second.components != value.components || existing->second.values != value.values))
				{
					error = "conflicting default initializers for " + name;
					return {};
				}
				out[name] = std::move(value);
			}
			const std::regex anyDefault("\\buniform\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+([A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\[[^\\]]+\\])?\\s*=");
			for (std::sregex_iterator it(stripped.begin(), stripped.end(), anyDefault), end; it != end; ++it)
			{
				const std::string name = (*it)[2].str();
				if (out.find(name) == out.end())
				{
					error = "unsupported default initializer for " + name;
					return {};
				}
			}
			return out;
		}

		std::vector<unsigned char> DefaultBytes(const UniformDefault &value)
		{
			std::vector<unsigned char> out(value.values.size() * 4);
			for (size_t i = 0; i < value.values.size(); ++i)
			{
				if (value.integer)
				{
					const int32_t v = static_cast<int32_t>(value.values[i]);
					std::memcpy(out.data() + i * 4, &v, 4);
				}
				else
				{
					const float v = value.values[i];
					std::memcpy(out.data() + i * 4, &v, 4);
				}
			}
			return out;
		}

		bool CompileStage(slang::IGlobalSession *global, const ShaderCompileInput &in, const std::string &source,
			const char *stageDefine, const char *stageName, const char *entryName,
			ShaderStageCompile &out, std::string &error)
		{
			std::string defaultError;
			const auto defaults = ParseUniformDefaults(source, defaultError);
			if (!defaultError.empty())
			{
				error = in.name + ": " + defaultError;
				return false;
			}
			const std::string stem = Sanitize(in.name) + "_" + stageName;
			slang::TargetDesc target = {};
			target.format = SLANG_SPIRV;
			slang::SessionDesc sessionDesc = {};
			sessionDesc.targets = &target;
			sessionDesc.targetCount = 1;
			slang::ISession *sessionRaw = nullptr;
			if (SLANG_FAILED(global->createSession(sessionDesc, &sessionRaw)))
			{
				error = in.name + ": cannot create Slang session";
				return false;
			}
			Slang::ComPtr<slang::ISession> session(Slang::INIT_ATTACH, sessionRaw);
			SlangCompileRequest *requestRaw = nullptr;
			if (SLANG_FAILED(session->createCompileRequest(&requestRaw)))
			{
				error = in.name + ": cannot create Slang compile request";
				return false;
			}
			Slang::ComPtr<slang::ICompileRequest> request(Slang::INIT_ATTACH, requestRaw);
			request->setCodeGenTarget(SLANG_SPIRV);
			request->setMatrixLayoutMode(SLANG_MATRIX_LAYOUT_ROW_MAJOR);
			for (const auto &path : in.searchPaths)
				request->addSearchPath(path.c_str());
			const std::string stageSource = InjectDefines(source, stageDefine, in.defines);
			const int unit = request->addTranslationUnit(SLANG_SOURCE_LANGUAGE_GLSL, Sanitize(in.name).c_str());
			request->addTranslationUnitSourceString(unit, (stem + ".glsl").c_str(), stageSource.c_str());
			const SlangStage stage = std::strcmp(stageName, "vertex") == 0 ? SLANG_STAGE_VERTEX : SLANG_STAGE_FRAGMENT;
			request->addEntryPoint(unit, "main", stage);
			SlangResult rc = request->compile();
			if (SLANG_FAILED(rc))
			{
				error = in.name + ": Slang compile failed (" + stageDefine + ")\n" + request->getDiagnosticOutput();
				return false;
			}
			Slang::ComPtr<slang::IBlob> code;
			rc = request->getEntryPointCodeBlob(0, 0, code.writeRef());
			if (SLANG_FAILED(rc))
			{
				error = in.name + ": Slang output failed (" + stageDefine + ")\n" + request->getDiagnosticOutput();
				return false;
			}
			const auto *bytes = static_cast<const unsigned char *>(code->getBufferPointer());
			std::vector<unsigned char> spirv(bytes, bytes + code->getBufferSize());

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
			out.spirv = spirv;

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
				if (resources.uniform_buffers.size() > 1)
				{
					error = in.name + ": expected at most one globals UBO (" + stageDefine + ")";
					return false;
				}
				if (!resources.uniform_buffers.empty())
				{
					const auto &block = resources.uniform_buffers[0];
					out.globalsBlockName = compiler.get_name(block.id);
					if (out.globalsBlockName != "globalParams")
					{
						error = in.name + ": unexpected uniform block '" + out.globalsBlockName + "'";
						return false;
					}
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
						if (member.offset + member.size > out.globalsBlockSize)
						{
							error = in.name + ": globals member '" + member.name + "' exceeds the MSL block";
							return false;
						}
						const auto defaultIt = defaults.find(member.name);
						if (defaultIt != defaults.end())
						{
							member.defaultValue = DefaultBytes(defaultIt->second);
							if (member.defaultValue.size() > member.size)
							{
								error = in.name + ": default for '" + member.name + "' exceeds its reflected size";
								return false;
							}
						}
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

			return true;
		}
		class SlangShaderCompiler final : public IShaderCompiler
		{
		public:
			SlangShaderCompiler()
			{
				SlangGlobalSessionDesc desc = {};
				desc.enableGLSL = true;
				slang::IGlobalSession *global = nullptr;
				if (SLANG_FAILED(slang_createGlobalSession2(&desc, &global)))
					return;
				m_Global.attach(global);
			}

			bool Compile(const ShaderCompileInput &in, ShaderModuleCompile &out) override
			{
				std::lock_guard<std::mutex> lock(s_CompileMutex);
				if (m_Global == nullptr)
				{
					out.error = in.name + ": cannot initialize Slang";
					return false;
				}
				if (!CompileStage(m_Global.get(), in, in.vertexSource, "VERTEX_SHADER", "vertex", "vertexMain", out.vertex, out.error))
					return false;
				if (!CompileStage(m_Global.get(), in, in.fragmentSource, "FRAGMENT_SHADER", "fragment", "fragmentMain", out.fragment, out.error))
					return false;
				out.ok = true;
				return true;
			}

		private:
			Slang::ComPtr<slang::IGlobalSession> m_Global;
		};
	}

	IShaderCompiler &GetShaderCompiler()
	{
		static SlangShaderCompiler compiler;
		return compiler;
	}

	bool CompileGlslToMSL(const ShaderCompileInput &in, ShaderModuleCompile &out)
	{
		return GetShaderCompiler().Compile(in, out);
	}
}

#else

namespace fury
{
	namespace
	{
		class UnsupportedShaderCompiler final : public IShaderCompiler
		{
		public:
			bool Compile(const ShaderCompileInput &in, ShaderModuleCompile &out) override
			{
				out.error = in.name + ": built without Slang (FURY_WITH_SLANG=OFF)";
				return false;
			}
		};
	}

	IShaderCompiler &GetShaderCompiler()
	{
		static UnsupportedShaderCompiler compiler;
		return compiler;
	}

	bool CompileGlslToMSL(const ShaderCompileInput &in, ShaderModuleCompile &out)
	{
		out.error = in.name + ": built without Slang (FURY_WITH_SLANG=OFF)";
		return false;
	}
}

#endif // FURY_WITH_SLANG
