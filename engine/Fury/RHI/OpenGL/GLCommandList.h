#ifndef _FURY_GL_COMMAND_LIST_H_
#define _FURY_GL_COMMAND_LIST_H_

#include "Fury/RHI/RHICommandList.h"

namespace fury
{
	class GLShaderModule;

	typedef std::shared_ptr<GLShaderModule> GLShaderModuleRef;

	// Immediate-mode command list: every call issues GL directly. State
	// tracking exists only where the API contract needs it (current module,
	// texture unit cursor).
	class FURY_API GLCommandList : public RHICommandList
	{
	protected:

		RHIShaderModuleRef m_CurrentModule;

		RHIGraphicsPipelineRef m_CurrentPipeline;

		unsigned int m_TextureUnit = 0;

		unsigned int m_PrimitiveMode = 0;

		unsigned int m_BlitFBOs[2] = { 0, 0 };

		unsigned int m_MeshVAO = 0;

		struct TargetState
		{
			int FBO = 0;
			int Viewport[4] = { 0, 0, 0, 0 };
		};

		std::vector<TargetState> m_TargetStack;

	public:

		GLCommandList();

		~GLCommandList();

		void BeginRenderTargets(const RHIRenderTargetSetRef &targets, ClearMode clear, const Color &clearColor) override;

		void BeginBackbuffer(int width, int height, ClearMode clear, const Color &clearColor) override;

		void EndRenderTargets() override;

		void SetViewport(int x, int y, int width, int height) override;

	void SetScissor(bool enabled, int x, int y, int width, int height) override;

		void Clear(ClearMode mode, const Color &color) override;

	void ClearColorUInt(unsigned int value) override;

		void SetPipeline(const RHIGraphicsPipelineRef &pipeline) override;

		void BindShaderModule(const RHIShaderModuleRef &module) override;

		void SetRasterState(BlendMode blend, CompareMode depthFunc, bool depthTest, bool depthWrite, CullMode cull) override;

		void SetBlendMode(BlendMode mode) override;

		void SetDepthState(CompareMode func, bool test, bool write) override;

		void SetCullMode(CullMode mode) override;

		void SetColorWriteEnabled(bool enabled) override;

		void SetBlendEnabled(unsigned int attachment, bool enabled) override;

		void SetSRGBWriteEnabled(bool enabled) override;

		void PushRenderTarget() override;

		void PopRenderTarget() override;

		void SetPrimitiveMode(RHIPrimitiveMode mode) override;

		void SetFillMode(RHIFillMode mode) override;

		void SetDepthBias(float factor, float units) override;

		void SetVertexBuffer(const RHIBufferRef &buffer, unsigned int stream = 0) override;

		void SetIndexBuffer(const RHIBufferRef &buffer) override;

		void SetMeshGeometry(const RHIMeshGeometryDesc &desc) override;

		void SetInstanceMatrixStream(const RHIBufferRef &buffer, const char *const attrNames[4]) override;

		void SetStorageBuffer(unsigned int binding, const RHIBufferRef &buffer) override;

		RHIUniformHandle ResolveUniform(const std::string &name) override;

		void SetUniformFloat(const std::string &name, float v0) override;

		void SetUniformFloat(const std::string &name, float v0, float v1) override;

		void SetUniformFloat(const std::string &name, float v0, float v1, float v2) override;

		void SetUniformFloat(const std::string &name, float v0, float v1, float v2, float v3) override;

		void SetUniformFloatArray(const std::string &name, int size, int count, const float *value) override;

		void SetUniformInt(const std::string &name, int v0) override;

		void SetUniformInt(const std::string &name, int v0, int v1) override;

		void SetUniformInt(const std::string &name, int v0, int v1, int v2) override;

		void SetUniformInt(const std::string &name, int v0, int v1, int v2, int v3) override;

		void SetUniformIntArray(const std::string &name, int size, int count, const int *value) override;

		void SetUniformUInt(const std::string &name, unsigned int v0) override;

		void SetUniformMatrix(const std::string &name, const Matrix4 &matrix) override;

		void SetUniformMatrix(const std::string &name, const float *raw) override;

		void SetUniformMatrices(const std::string &name, int count, const float *raw) override;

		void SetUniformFloatAt(RHIUniformHandle handle, float x) override;

		void SetUniformFloatAt(RHIUniformHandle handle, float x, float y) override;

		void SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z) override;

		void SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z, float w) override;

		void SetUniformIntAt(RHIUniformHandle handle, int x) override;

		void SetUniformMatrixAt(RHIUniformHandle handle, const float *raw) override;

		void SetUniformMatricesAt(RHIUniformHandle handle, int count, const float *raw) override;

		void SetTexture(const std::string &name, const RHITextureRef &texture) override;

		int SetTextureAt(RHIUniformHandle handle, const RHITextureRef &texture) override;

		void SetTextureUnitCursor(int unitOffset) override;

		void BindTextureUnit(const RHITextureRef &texture) override;

		void Draw(unsigned int vertexCount, unsigned int firstVertex = 0) override;

		void DrawIndexed(unsigned int indexCount, unsigned int firstIndex = 0, int vertexOffset = 0) override;

		void DrawInstanced(unsigned int vertexCount, unsigned int instanceCount, unsigned int firstVertex = 0) override;

		void DrawIndexedInstanced(unsigned int indexCount, unsigned int instanceCount, unsigned int firstIndex = 0, int vertexOffset = 0) override;

		void Dispatch(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ) override;

		void Barrier() override;

		void Blit(const RHITextureRef &src, const RHITextureRef &dst, int srcWidth, int srcHeight, int dstWidth, int dstHeight, bool linearFilter = false) override;

		// Clear helper shared by BeginRenderTargets/BeginBackbuffer.
		static void ClearBuffers(ClearMode mode, const Color &color);

	protected:

		void BindModule(const RHIShaderModuleRef &module);

		GLShaderModule *CurrentGLModule() const;
	};
}

#endif // _FURY_GL_COMMAND_LIST_H_
