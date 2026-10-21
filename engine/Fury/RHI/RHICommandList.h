#ifndef _FURY_RHI_COMMAND_LIST_H_
#define _FURY_RHI_COMMAND_LIST_H_

#include <string>

#include "Fury/Matrix4.h"
#include "Fury/RHI/RHIResources.h"
#include "Fury/RHI/RHITypes.h"

namespace fury
{
	// Records one frame's worth of render work. Lives entirely on the render
	// thread; backends either execute directly (GL) or record native command
	// buffers (Vulkan/Metal).
	//
	// Two binding models coexist:
	//  - SetPipeline: full pipeline (shader + state) -- the primary model.
	//  - BindShaderModule + SetRasterState: dynamic-state model used by debug
	//    draws and simple full-screen passes.
	class FURY_API RHICommandList
	{
	public:

		virtual ~RHICommandList() = default;

		// Binds the render target set, sets viewport to its extent, clears per mode.
		virtual void BeginRenderTargets(const RHIRenderTargetSetRef &targets, ClearMode clear, const Color &clearColor) = 0;

		// Binds the window backbuffer as the render target.
		virtual void BeginBackbuffer(int width, int height, ClearMode clear, const Color &clearColor) = 0;

		virtual void EndRenderTargets() = 0;

		virtual void SetViewport(int x, int y, int width, int height) = 0;

		// Scissor rect in pixels, GL convention (y-up, origin bottom-left);
		// backends with y-down scissor convert. Disabled = full target.
		virtual void SetScissor(bool enabled, int x, int y, int width, int height) = 0;

		// Clears the currently bound target.
		virtual void Clear(ClearMode mode, const Color &color) = 0;

		// Clears color attachment 0 to a uint value on all channels
		// (integer id attachments; GL glClearBufferuiv).
		virtual void ClearColorUInt(unsigned int value) = 0;

		virtual void SetPipeline(const RHIGraphicsPipelineRef &pipeline) = 0;

		// Program-only bind; raster state follows via SetRasterState.
		virtual void BindShaderModule(const RHIShaderModuleRef &module) = 0;

		virtual void SetRasterState(BlendMode blend, CompareMode depthFunc, bool depthTest, bool depthWrite, CullMode cull) = 0;

		// Blend-only update for mid-pass overrides (per-emitter particle
		// modes, additive light loops); depth/cull untouched.
		virtual void SetBlendMode(BlendMode mode) = 0;

		// Depth-only update for mid-pass phases (PreZ func/write toggles,
		// light-volume tests); blend/cull untouched.
		virtual void SetDepthState(CompareMode func, bool test, bool write) = 0;

		// Cull-only update for two-sided units and light volumes.
		virtual void SetCullMode(CullMode mode) = 0;

		// Toggles color writes on all channels (depth-only phases).
		virtual void SetColorWriteEnabled(bool enabled) = 0;

		// Per-attachment blend enable for multi-target passes; the func
		// still comes from SetBlendMode/SetRasterState.
		virtual void SetBlendEnabled(unsigned int attachment, bool enabled) = 0;

		// Legacy LDR-path sRGB encode on screen-bound draws
		// (GL_FRAMEBUFFER_SRGB). Other backends encode via backbuffer format.
		virtual void SetSRGBWriteEnabled(bool enabled) = 0;

		// Saves the currently bound target + viewport; PopRenderTarget
		// restores. For overlays/chain writes that hand the previous
		// target back untouched.
		virtual void PushRenderTarget() = 0;

		virtual void PopRenderTarget() = 0;

		// Primitive topology for Draw* calls outside a pipeline (debug lines).
		// SetPipeline overrides this from the pipeline desc.
		virtual void SetPrimitiveMode(RHIPrimitiveMode mode) = 0;

		// Solid/wireframe; SetPipeline overrides from the pipeline desc.
		virtual void SetFillMode(RHIFillMode mode) = 0;

		// Depth bias for shadow-map renders (GL polygon offset; Metal
		// setDepthBias; Vulkan vkCmdSetDepthBias). (0, 0) disables.
		virtual void SetDepthBias(float factor, float units) = 0;

		virtual void SetVertexBuffer(const RHIBufferRef &buffer, unsigned int stream = 0) = 0;

		virtual void SetIndexBuffer(const RHIBufferRef &buffer) = 0;

		// Binds a mesh's vertex channels against the current module's
		// attribute layout (GL: VAO state; Metal/Vulkan: vertex descriptor).
		virtual void SetMeshGeometry(const RHIMeshGeometryDesc &desc) = 0;

		// Divisor-1 instance matrix stream (4 float4 rows named attrNames).
		// Null buffer unbinds. SSBO instancing path stays on SetStorageBuffer.
		virtual void SetInstanceMatrixStream(const RHIBufferRef &buffer, const char *const attrNames[4]) = 0;

		// Storage buffer at the binding index the shader declares
		// (layout(std430, binding = N)); other backends resolve by reflection.
		virtual void SetStorageBuffer(unsigned int binding, const RHIBufferRef &buffer) = 0;

		// Named variant: cross-compilers repack SSBO argument indices, so
		// backends that cannot use the GLSL binding resolve the name through
		// reflection instead. Default forwards to the numeric path (GL).
		virtual void SetStorageBufferNamed(const char *name, unsigned int binding, const RHIBufferRef &buffer)
		{
			SetStorageBuffer(binding, buffer);
		}

		virtual RHIUniformHandle ResolveUniform(const std::string &name) = 0;

		virtual void SetUniformFloat(const std::string &name, float v0) = 0;

		virtual void SetUniformFloat(const std::string &name, float v0, float v1) = 0;

		virtual void SetUniformFloat(const std::string &name, float v0, float v1, float v2) = 0;

		virtual void SetUniformFloat(const std::string &name, float v0, float v1, float v2, float v3) = 0;

		// size = components per element (1-4), count = element count.
		virtual void SetUniformFloatArray(const std::string &name, int size, int count, const float *value) = 0;

		virtual void SetUniformInt(const std::string &name, int v0) = 0;

		virtual void SetUniformInt(const std::string &name, int v0, int v1) = 0;

		virtual void SetUniformInt(const std::string &name, int v0, int v1, int v2) = 0;

		virtual void SetUniformInt(const std::string &name, int v0, int v1, int v2, int v3) = 0;

		virtual void SetUniformIntArray(const std::string &name, int size, int count, const int *value) = 0;

		virtual void SetUniformUInt(const std::string &name, unsigned int v0) = 0;

		virtual void SetUniformMatrix(const std::string &name, const Matrix4 &matrix) = 0;

		virtual void SetUniformMatrix(const std::string &name, const float *raw) = 0;

		virtual void SetUniformMatrices(const std::string &name, int count, const float *raw) = 0;

		virtual void SetUniformFloatAt(RHIUniformHandle handle, float x) = 0;

		virtual void SetUniformFloatAt(RHIUniformHandle handle, float x, float y) = 0;

		virtual void SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z) = 0;

		virtual void SetUniformFloatAt(RHIUniformHandle handle, float x, float y, float z, float w) = 0;

		virtual void SetUniformIntAt(RHIUniformHandle handle, int x) = 0;

		virtual void SetUniformMatrixAt(RHIUniformHandle handle, const float *raw) = 0;

		virtual void SetUniformMatricesAt(RHIUniformHandle handle, int count, const float *raw) = 0;

		// Texture binds allocate sequential texture units from the cursor;
		// the cursor resets on BindShaderModule/SetPipeline.
		virtual void SetTexture(const std::string &name, const RHITextureRef &texture) = 0;

		// Binds at the current unit and assigns the unit to the uniform.
		// Returns the unit offset consumed (0 when the uniform is absent).
		virtual int SetTextureAt(RHIUniformHandle handle, const RHITextureRef &texture) = 0;

		virtual void SetTextureUnitCursor(int unitOffset) = 0;

		// Binds a texture to the current unit without touching any uniform.
		virtual void BindTextureUnit(const RHITextureRef &texture) = 0;

		virtual void Draw(unsigned int vertexCount, unsigned int firstVertex = 0) = 0;

		virtual void DrawIndexed(unsigned int indexCount, unsigned int firstIndex = 0, int vertexOffset = 0) = 0;

		virtual void DrawInstanced(unsigned int vertexCount, unsigned int instanceCount, unsigned int firstVertex = 0) = 0;

		virtual void DrawIndexedInstanced(unsigned int indexCount, unsigned int instanceCount, unsigned int firstIndex = 0, int vertexOffset = 0) = 0;

		virtual void Dispatch(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ) = 0;

		// glMemoryBarrier-equivalent; no-op where the backend serializes implicitly.
		virtual void Barrier() = 0;

		// Copy between two textures (depth blits, resolves).
		virtual void Blit(const RHITextureRef &src, const RHITextureRef &dst, int srcWidth, int srcHeight, int dstWidth, int dstHeight, bool linearFilter = false) = 0;
	};
}

#endif // _FURY_RHI_COMMAND_LIST_H_
