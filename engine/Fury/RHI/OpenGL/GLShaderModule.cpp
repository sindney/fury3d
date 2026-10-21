#include "Fury/RHI/OpenGL/GLShaderModule.h"

#include "Fury/GLLoader.h"
#include "Fury/Log.h"

namespace fury
{
	unsigned int GLShaderModule::CompileStage(unsigned int glStage, const std::string &source, const char *stageName, const std::string &name)
	{
		unsigned int shader = glCreateShader(glStage);
		if (shader == 0)
		{
			FURYE << name << ": glCreateShader failed for " << stageName;
			return 0;
		}

		const char *src = source.c_str();
		const int len = (int)source.size();
		glShaderSource(shader, 1, &src, &len);
		glCompileShader(shader);

		GLint status;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
		if (status != GL_TRUE)
		{
			char logbuffer[4096];
			int logbufferLen;
			glGetShaderInfoLog(shader, sizeof(logbuffer), &logbufferLen, logbuffer);
			FURYE << name << "'s " << stageName << " compile failed!";
			FURYE << std::string(logbuffer, logbufferLen);
			glDeleteShader(shader);
			return 0;
		}
		return shader;
	}

	bool GLShaderModule::Link(unsigned int program, const std::string &name)
	{
		glLinkProgram(program);

		GLint status;
		glGetProgramiv(program, GL_LINK_STATUS, &status);
		if (status != GL_TRUE)
		{
			char logbuffer[4096];
			int logbufferLen;
			glGetProgramInfoLog(program, sizeof(logbuffer), &logbufferLen, logbuffer);
			FURYE << name << " link failed!";
			FURYE << std::string(logbuffer, logbufferLen);
			return false;
		}
		return true;
	}

	std::shared_ptr<GLShaderModule> GLShaderModule::Create(const std::string &name,
		const std::string &vertex, const std::string &fragment, const std::string &geometry)
	{
		unsigned int vs = CompileStage(GL_VERTEX_SHADER, vertex, "vertex shader", name);
		if (vs == 0)
			return nullptr;

		unsigned int fs = CompileStage(GL_FRAGMENT_SHADER, fragment, "fragment shader", name);
		if (fs == 0)
		{
			glDeleteShader(vs);
			return nullptr;
		}

		unsigned int gs = 0;
		if (!geometry.empty())
		{
			gs = CompileStage(GL_GEOMETRY_SHADER, geometry, "geometry shader", name);
			if (gs == 0)
			{
				glDeleteShader(vs);
				glDeleteShader(fs);
				return nullptr;
			}
		}

		unsigned int program = glCreateProgram();
		if (program == 0)
		{
			FURYE << name << ": glCreateProgram failed";
			glDeleteShader(vs);
			glDeleteShader(fs);
			if (gs) glDeleteShader(gs);
			return nullptr;
		}

		glAttachShader(program, vs);
		glAttachShader(program, fs);
		if (gs)
			glAttachShader(program, gs);

		bool linked = Link(program, name);

		glDetachShader(program, vs);
		glDetachShader(program, fs);
		glDeleteShader(vs);
		glDeleteShader(fs);
		if (gs)
		{
			glDetachShader(program, gs);
			glDeleteShader(gs);
		}

		if (!linked)
		{
			glDeleteProgram(program);
			return nullptr;
		}

		auto module = std::make_shared<GLShaderModule>();
		module->m_Name = name;
		module->m_Program = program;
		FURYD << name << " compile & link success!";
		return module;
	}

	std::shared_ptr<GLShaderModule> GLShaderModule::CreateCompute(const std::string &name, const std::string &source)
	{
		unsigned int cs = CompileStage(GL_COMPUTE_SHADER, source, "compute shader", name);
		if (cs == 0)
			return nullptr;

		unsigned int program = glCreateProgram();
		if (program == 0)
		{
			FURYE << name << ": glCreateProgram failed";
			glDeleteShader(cs);
			return nullptr;
		}

		glAttachShader(program, cs);
		bool linked = Link(program, name);
		glDetachShader(program, cs);
		glDeleteShader(cs);

		if (!linked)
		{
			glDeleteProgram(program);
			return nullptr;
		}

		auto module = std::make_shared<GLShaderModule>();
		module->m_Name = name;
		module->m_Program = program;
		module->m_IsCompute = true;
		FURYD << name << " compute compile & link success!";
		return module;
	}

	GLShaderModule::~GLShaderModule()
	{
		if (m_Program != 0)
			glDeleteProgram(m_Program);
	}

	bool GLShaderModule::IsValid() const
	{
		return m_Program != 0;
	}

	uintptr_t GLShaderModule::GetNativeHandle() const
	{
		return m_Program;
	}

	unsigned int GLShaderModule::GetProgram() const
	{
		return m_Program;
	}

	bool GLShaderModule::IsCompute() const
	{
		return m_IsCompute;
	}

	int GLShaderModule::GetUniformLocation(const std::string &name)
	{
		auto it = m_UniformLocations.find(name);
		if (it != m_UniformLocations.end())
			return it->second;

		int location = glGetUniformLocation(m_Program, name.c_str());
		m_UniformLocations[name] = location;
		return location;
	}

	int GLShaderModule::GetAttribLocation(const std::string &name)
	{
		auto it = m_AttribLocations.find(name);
		if (it != m_AttribLocations.end())
			return it->second;

		int location = glGetAttribLocation(m_Program, name.c_str());
		m_AttribLocations[name] = location;
		return location;
	}

	RHIUniformHandle GLShaderModule::ResolveUniform(const std::string &name)
	{
		return GetUniformLocation(name);
	}

	int GLShaderModule::ResolveAttribLocation(const std::string &name)
	{
		return GetAttribLocation(name);
	}
}
