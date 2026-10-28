#ifndef _FURY_VULKAN_SHADER_MODULE_H_
#define _FURY_VULKAN_SHADER_MODULE_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_VULKAN

#include <unordered_map>
#include <volk.h>

#include "Fury/RHI/RHIResources.h"

namespace fury
{
	// One stage's loose-uniform globals block (Slang's "globalParams"):
	// the CPU shadow mirrors this layout and binds at the block's
	// descriptor binding.
	struct VulkanStageGlobals
	{
		struct Member
		{
			std::string name;

			size_t offset = 0;

			size_t size = 0;
		};

		std::string blockName;

		size_t blockSize = 0;

		// Descriptor binding of the block in the SPIR-V (-1 = absent).
		int blockIndex = -1;

		std::vector<Member> members;

		std::vector<unsigned char> defaultValues;

		bool HasBlock() const { return blockSize > 0 && !blockName.empty(); }

		// -1 when the member is absent.
		long FindMember(const std::string &name) const;
	};

	enum class VulkanShaderResourceType
	{
		UniformBuffer,

		// Combined image sampler; arrays/cubes/shadow samplers all arrive here.
		SampledImage,

		StorageBuffer
	};

	// One descriptor-relevant resource, merged across stages by
	// name+set+binding+type. Sets are stage-separated: vertex resources live
	// in set 0, fragment in set 1, compute in 0 (rewritten into the SPIR-V),
	// so both stages' globals blocks keep distinct CPU shadows.
	struct VulkanShaderResource
	{
		std::string name;

		uint32_t set = 0;

		uint32_t binding = 0;

		VkShaderStageFlags stages = 0;

		VulkanShaderResourceType type = VulkanShaderResourceType::UniformBuffer;

		bool isArray = false;

		uint32_t arraySize = 1;

		bool isCube = false;

		bool isShadow = false;
	};

	// VkShaderModule set + reflection. Geometry stages are unsupported (no
	// engine shader uses one). Descriptor sets are stage-separated: vertex
	// resources set 0, fragment set 1 (compute 0), rewritten into the SPIR-V
	// by the shader cache. Two source paths:
	//  - SPIR-V binary blobs: modules are created directly, descriptors and
	//    stage inputs are reflected from the SPIR-V.
	//  - Engine GLSL: cross-compiled per stage (Slang front-end -> SPIR-V),
	//    cached as .spv + .refl through the shader DDC, one module per stage
	//    (entry point "main").
	class FURY_API VulkanShaderModule : public RHIShaderModule
	{
	protected:

		VkDevice m_Device = VK_NULL_HANDLE;

		VkShaderModule m_Vertex = VK_NULL_HANDLE;

		VkShaderModule m_Fragment = VK_NULL_HANDLE;

		VkShaderModule m_Compute = VK_NULL_HANDLE;

		VulkanStageGlobals m_Globals[2];   // [0] = vertex, [1] = fragment

		std::unordered_map<std::string, int> m_StageInputs;

		// Union of both stages' globals members; offsets are per stage (-1
		// = absent in that stage). Powers the single-handle uniform model
		// (GL links stages into one uniform namespace).
		struct GlobalMember
		{
			std::string name;

			long offset[2] = { -1, -1 };

			size_t size = 0;
		};

		std::vector<GlobalMember> m_Members;

		std::unordered_map<std::string, int> m_MemberIndex;

		std::vector<VulkanShaderResource> m_Resources;

		std::unordered_map<std::string, int> m_ResourceIndex;

		std::string m_Name;

	public:

		static std::shared_ptr<VulkanShaderModule> Create(VkDevice device, const std::string &name,
			const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry);

		static std::shared_ptr<VulkanShaderModule> CreateCompute(VkDevice device, const std::string &name,
			const RHIShaderBlob &compute);

		~VulkanShaderModule() override;

		// Destroys the native modules and nulls the handles; safe to call
		// twice and after device teardown (the device owner drains registered
		// modules before vkDestroyDevice, so late destructors must no-op).
		void ReleaseNative();

		bool IsValid() const override { return m_Vertex != VK_NULL_HANDLE || m_Compute != VK_NULL_HANDLE; }

		// Globals members resolve directly; the rest defer to the pipeline's
		// descriptor maps by name. Same payload encoding as the Metal module.
		RHIUniformHandle ResolveUniform(const std::string &name) override;

		// Vertex attribute location from the compiler sidecar (-1 = absent).
		int ResolveAttribLocation(const std::string &name) override;

		uintptr_t GetNativeHandle() const override { return reinterpret_cast<uintptr_t>(m_Vertex); }

		VkShaderModule GetVertexModule() const { return m_Vertex; }

		VkShaderModule GetFragmentModule() const { return m_Fragment; }

		VkShaderModule GetComputeModule() const { return m_Compute; }

		const VulkanStageGlobals &GetStageGlobals(int stage) const { return m_Globals[stage]; }

		// -1 when absent from both stages' globals blocks.
		long FindMemberIndex(const std::string &name) const;

		const GlobalMember &GetMember(size_t index) const { return m_Members[index]; }

		// Merged (name -> set/binding/stage/type) descriptor reflection.
		size_t ResourceCount() const { return m_Resources.size(); }

		const VulkanShaderResource &GetResource(size_t index) const { return m_Resources[index]; }

		// First index with this name (-1 = absent); stage-separated sets can
		// share a name, so callers that need every match use the indices list.
		long FindResourceIndex(const std::string &name) const;

		// All indices with this name, in resource order.
		std::vector<size_t> FindResourceIndices(const std::string &name) const;

		// Names registered through deferred (kind-3) handles; the command
		// list resolves them against the pipeline's descriptor maps.
		std::vector<std::string> m_HandleNames;

		std::unordered_map<std::string, RHIUniformHandle> m_DeferredHandles;

		const std::string &GetHandleName(size_t index) const { return m_HandleNames[index]; }

		size_t HandleNameCount() const { return m_HandleNames.size(); }

		const std::string &GetName() const { return m_Name; }
	};

	typedef std::shared_ptr<VulkanShaderModule> VulkanShaderModuleRef;

	// Pipeline-state bundle. The VkPipeline is NOT built here: pixel formats
	// are render-target data, so the device materializes pipeline variants
	// per (desc, target formats) in its cache.
	class FURY_API VulkanGraphicsPipeline : public RHIGraphicsPipeline
	{
	protected:

		RHIGraphicsPipelineDesc m_Desc;

		VulkanShaderModuleRef m_Shaders;

	public:

		VulkanGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const VulkanShaderModuleRef &shaders)
			: m_Desc(desc), m_Shaders(shaders) {}

		const RHIGraphicsPipelineDesc &GetDesc() const override { return m_Desc; }

		RHIShaderModuleRef GetShaders() const override { return m_Shaders; }

		const VulkanShaderModuleRef &GetVulkanShaders() const { return m_Shaders; }
	};

	typedef std::shared_ptr<VulkanGraphicsPipeline> VulkanGraphicsPipelineRef;
}

#endif // FURY_RHI_VULKAN

#endif // _FURY_VULKAN_SHADER_MODULE_H_
