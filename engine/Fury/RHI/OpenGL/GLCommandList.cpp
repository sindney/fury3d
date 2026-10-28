#include "Fury/RHI/OpenGL/GLCommandList.h"

#include "Fury/RHI/OpenGL/GLInputAssembler.h"

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/RHI/OpenGL/GLEnumMap.h"
#include "Fury/RHI/OpenGL/GLResources.h"
#include "Fury/RHI/OpenGL/GLShaderModule.h"
#include "Fury/RHI/RHIDevice.h"

namespace fury
{
	GLCommandList::GLCommandList()
	{
		m_PrimitiveMode = GL_TRIANGLES;
	}

	GLCommandList::~GLCommandList()
	{
		if (m_BlitFBOs[0] != 0)
			glDeleteFramebuffers(2, m_BlitFBOs);
	}

	void GLCommandList::ClearBuffers(ClearMode mode, const Color &color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
		switch (mode)
		{
		case ClearMode::COLOR:
			glClear(GL_COLOR_BUFFER_BIT);
			break;
		case ClearMode::COLOR_DEPTH:
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			break;
		case ClearMode::COLOR_DEPTH_STENCIL:
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::COLOR_STENCIL:
			glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::DEPTH:
			glClear(GL_DEPTH_BUFFER_BIT);
			break;
		case ClearMode::STENCIL:
			glClear(GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::STENCIL_DEPTH:
			glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::NONE:
			break;
		}
	}

	void GLCommandList::BeginRenderTargets(const RHIRenderTargetSetRef &targets, ClearMode clear, const Color &clearColor)
	{
		auto *set = static_cast<GLRenderTargetSet *>(targets.get());
		for (unsigned int i = 0; i < set->GetColorCount(); ++i)
			GRHIDevice->TrackRenderTarget(set->GetColorTexture(i));
		if (set->GetDepthTexture() != nullptr)
			GRHIDevice->TrackRenderTarget(set->GetDepthTexture());
		glBindFramebuffer(GL_FRAMEBUFFER, set->GetFBO());
		glViewport(0, 0, set->GetWidth(), set->GetHeight());
		if (set->HasLayeredAttachments())
			set->ClearAllLayers(clear, clearColor);
		else
			ClearBuffers(clear, clearColor);
	}

	void GLCommandList::BeginBackbuffer(int width, int height, ClearMode clear, const Color &clearColor)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, width, height);
		ClearBuffers(clear, clearColor);
	}

	void GLCommandList::Clear(ClearMode mode, const Color &color)
	{
		ClearBuffers(mode, color);
	}

	void GLCommandList::ClearColorUInt(unsigned int value)
	{
		const GLuint v[4] = { value, value, value, value };
		glClearBufferuiv(GL_COLOR, 0, v);
	}

	void GLCommandList::EndRenderTargets()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void GLCommandList::SetViewport(int x, int y, int width, int height)
	{
		glViewport(x, y, width, height);
	}

	void GLCommandList::SetScissor(bool enabled, int x, int y, int width, int height)
	{
		if (!enabled)
		{
			glDisable(GL_SCISSOR_TEST);
			return;
		}
		glEnable(GL_SCISSOR_TEST);
		glScissor(x, y, width, height);
	}

	void GLCommandList::SetPipeline(const RHIGraphicsPipelineRef &pipeline)
	{
		m_CurrentPipeline = pipeline;
		auto *glPipeline = static_cast<GLGraphicsPipeline *>(pipeline.get());
		BindModule(glPipeline->GetShaders());

		const auto &desc = glPipeline->GetDesc();
		SetRasterState(desc.Blend, desc.DepthFunc, desc.DepthTest, desc.DepthWrite, desc.Cull);
		SetPrimitiveMode(desc.Primitive);
		SetFillMode(desc.Fill);
	}

	void GLCommandList::BindShaderModule(const RHIShaderModuleRef &module)
	{
		BindModule(module);
	}

	GLShaderModule *GLCommandList::CurrentGLModule() const
	{
		return static_cast<GLShaderModule *>(m_CurrentModule.get());
	}

	void GLCommandList::BindModule(const RHIShaderModuleRef &module)
	{
		m_CurrentModule = module;
		m_TextureUnit = 0;
		auto *glModule = CurrentGLModule();
		glUseProgram(glModule != nullptr ? glModule->GetProgram() : 0);
	}

	void GLCommandList::SetRasterState(BlendMode blend, CompareMode depthFunc, bool depthTest, bool depthWrite, CullMode cull)
	{
		SetBlendMode(blend);
		SetDepthState(depthFunc, depthTest, depthWrite);
		SetCullMode(cull);
	}

	void GLCommandList::SetBlendMode(BlendMode mode)
	{
		if (mode != BlendMode::REPLACE)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GLEnumMap::BlendModeSrcToGL(mode), GLEnumMap::BlendModeDestToGL(mode));
			glBlendEquation(GLEnumMap::BlendModeOpToGL(mode));
		}
		else
		{
			glDisable(GL_BLEND);
		}
	}

	void GLCommandList::SetDepthState(CompareMode func, bool test, bool write)
	{
		if (test)
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GLEnumMap::CompareModeToGL(func));
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}
		glDepthMask(write ? GL_TRUE : GL_FALSE);
	}

	void GLCommandList::SetCullMode(CullMode mode)
	{
		auto cullMode = GLEnumMap::CullModeToGL(mode);
		if (cullMode.first)
		{
			glEnable(GL_CULL_FACE);
			glCullFace(cullMode.second);
		}
		else
		{
			glDisable(GL_CULL_FACE);
		}
	}

	void GLCommandList::SetColorWriteEnabled(bool enabled)
	{
		GLboolean flag = enabled ? GL_TRUE : GL_FALSE;
		glColorMask(flag, flag, flag, flag);
	}

	void GLCommandList::SetBlendEnabled(unsigned int attachment, bool enabled)
	{
		if (enabled)
			glEnablei(GL_BLEND, attachment);
		else
			glDisablei(GL_BLEND, attachment);
	}

	void GLCommandList::SetSRGBWriteEnabled(bool enabled)
	{
		if (enabled)
			glEnable(GL_FRAMEBUFFER_SRGB);
		else
			glDisable(GL_FRAMEBUFFER_SRGB);
	}

	void GLCommandList::PushRenderTarget()
	{
		TargetState state;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.FBO);
		glGetIntegerv(GL_VIEWPORT, state.Viewport);
		m_TargetStack.push_back(state);
	}

	void GLCommandList::PopRenderTarget()
	{
		if (m_TargetStack.empty())
			return;
		const TargetState state = m_TargetStack.back();
		m_TargetStack.pop_back();
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)state.FBO);
		glViewport(state.Viewport[0], state.Viewport[1], state.Viewport[2], state.Viewport[3]);
	}

	void GLCommandList::SetVertexBuffer(const RHIBufferRef &buffer, unsigned int stream)
	{
		// VAO-driven attribute setup happens at the draw call sites; vertex
		// buffers bound through this path are no-ops until then.
	}

	void GLCommandList::SetIndexBuffer(const RHIBufferRef &buffer)
	{
		if (buffer != nullptr)
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)buffer->GetNativeHandle());
	}

	RHIUniformHandle GLCommandList::ResolveUniform(const std::string &name)
	{
		auto *module = CurrentGLModule();
		if (module == nullptr)
			return -1;
		return module->GetUniformLocation(name);
	}

	void GLCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x)
	{
		if (handle >= 0) glUniform1f(handle, x);
	}

	void GLCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y)
	{
		if (handle >= 0) glUniform2f(handle, x, y);
	}

	void GLCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z)
	{
		if (handle >= 0) glUniform3f(handle, x, y, z);
	}

	void GLCommandList::SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z, float w)
	{
		if (handle >= 0) glUniform4f(handle, x, y, z, w);
	}

	void GLCommandList::SetUniformIntAt(RHIUniformHandle handle, int x)
	{
		if (handle >= 0) glUniform1i(handle, x);
	}

	void GLCommandList::SetUniformMatrixAt(RHIUniformHandle handle, const float *raw)
	{
		if (handle >= 0) glUniformMatrix4fv(handle, 1, GL_FALSE, raw);
	}

	void GLCommandList::SetUniformMatricesAt(RHIUniformHandle handle, int count, const float *raw)
	{
		if (handle >= 0) glUniformMatrix4fv(handle, count, GL_FALSE, raw);
	}

	void GLCommandList::SetUniformFloat(const std::string &name, float v0)
	{
		SetUniformFloatAt(ResolveUniform(name), v0);
	}

	void GLCommandList::SetUniformFloat(const std::string &name, float v0, float v1)
	{
		SetUniformFloatAt(ResolveUniform(name), v0, v1);
	}

	void GLCommandList::SetUniformFloat(const std::string &name, float v0, float v1, float v2)
	{
		SetUniformFloatAt(ResolveUniform(name), v0, v1, v2);
	}

	void GLCommandList::SetUniformFloat(const std::string &name, float v0, float v1, float v2, float v3)
	{
		SetUniformFloatAt(ResolveUniform(name), v0, v1, v2, v3);
	}

	void GLCommandList::SetUniformFloatArray(const std::string &name, int size, int count, const float *value)
	{
		int location = ResolveUniform(name);
		if (location < 0)
			return;
		switch (size)
		{
		case 1: glUniform1fv(location, count, value); break;
		case 2: glUniform2fv(location, count, value); break;
		case 3: glUniform3fv(location, count, value); break;
		case 4: glUniform4fv(location, count, value); break;
		}
	}

	void GLCommandList::SetUniformInt(const std::string &name, int v0)
	{
		SetUniformIntAt(ResolveUniform(name), v0);
	}

	void GLCommandList::SetUniformInt(const std::string &name, int v0, int v1)
	{
		int location = ResolveUniform(name);
		if (location >= 0) glUniform2i(location, v0, v1);
	}

	void GLCommandList::SetUniformInt(const std::string &name, int v0, int v1, int v2)
	{
		int location = ResolveUniform(name);
		if (location >= 0) glUniform3i(location, v0, v1, v2);
	}

	void GLCommandList::SetUniformInt(const std::string &name, int v0, int v1, int v2, int v3)
	{
		int location = ResolveUniform(name);
		if (location >= 0) glUniform4i(location, v0, v1, v2, v3);
	}

	void GLCommandList::SetUniformIntArray(const std::string &name, int size, int count, const int *value)
	{
		int location = ResolveUniform(name);
		if (location < 0)
			return;
		switch (size)
		{
		case 1: glUniform1iv(location, count, value); break;
		case 2: glUniform2iv(location, count, value); break;
		case 3: glUniform3iv(location, count, value); break;
		case 4: glUniform4iv(location, count, value); break;
		}
	}

	void GLCommandList::SetUniformUInt(const std::string &name, unsigned int v0)
	{
		int location = ResolveUniform(name);
		if (location >= 0 && glUniform1ui != nullptr)
			glUniform1ui(location, v0);
	}

	void GLCommandList::SetUniformMatrix(const std::string &name, const Matrix4 &matrix)
	{
		SetUniformMatrixAt(ResolveUniform(name), &matrix.Raw[0]);
	}

	void GLCommandList::SetUniformMatrix(const std::string &name, const float *raw)
	{
		SetUniformMatrixAt(ResolveUniform(name), raw);
	}

	void GLCommandList::SetUniformMatrices(const std::string &name, int count, const float *raw)
	{
		SetUniformMatricesAt(ResolveUniform(name), count, raw);
	}

	void GLCommandList::SetTexture(const std::string &name, const RHITextureRef &texture)
	{
		SetTextureAt(ResolveUniform(name), texture);
	}

	int GLCommandList::SetTextureAt(RHIUniformHandle handle, const RHITextureRef &texture)
	{
		// A miss leaves the cursor untouched (the cache-replay walk counts on it).
		if (handle < 0 || texture == nullptr)
			return (int)m_TextureUnit;

		auto *glTexture = static_cast<GLTexture *>(texture.get());
		glActiveTexture(GL_TEXTURE0 + m_TextureUnit);
		glBindTexture(glTexture->GetTarget(), glTexture->GetID());
		glUniform1i(handle, m_TextureUnit);
		m_TextureUnit++;
		return m_TextureUnit;
	}

	void GLCommandList::SetTextureUnitCursor(int unitOffset)
	{
		m_TextureUnit = (unsigned int)unitOffset;
	}

	void GLCommandList::BindTextureUnit(const RHITextureRef &texture)
	{
		if (texture == nullptr)
			return;
		auto *glTexture = static_cast<GLTexture *>(texture.get());
		glActiveTexture(GL_TEXTURE0 + m_TextureUnit);
		glBindTexture(glTexture->GetTarget(), glTexture->GetID());
	}

	void GLCommandList::SetMeshGeometry(const RHIMeshGeometryDesc &desc)
	{
		GLBindMeshGeometry(m_MeshVAO, desc);
	}

	void GLCommandList::SetInstanceMatrixStream(const RHIBufferRef &buffer, const char *const attrNames[4])
	{
		if (buffer != nullptr)
			GLBindInstanceMatrixAttribs(m_CurrentModule, buffer, attrNames);
		else
			GLUnbindInstanceMatrixAttribs(m_CurrentModule, attrNames);
	}

	void GLCommandList::SetStorageBuffer(unsigned int binding, const RHIBufferRef &buffer)
	{
		if (glBindBufferBase == nullptr)
			return;
		// nullptr unbinds the binding point.
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding,
			buffer != nullptr ? (GLuint)buffer->GetNativeHandle() : 0);
	}

	void GLCommandList::SetPrimitiveMode(RHIPrimitiveMode mode)
	{
		switch (mode)
		{
		case RHIPrimitiveMode::LINES: m_PrimitiveMode = GL_LINES; break;
		case RHIPrimitiveMode::LINE_STRIP: m_PrimitiveMode = GL_LINE_STRIP; break;
		case RHIPrimitiveMode::LINE_LOOP: m_PrimitiveMode = GL_LINE_LOOP; break;
		case RHIPrimitiveMode::TRIANGLES:
		default: m_PrimitiveMode = GL_TRIANGLES; break;
		}
	}

	void GLCommandList::SetFillMode(RHIFillMode mode)
	{
		glPolygonMode(GL_FRONT_AND_BACK, mode == RHIFillMode::WIREFRAME ? GL_LINE : GL_FILL);
	}

	void GLCommandList::SetDepthBias(float factor, float units)
	{
		if (factor == 0.0f && units == 0.0f)
		{
			glDisable(GL_POLYGON_OFFSET_FILL);
			return;
		}
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(factor, units);
	}

	void GLCommandList::Draw(unsigned int vertexCount, unsigned int firstVertex)
	{
		glDrawArrays(m_PrimitiveMode, firstVertex, vertexCount);
	}

	void GLCommandList::DrawIndexed(unsigned int indexCount, unsigned int firstIndex, int vertexOffset)
	{
		glDrawElements(m_PrimitiveMode, (GLsizei)indexCount, GL_UNSIGNED_INT,
			(const void *)(uintptr_t)(firstIndex * sizeof(unsigned int)));
	}

	void GLCommandList::DrawInstanced(unsigned int vertexCount, unsigned int instanceCount, unsigned int firstVertex)
	{
		glDrawArraysInstanced(m_PrimitiveMode, firstVertex, vertexCount, instanceCount);
	}

	void GLCommandList::DrawIndexedInstanced(unsigned int indexCount, unsigned int instanceCount, unsigned int firstIndex, int vertexOffset)
	{
		glDrawElementsInstanced(m_PrimitiveMode, (GLsizei)indexCount, GL_UNSIGNED_INT,
			(const void *)(uintptr_t)(firstIndex * sizeof(unsigned int)), instanceCount);
	}

	void GLCommandList::Dispatch(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ)
	{
		if (glDispatchCompute == nullptr)
		{
			FURYW << "Dispatch: compute entry points missing";
			return;
		}
		glDispatchCompute(groupsX, groupsY, groupsZ);
	}

	void GLCommandList::Barrier()
	{
		if (glMemoryBarrier == nullptr)
			return;
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			| GL_TEXTURE_FETCH_BARRIER_BIT
			| GL_SHADER_STORAGE_BARRIER_BIT
			| GL_TEXTURE_UPDATE_BARRIER_BIT);
	}

	void GLCommandList::Blit(const RHITextureRef &src, const RHITextureRef &dst, int srcWidth, int srcHeight, int dstWidth, int dstHeight, bool linearFilter)
	{
		if (src == nullptr || dst == nullptr)
			return;

		if (m_BlitFBOs[0] == 0)
			glGenFramebuffers(2, m_BlitFBOs);

		auto *srcTexture = static_cast<GLTexture *>(src.get());
		auto *dstTexture = static_cast<GLTexture *>(dst.get());

		auto attachFor = [](unsigned int fbo, unsigned int target, GLTexture *texture) {
			const auto &desc = texture->GetDesc();
			const bool isDepth = desc.Format == TextureFormat::DEPTH16 || desc.Format == TextureFormat::DEPTH24
				|| desc.Format == TextureFormat::DEPTH32F;
			const bool isDepthStencil = desc.Format == TextureFormat::DEPTH24_STENCIL8
				|| desc.Format == TextureFormat::DEPTH32F_STENCIL8;
			if (isDepthStencil)
				glFramebufferTexture2D(target, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, texture->GetID(), 0);
			else if (isDepth)
				glFramebufferTexture2D(target, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture->GetID(), 0);
			else
				glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->GetID(), 0);
			return isDepth || isDepthStencil;
		};

		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_BlitFBOs[0]);
		bool srcDepth = attachFor(m_BlitFBOs[0], GL_READ_FRAMEBUFFER, srcTexture);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_BlitFBOs[1]);
		attachFor(m_BlitFBOs[1], GL_DRAW_FRAMEBUFFER, dstTexture);

		GLbitfield mask = srcDepth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;
		glBlitFramebuffer(0, 0, srcWidth, srcHeight, 0, 0, dstWidth, dstHeight, mask,
			linearFilter && !srcDepth ? GL_LINEAR : GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	}
}
