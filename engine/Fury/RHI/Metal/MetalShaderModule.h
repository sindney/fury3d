#ifndef _FURY_METAL_SHADER_MODULE_H_
#define _FURY_METAL_SHADER_MODULE_H_

#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_METAL

#include <Metal/Metal.hpp>

#include "Fury/RHI/RHIResources.h"

namespace fury
{
	// One stage's loose-uniform globals block (Slang's "globalParams" from
	// the cross-compiled GLSL set): the CPU shadow mirrors this layout and
	// binds at the block's MSL argument index.
	struct MetalStageGlobals
	{
		struct Member
		{
			std::string name;

			size_t offset = 0;

			size_t size = 0;
		};

		std::string blockName;

		size_t blockSize = 0;

		// [[buffer(n)]] index of the block in the emitted MSL (-1 = absent).
		int blockIndex = -1;

		std::vector<Member> members;

		bool HasBlock() const { return blockSize > 0 && !blockName.empty(); }

		// -1 when the member is absent.
		long FindMember(const std::string &name) const;
	};

	// MSL source library + stage functions. Geometry stages are unsupported
	// (no engine shader uses one). Two source paths:
	//  - MSL source (hand-authored twins, probe): vertex+fragment concat into
	//    one library.
	//  - GLSL (the engine's file set): cross-compiled per stage (Slang front-
	//    end -> SPIR-V -> SPIRV-Cross), one library per stage since both
	//    stages' globals blocks share the GlobalParams_std140 type name.
	class FURY_API MetalShaderModule : public RHIShaderModule
	{
	protected:

		MTL::Library *m_Library = nullptr;

		MTL::Library *m_VertexLibrary = nullptr;

		MTL::Library *m_FragmentLibrary = nullptr;

		MTL::Function *m_Vertex = nullptr;

		MTL::Function *m_Fragment = nullptr;

		MTL::Function *m_Compute = nullptr;

		MetalStageGlobals m_Globals[2];   // [0] = vertex, [1] = fragment

		std::unordered_map<std::string, int> m_StageInputs;

		// True for the GLSL cross-compile path (every loose uniform lives in
		// the globals block; no direct uniform arguments exist to resolve).
		bool m_CrossCompiled = false;

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

		std::string m_Name;

	public:

		static std::shared_ptr<MetalShaderModule> Create(MTL::Device *device, const std::string &name,
			const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry);

		static std::shared_ptr<MetalShaderModule> CreateCompute(MTL::Device *device, const std::string &name,
			const RHIShaderBlob &compute);

		~MetalShaderModule() override;

		bool IsValid() const override { return m_Library != nullptr || m_VertexLibrary != nullptr; }

		// Deferred-name handles (globals members resolve directly; the rest
		// defer to the live PSO's reflection maps by name). Vertex attribute
		// locations come from the compiler sidecar.
		RHIUniformHandle ResolveUniform(const std::string &name) override;

		// Vertex attribute location from the compiler sidecar (-1 = absent).
		int ResolveAttribLocation(const std::string &name) override;

		uintptr_t GetNativeHandle() const override { return reinterpret_cast<uintptr_t>(m_Library); }

		MTL::Function *GetVertex() const { return m_Vertex; }

		MTL::Function *GetFragment() const { return m_Fragment; }

		MTL::Function *GetCompute() const { return m_Compute; }

		const MetalStageGlobals &GetStageGlobals(int stage) const { return m_Globals[stage]; }

		bool IsCrossCompiled() const { return m_CrossCompiled; }

		// -1 when absent from both stages' globals blocks.
		long FindMemberIndex(const std::string &name) const;

		const GlobalMember &GetMember(size_t index) const { return m_Members[index]; }

		// Names registered through deferred (kind-3) handles; the command
		// list resolves them against the live PSO's reflection maps.
		std::vector<std::string> m_HandleNames;

		const std::string &GetHandleName(size_t index) const { return m_HandleNames[index]; }

		size_t HandleNameCount() const { return m_HandleNames.size(); }

		const std::string &GetName() const { return m_Name; }
	};

	typedef std::shared_ptr<MetalShaderModule> MetalShaderModuleRef;

	// Pipeline-state bundle. The MTL::RenderPipelineState is NOT built here:
	// pixel formats are render-target data, so the device materializes PSO
	// variants per (desc, target formats) in its cache (MetalCommandList
	// binds the resolved variant).
	class FURY_API MetalGraphicsPipeline : public RHIGraphicsPipeline
	{
	protected:

		RHIGraphicsPipelineDesc m_Desc;

		MetalShaderModuleRef m_Shaders;

	public:

		MetalGraphicsPipeline(const RHIGraphicsPipelineDesc &desc, const MetalShaderModuleRef &shaders)
			: m_Desc(desc), m_Shaders(shaders) {}

		const RHIGraphicsPipelineDesc &GetDesc() const override { return m_Desc; }

		RHIShaderModuleRef GetShaders() const override { return m_Shaders; }

		const MetalShaderModuleRef &GetMetalShaders() const { return m_Shaders; }
	};

	typedef std::shared_ptr<MetalGraphicsPipeline> MetalGraphicsPipelineRef;
}

#endif // FURY_RHI_METAL

#endif // _FURY_METAL_SHADER_MODULE_H_
