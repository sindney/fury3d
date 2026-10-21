#ifndef _FURY_GL_SHADER_MODULE_H_
#define _FURY_GL_SHADER_MODULE_H_

#include <string>
#include <unordered_map>

#include "Fury/RHI/RHIResources.h"

namespace fury
{
	// GL program: compile + link of complete per-stage sources (version,
	// defines and stage defines already injected by the engine-side shader
	// preprocessing shared with the cross-compile path).
	class FURY_API GLShaderModule : public RHIShaderModule
	{
	protected:

		std::string m_Name;

		unsigned int m_Program = 0;

		bool m_IsCompute = false;

		// Cleared on every successful link.
		std::unordered_map<std::string, int> m_UniformLocations;

		mutable std::unordered_map<std::string, int> m_AttribLocations;

	public:

		// All sources are complete translation units. geometry empty = no GS.
		static std::shared_ptr<GLShaderModule> Create(const std::string &name,
			const std::string &vertex, const std::string &fragment, const std::string &geometry);

		static std::shared_ptr<GLShaderModule> CreateCompute(const std::string &name, const std::string &source);

		~GLShaderModule();

		bool IsValid() const override;

		RHIUniformHandle ResolveUniform(const std::string &name) override;

		int ResolveAttribLocation(const std::string &name) override;

		uintptr_t GetNativeHandle() const override;

		unsigned int GetProgram() const;

		bool IsCompute() const;

		int GetUniformLocation(const std::string &name);

		int GetAttribLocation(const std::string &name);

	protected:

		static unsigned int CompileStage(unsigned int glStage, const std::string &source, const char *stageName, const std::string &name);

		static bool Link(unsigned int program, const std::string &name);
	};

	typedef std::shared_ptr<GLShaderModule> GLShaderModuleRef;

	// GL "pipeline": shader module + raster/blend/depth state bundle. Vertex
	// layout is VAO-driven in GL, so VertexAttributes is unused here.
	class FURY_API GLGraphicsPipeline : public RHIGraphicsPipeline
	{
	protected:

		RHIGraphicsPipelineDesc m_Desc;

		GLShaderModuleRef m_Module;

	public:

		GLGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const GLShaderModuleRef &module)
			: m_Desc(desc), m_Module(module) {}

		const RHIGraphicsPipelineDesc &GetDesc() const override { return m_Desc; }

		RHIShaderModuleRef GetShaders() const override { return m_Module; }

		GLShaderModuleRef GetGLModule() const { return m_Module; }
	};

	typedef std::shared_ptr<GLGraphicsPipeline> GLGraphicsPipelineRef;
}

#endif // _FURY_GL_SHADER_MODULE_H_
