#ifndef _FURY_SHADER_COMPILER_H_
#define _FURY_SHADER_COMPILER_H_

#include <string>
#include <vector>

#include "Fury/Macros.h"

namespace fury
{
	// One stage of a compiled shader module: MSL source + the reflection data
	// the Metal backend binds with. Loose GLSL uniforms arrive packed into a
	// single globals block (Slang's "globalParams"); textures/samplers/buffers
	// resolve by name at PSO reflection time (runtime), so only the globals
	// block's member map is needed here.
	struct ShaderStageCompile
	{
		std::string msl;

		// MSL argument name of the globals constant block ("globalParams");
		// empty when the stage has no loose uniforms.
		std::string globalsBlockName;

		// Byte size of the globals block (0 when absent).
		size_t globalsBlockSize = 0;

		// [[buffer(n)]] index of the globals block in the emitted MSL (the
		// SPIR-V binding); -1 when absent. Binds bypass PSO reflection maps:
		// the two stages' blocks share one argument name, which the merged
		// per-module maps cannot represent.
		int globalsBlockIndex = -1;

		struct Member
		{
			std::string name;

			size_t offset = 0;

			size_t size = 0;
		};

		std::vector<Member> globals;

		struct StageInput
		{
			std::string name;

			int location = -1;
		};

		// Vertex stage only: name -> MSL attribute index for stage_in inputs.
		std::vector<StageInput> stageInputs;
	};

	struct ShaderModuleCompile
	{
		bool ok = false;

		std::string error;

		ShaderStageCompile vertex;

		ShaderStageCompile fragment;
	};

	struct ShaderCompileInput
	{
		// Diagnostics identifier (shader name/path).
		std::string name;

		// GLSL translation units as the engine assembles them (include
		// resolution happens inside Slang via searchPaths; the stage define
		// (VERTEX_SHADER / FRAGMENT_SHADER) is injected by the compiler).
		std::string vertexSource;

		std::string fragmentSource;

		// Search paths for #include resolution (engine shader dirs).
		std::vector<std::string> searchPaths;

		// JSON permutation defines, applied to both stages.
		std::vector<std::pair<std::string, std::string>> defines;
	};

	// GLSL -> (Slang front-end) -> SPIR-V -> (SPIRV-Cross) -> MSL + reflection.
	// One compilation may run on any thread; calls are serialized internally.
	FURY_API bool CompileGlslToMSL(const ShaderCompileInput &in, ShaderModuleCompile &out);
}

#endif // _FURY_SHADER_COMPILER_H_
