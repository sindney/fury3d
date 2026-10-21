#ifndef _FURY_METAL_COMMAND_LIST_H_
#define _FURY_METAL_COMMAND_LIST_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_METAL

#include <Metal/Metal.hpp>

#include <map>
#include <unordered_map>

#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/Metal/MetalResources.h"
#include "Fury/RHI/Metal/MetalShaderModule.h"

namespace fury
{
	class RHIMetalDevice;

	struct MetalBindingEntry
	{
		int Stage = 0;   // 0 = vertex, 1 = fragment
		int Index = -1;
	};

	struct MetalBindingMaps
	{
		std::unordered_map<std::string, MetalBindingEntry> Uniforms;
		std::unordered_map<std::string, MetalBindingEntry> Textures;

		// Per-stage views: resources shared by both stages can sit at
		// different argument indices per stage (e.g. an SSBO shifts the
		// globals block), which the merged maps above cannot represent.
		std::unordered_map<std::string, MetalBindingEntry> UniformsByStage[2];
		std::unordered_map<std::string, MetalBindingEntry> TexturesByStage[2];
	};

	// Records one frame of work as MTL command-buffer encoders. Render-pass
	// pixel formats are target data, so MTL::RenderPipelineState variants are
	// materialized lazily per (module + state + formats) in the device cache;
	// both binding models (SetPipeline / BindShaderModule+SetRasterState)
	// resolve to the same cache.
	//
	// Buffer binding conventions:
	//   buffer(0..15)   explicit MSL arguments, indices assigned by the
	//                   compiler/reflection: globals blocks, fullscreen
	//                   vertex streams (SetVertexBuffer stream 0..3), SSBOs,
	//                   hand-authored direct uniform arguments
	//   buffer(16..23)  mesh geometry channels (SetMeshGeometry, ride the
	//                   vertex descriptor; kept clear of every compiler-
	//                   assigned argument index, SSBOs included)
	//   buffer(24)      instance matrix rows x4 (divisor instancing path)
	//   buffer(25)      stride-0 constant-fill stream for empty channels
	class FURY_API MetalCommandList : public RHICommandList
	{
	public:

		static const int MAX_STREAMS = 4;

		static const int MESH_STREAM_BASE = 16;

		static const int INSTANCE_STREAM = 24;

		static const int CONSTANTS_STREAM = 25;

	protected:

		RHIMetalDevice *m_Device = nullptr;

		// Current target. Backbuffer: m_Targets null, formats from the layer.
		MetalRenderTargetSetRef m_Targets;

		MTL::PixelFormat m_ColorFormats[4] = { MTL::PixelFormatInvalid, MTL::PixelFormatInvalid, MTL::PixelFormatInvalid, MTL::PixelFormatInvalid };

		MTL::PixelFormat m_DepthFormat = MTL::PixelFormatInvalid;

		int m_TargetWidth = 0;

		int m_TargetHeight = 0;

		MTL::RenderCommandEncoder *m_Encoder = nullptr;

		// Descriptor of the live encoder's render pass; the UI overlay
		// hands it to the ImGui Metal backend (it derives the drawable's
		// size and format from it).
		NS::SharedPtr<MTL::RenderPassDescriptor> m_RenderPass;

		// Bound state (mirrors GLCommandList's tracked state).
		MetalShaderModuleRef m_CurrentModule;

		RHIGraphicsPipelineRef m_CurrentPipeline;

		BlendMode m_Blend = BlendMode::REPLACE;

		CompareMode m_DepthFunc = CompareMode::LESS;

		bool m_DepthTest = true;

		bool m_DepthWrite = true;

		CullMode m_Cull = CullMode::BACK;

		RHIFillMode m_Fill = RHIFillMode::SOLID;

		RHIPrimitiveMode m_Primitive = RHIPrimitiveMode::TRIANGLES;

		bool m_ColorWrite = true;

		bool m_BlendEnabled[4] = { true, true, true, true };

		bool m_SRGBWrite = false;

		double m_UIntClear = 0.0;

		MTL::Viewport m_Viewport = { 0, 0, 0, 0, 0, 1 };

		// True for the backbuffer, false for offscreen targets. Offscreen
		// targets render with a negative-height viewport (GL's rasterization
		// convention, so render-target sampling matches GL); the backbuffer
		// renders in Metal's native orientation so the window displays
		// correctly without layer tricks. Flips viewport sign, winding and
		// scissor mapping with the target.
		bool m_NativeYViewport = false;

		bool m_ScissorEnabled = false;

		MTL::ScissorRect m_Scissor = { 0, 0, 0, 0 };

		// Polygon offset (GL parity). Metal keeps the bias on the encoder, so
		// it is held here and re-applied to every encoder the command list
		// creates - a mid-pass rebuild (CSM cascade layer switch) otherwise
		// drops it and the shadow map is written without offset.
		float m_DepthBiasFactor = 0.0f;

		float m_DepthBiasUnits = 0.0f;

		// Resolved PSO + reflection for the current module/state/formats.
		MTL::RenderPipelineState *m_PSO = nullptr;

		bool m_PSOValid = false;

		std::unordered_map<std::string, MetalBindingEntry> m_UniformBindings;

		std::unordered_map<std::string, MetalBindingEntry> m_TextureBindings;

		// Per-stage views of the same PSO reflection (see MetalBindingMaps).
		std::unordered_map<std::string, MetalBindingEntry> m_UniformsByStage[2];

		std::unordered_map<std::string, MetalBindingEntry> m_TexturesByStage[2];

		// Uniform shadow store: (stage,index) -> bytes, flushed via set*Bytes.
		std::map<uint64_t, std::vector<unsigned char>> m_UniformShadow;

		// Per-stage loose-uniform shadow for the cross-compiled GLSL set:
		// contiguous bytes in the globals block's layout, bound at the
		// "globalParams" argument index. [0] = vertex, [1] = fragment.
		std::vector<unsigned char> m_GlobalsShadow[2];

		bool m_GlobalsActive[2] = { false, false };

		struct TextureSlot
		{
			MetalTextureRef Texture;
		};

		std::map<uint64_t, TextureSlot> m_Textures;

		// Binds that arrived while the PSO reflection maps were cold (PSO
		// creation can fail transiently at bind time, e.g. stale mesh layout):
		// resolved against the maps at flush time, where the PSO is valid.
		std::unordered_map<std::string, MetalTextureRef> m_PendingTextures;

		MetalBufferRef m_VertexStreams[MAX_STREAMS];

		MetalBufferRef m_IndexBuffer;

		// Mesh geometry from SetMeshGeometry (channel buffers bind per draw;
		// the synthetic vertex layout is rebuilt per EnsurePSO).
		RHIMeshChannelDesc m_MeshChannels[RHIMeshGeometryDesc::MAX_CHANNELS];

		int m_MeshChannelCount = 0;

		const char *m_MeshName = "";

		MetalBufferRef m_InstanceBuffer;

		std::string m_InstanceAttrNames[4];

		bool m_HasInstanceStream = false;

		MetalBufferRef m_ConstantsBuffer;

		std::map<unsigned int, MetalBufferRef> m_StorageBuffers;

		// Named SSBO binds (SetStorageBufferNamed): cross-compilers repack
		// SSBO argument indices, so the GLSL binding is meaningless here --
		// resolve the name against the PSO reflection maps at flush time.
		std::unordered_map<std::string, MetalBufferRef> m_NamedStorageBuffers;

		unsigned int m_TextureCursor = 0;

		struct TargetState
		{
			MetalRenderTargetSetRef Targets;
			MTL::Viewport Viewport;
			int Width = 0;
			int Height = 0;
		};

		std::vector<TargetState> m_TargetStack;

	public:

		explicit MetalCommandList(RHIMetalDevice *device) : m_Device(device) {}

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

		void SetStorageBufferNamed(const char *name, unsigned int binding, const RHIBufferRef &buffer) override;

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

		// Frame boundary hooks (device).
		void OnBeginFrame();

		void OnEndFrame();

		// UI overlay: draws the backend's ImGui draw data into a
		// load-only pass over the presented drawable.
		void DrawUiOverlay(void *drawData, unsigned int width, unsigned int height);

	protected:

		// Ensures a live render encoder for the current target, applying
		// pending viewport/scissor. Ends + recreates when the target changes.
		MTL::RenderCommandEncoder *EnsureEncoder();

		// useClear: recreate the encoder with clear load-actions (mid-target
		// Clear calls); clearUInt: clear color0 from m_UIntClear (id passes).
		MTL::RenderCommandEncoder *EnsureEncoderWithClear(bool useClear, ClearMode clear, const Color &clearColor, bool clearUInt);

	public:

		// Ends the live render encoder (device frame boundaries).
		void EndEncoder();

		// Clears every slice of one attachment whose state names the whole
		// texture (loadAction=Clear covers only the attached slice).
		void ClearAllSlices(MetalTexture *tex, bool depth, const Color &color);

		// Resolved (level, slice) per attachment the live encoder was built
		// with; a SetAttachmentLayer/CubeFace after creation forces a rebuild
		// (Metal resolves attachment slices only at encoder creation).
		std::vector<std::pair<unsigned int, unsigned int>> m_EncoderSlices;

		// Resolves (creating on miss) the PSO for the current
		// module/state/formats and rebuilds the reflection maps on change.
		bool EnsurePSO();

		void InvalidateBindings();

		// Rebinds the current module and reshapes the per-stage globals
		// shadows to its layout (clears uniform shadows on module change).
		void AdoptModule(const MetalShaderModuleRef &module);

		static uint64_t ShadowKey(int stage, int index);

		void WriteShadow(const std::string &name, const void *data, size_t size);

		void WriteShadowAt(RHIUniformHandle handle, const void *data, size_t size);

		void FlushDrawState();

		static uint32_t HashVertexLayout(const std::vector<RHIVertexAttribute> &attributes);

		const std::vector<RHIVertexAttribute> *CurrentVertexLayout();

		// Builds the synthetic vertex layout for the current mesh desc +
		// instance stream against the bound module's attribute map.
		void BuildMeshLayout(std::vector<RHIVertexAttribute> &out);

		static RHIVertexFormat MeshChannelFormat(int components, bool integer);

		// Shared stride-0 constant-fill buffer for empty mesh channels.
		MTL::Buffer *GetConstantsBuffer();

		// Vertex layout built on demand by CurrentVertexLayout (mesh draws).
		std::vector<RHIVertexAttribute> m_MeshLayout;

		// Handle bits: [0..23] payload+1, [24..25] stage (0=v, 1=f, 2=both),
		// [26..28] kind (0 = direct-arg uniform, 1 = texture, 2 = globals member).
		static int EncodeHandle(int stage, int index, int kind);

		static void DecodeHandle(RHIUniformHandle handle, int &stage, int &index, int &kind);
	};
}

#endif // FURY_RHI_METAL

#endif // _FURY_METAL_COMMAND_LIST_H_
