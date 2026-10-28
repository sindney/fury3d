#ifndef _FURY_SHADER_CACHE_H_
#define _FURY_SHADER_CACHE_H_

#include <string>
#include <vector>

#include "Fury/Macros.h"
#include "Fury/ShaderCompiler.h"

namespace fury
{
	class DdcStore;

	enum class ShaderTarget
	{
		OpenGL,
		Vulkan,
		Metal
	};

	constexpr unsigned int FURY_SHADER_DDC_MASTER_VERSION = 3;

	struct ShaderArtifact
	{
		std::string key;
		std::string extension;
		std::vector<unsigned char> bytes;
	};

	const char *ShaderTargetName(ShaderTarget target);
	bool ParseShaderTarget(const std::string &name, ShaderTarget &target);

	// Loads a shader and recursively resolves quoted relative includes.
	bool LoadShaderSource(const std::string &path, std::string &source, std::string &error);

	// Assemble the same stage sources for runtime, cook, and cache keys.
	std::string BuildShaderStageSource(const std::string &source, const std::string &stageDefine,
		const std::vector<std::string> &defines, bool withEditor, bool withDebugOverlay);
	ShaderCompileInput BuildShaderCompileInput(const std::string &name, const std::string &source,
		const std::vector<std::string> &defines, bool withEditor, bool withDebugOverlay);

	std::string MakeShaderDdcKey(const ShaderCompileInput &input, ShaderTarget target);
	std::vector<ShaderArtifact> MakeShaderArtifacts(const ShaderCompileInput &input,
		const ShaderModuleCompile &compiled, ShaderTarget target);
	std::string ShaderArtifactPakKey(const ShaderArtifact &artifact, const DdcStore &ddc);

	bool StoreShaderArtifacts(DdcStore &ddc, const std::vector<ShaderArtifact> &artifacts, std::string &error);
	bool LoadShaderArtifacts(const DdcStore &ddc, const std::string &key, ShaderTarget target,
		std::vector<ShaderArtifact> &artifacts);

	// Vulkan descriptor-set contract: one set per stage, rewritten into the
	// SPIR-V (vertex 0, fragment 1, compute 0). Idempotent; fails when a
	// descriptor resource (uniform/storage buffer, sampled image) lacks a
	// DescriptorSet decoration.
	bool RewriteVulkanDescriptorSets(std::vector<unsigned char> &spirv, unsigned int set,
		const char *stage, std::string &error);

	// Loads target code + reflection from pak/DDC, compiling and storing on a
	// miss. Vulkan loads SPIR-V (MSL is a compile byproduct, not an artifact).
	bool CompileOrLoadMetalShader(const ShaderCompileInput &input, ShaderModuleCompile &out,
		bool *cacheHit, std::string &error);
	bool CompileOrLoadVulkanShader(const ShaderCompileInput &input, ShaderModuleCompile &out,
		bool *cacheHit, std::string &error);

	extern FURY_API int g_ShaderCompilerInvocations;
}

#endif // _FURY_SHADER_CACHE_H_
