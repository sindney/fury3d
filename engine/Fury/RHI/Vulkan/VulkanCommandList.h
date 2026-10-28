#ifndef _FURY_VULKAN_COMMAND_LIST_H_
#define _FURY_VULKAN_COMMAND_LIST_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_VULKAN

#include <map>
#include <unordered_map>

#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/Vulkan/VulkanResources.h"
#include "Fury/RHI/Vulkan/VulkanShaderModule.h"

namespace fury
{
	class RHIVulkanDevice;

	class FURY_API VulkanCommandList : public RHICommandList
	{
	public:
		static const int MAX_STREAMS = 4;
		static const int MESH_STREAM_BASE = 16;
		static const int INSTANCE_STREAM = 24;
		static const int CONSTANTS_STREAM = 25;

	protected:
		struct PipelineKey
		{
			const VulkanShaderModule *Module = nullptr;
			BlendMode Blend = BlendMode::REPLACE;
			CompareMode DepthFunc = CompareMode::LESS;
			bool DepthTest = true;
			bool DepthWrite = true;
			CullMode Cull = CullMode::BACK;
			RHIPrimitiveMode Primitive = RHIPrimitiveMode::TRIANGLES;
			RHIFillMode Fill = RHIFillMode::SOLID;
			bool ColorWrite = true;
			bool BlendEnabled[4] = { true, true, true, true };
			uint32_t ColorFormats[4] = { 0, 0, 0, 0 };
			uint32_t DepthFormat = 0;
			uint32_t VertexHash = 0;
			bool operator<(const PipelineKey &other) const;
		};

		struct PipelineEntry
		{
			VulkanShaderModuleRef Module;
			VkPipeline Pipeline = VK_NULL_HANDLE;
			VkPipelineLayout Layout = VK_NULL_HANDLE;
			VkDescriptorSetLayout SetLayouts[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
		};

		RHIVulkanDevice *m_Device = nullptr;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		std::map<PipelineKey, PipelineEntry> m_Pipelines;
		VulkanRenderTargetSetRef m_Targets;
		bool m_Backbuffer = false;
		bool m_Rendering = false;
		VkImageView m_ActiveViews[5] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
		int m_TargetWidth = 0;
		int m_TargetHeight = 0;
		VkFormat m_ColorFormats[4] = { VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED };
		VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;
		VkViewport m_Viewport = { 0, 0, 0, 0, 0, 1 };
		bool m_ScissorEnabled = false;
		VkRect2D m_Scissor = { { 0, 0 }, { 0, 0 } };
		VulkanShaderModuleRef m_CurrentModule;
		RHIGraphicsPipelineRef m_CurrentPipeline;
		PipelineEntry *m_NativePipeline = nullptr;
		BlendMode m_Blend = BlendMode::REPLACE;
		CompareMode m_DepthFunc = CompareMode::LESS;
		bool m_DepthTest = true;
		bool m_DepthWrite = true;
		CullMode m_Cull = CullMode::BACK;
		RHIPrimitiveMode m_Primitive = RHIPrimitiveMode::TRIANGLES;
		RHIFillMode m_Fill = RHIFillMode::SOLID;
		bool m_ColorWrite = true;
		bool m_SRGBWriteEnabled = true;
		bool m_BlendEnabled[4] = { true, true, true, true };
		float m_DepthBiasFactor = 0.0f;
		float m_DepthBiasUnits = 0.0f;
		std::vector<unsigned char> m_GlobalsShadow[2];
		VulkanBufferRef m_GlobalsBuffers[2];
		std::unordered_map<std::string, VulkanTextureRef> m_Textures;
		std::unordered_map<std::string, VulkanBufferRef> m_NamedStorageBuffers;
		std::map<unsigned int, VulkanBufferRef> m_StorageBuffers;
		VulkanBufferRef m_VertexStreams[MAX_STREAMS];
		VulkanBufferRef m_IndexBuffer;
		RHIMeshChannelDesc m_MeshChannels[RHIMeshGeometryDesc::MAX_CHANNELS];
		int m_MeshChannelCount = 0;
		const char *m_MeshName = "";
		VulkanBufferRef m_InstanceBuffer;
		std::string m_InstanceAttrNames[4];
		bool m_HasInstanceStream = false;
		VulkanBufferRef m_ConstantsBuffer;
		unsigned int m_TextureCursor = 0;
		std::vector<RHIVertexAttribute> m_MeshLayout;
		std::vector<std::shared_ptr<const void>> m_FrameRetain;

		struct TargetState
		{
			VulkanRenderTargetSetRef Targets;
			bool Backbuffer = false;
			VkViewport Viewport = { 0, 0, 0, 0, 0, 1 };
			int Width = 0;
			int Height = 0;
		};
		std::vector<TargetState> m_TargetStack;

	public:
		explicit VulkanCommandList(RHIVulkanDevice *device);
		~VulkanCommandList() override;
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
		void SetSRGBWriteEnabled(bool enabled) override { m_SRGBWriteEnabled = enabled; InvalidatePipeline(); }
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
		void OnBeginFrame();
		void OnEndFrame();
		void EndRendering();

	protected:
		template <typename T> void Retain(const std::shared_ptr<T> &resource)
		{
			if (resource != nullptr)
				m_FrameRetain.push_back(resource);
		}
		void AdoptModule(const VulkanShaderModuleRef &module);
		void InvalidatePipeline();
		bool BeginRendering(ClearMode clear, const Color &color, bool clearUInt = false, unsigned int uintValue = 0, bool allLayers = false);
		void SetTargetFormats();
		bool EnsurePipeline();
		const std::vector<RHIVertexAttribute> *CurrentVertexLayout();
		void BuildMeshLayout(std::vector<RHIVertexAttribute> &out);
		static RHIVertexFormat MeshChannelFormat(int components, bool integer);
		static uint32_t HashVertexLayout(const std::vector<RHIVertexAttribute> &attributes);
		VulkanBuffer *GetConstantsBuffer();
		bool FlushDrawState();
		void WriteShadow(const std::string &name, const void *data, size_t size);
		void WriteShadowAt(RHIUniformHandle handle, const void *data, size_t size);
		static void DecodeHandle(RHIUniformHandle handle, int &stage, int &index, int &kind);
		void ClearAttachments(ClearMode mode, const Color &color, bool clearUInt, unsigned int uintValue, bool allLayers);
	};
}

#endif // FURY_RHI_VULKAN

#endif // _FURY_VULKAN_COMMAND_LIST_H_
