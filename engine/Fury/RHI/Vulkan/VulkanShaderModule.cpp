#include "Fury/RHI/Vulkan/VulkanShaderModule.h"

#if FURY_RHI_VULKAN

#include <algorithm>
#include <cstring>

#include "SPIRV-Cross/spirv_glsl.hpp"

#include "Fury/Log.h"
#include "Fury/ShaderCache.h"
#include "Fury/ShaderCompiler.h"

namespace fury
{
	long VulkanStageGlobals::FindMember(const std::string &name) const
	{
		for (size_t i = 0; i < members.size(); ++i)
		{
			if (members[i].name == name)
				return static_cast<long>(i);
		}
		return -1;
	}

	namespace
	{
		bool IsGlslSource(const std::vector<unsigned char> &data)
		{
			const char marker[] = "#version";
			return std::search(data.begin(), data.end(), marker, marker + 8) != data.end();
		}

		VkShaderModule CreateModule(VkDevice device, const std::vector<unsigned char> &spirv,
			const std::string &name, const char *stage)
		{
			if (spirv.empty() || spirv.size() % 4 != 0)
			{
				FURYE << name << ": bad SPIR-V for " << stage << " stage";
				return VK_NULL_HANDLE;
			}
			VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			info.codeSize = spirv.size();
			info.pCode = reinterpret_cast<const uint32_t *>(spirv.data());
			VkShaderModule module = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
			{
				FURYE << name << ": vkCreateShaderModule failed (" << stage << ")";
				return VK_NULL_HANDLE;
			}
			return module;
		}

		void AdoptStageGlobals(const ShaderStageCompile &compiled, VulkanStageGlobals &out)
		{
			out.blockName = compiled.globalsBlockName;
			out.blockSize = compiled.globalsBlockSize;
			out.blockIndex = compiled.globalsBlockIndex;
			out.members.reserve(compiled.globals.size());
			out.defaultValues.assign(compiled.globalsBlockSize, 0);
			for (const auto &m : compiled.globals)
			{
				out.members.push_back({ m.name, m.offset, m.size });
				if (!m.defaultValue.empty() && m.offset + m.defaultValue.size() <= out.defaultValues.size())
					std::memcpy(out.defaultValues.data() + m.offset, m.defaultValue.data(), m.defaultValue.size());
			}
		}

		// Descriptor resources + stage inputs of one stage from its SPIR-V.
		bool ReflectStage(const std::vector<unsigned char> &spirv, VkShaderStageFlagBits stage,
			std::vector<VulkanShaderResource> &resources, std::unordered_map<std::string, int> &stageInputs,
			const std::string &name, std::string &error)
		{
			try
			{
				spirv_cross::CompilerGLSL compiler(
					reinterpret_cast<const uint32_t *>(spirv.data()), spirv.size() / 4);
				const auto refs = compiler.get_shader_resources();

				auto append = [&](const spirv_cross::SmallVector<spirv_cross::Resource> &list,
					VulkanShaderResourceType type)
				{
					for (const auto &res : list)
					{
						VulkanShaderResource out;
						out.name = res.name;
						if (out.name.empty())
							out.name = compiler.get_name(res.id);
						out.set = compiler.get_decoration(res.id, spv::DecorationDescriptorSet);
						out.binding = compiler.get_decoration(res.id, spv::DecorationBinding);
						out.stages = stage;
						out.type = type;
						if (type == VulkanShaderResourceType::SampledImage)
						{
							const auto &imageType = compiler.get_type(res.type_id);
							out.isArray = !imageType.array.empty();
							if (out.isArray)
								out.arraySize = std::max(1u, imageType.array[0]);
							out.isCube = imageType.image.dim == spv::DimCube;
							out.isShadow = imageType.image.depth;
						}
						resources.push_back(std::move(out));
					}
				};
				append(refs.uniform_buffers, VulkanShaderResourceType::UniformBuffer);
				append(refs.sampled_images, VulkanShaderResourceType::SampledImage);
				append(refs.storage_buffers, VulkanShaderResourceType::StorageBuffer);

				for (const auto &input : refs.stage_inputs)
				{
					stageInputs[compiler.get_name(input.id)] =
						static_cast<int>(compiler.get_decoration(input.id, spv::DecorationLocation));
				}
			}
			catch (const std::exception &e)
			{
				error = name + ": SPIRV-Cross reflection failed: " + e.what();
				return false;
			}
			return true;
		}

		// Same name+set+binding+type across stages merges (stages ORed);
		// stage-separated sets stay distinct entries.
		void MergeResources(const std::vector<VulkanShaderResource> &reflected,
			std::vector<VulkanShaderResource> &out, std::unordered_map<std::string, int> &index)
		{
			for (const auto &res : reflected)
			{
				const auto it = index.find(res.name);
				if (it != index.end())
				{
					auto &existing = out[it->second];
					if (existing.set == res.set && existing.binding == res.binding
						&& existing.type == res.type)
					{
						existing.stages |= res.stages;
						continue;
					}
				}
				index.emplace(res.name, static_cast<int>(out.size()));
				out.push_back(res);
			}
		}
	}

	std::shared_ptr<VulkanShaderModule> VulkanShaderModule::Create(VkDevice device, const std::string &name,
		const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry)
	{
		if (device == VK_NULL_HANDLE || vertex.Data.empty() || fragment.Data.empty())
			return nullptr;
		if (vertex.IsSource != fragment.IsSource)
		{
			FURYE << name << ": mixed source/binary shader blobs";
			return nullptr;
		}
		if (!geometry.Data.empty())
			FURYW << name << ": Vulkan has no geometry stage; stage ignored";

		auto out = std::make_shared<VulkanShaderModule>();
		out->m_Device = device;
		out->m_Name = name;

		std::vector<VulkanShaderResource> reflected;

		if (vertex.IsSource)
		{
			if (!IsGlslSource(vertex.Data) && !IsGlslSource(fragment.Data))
			{
				FURYE << name << ": VulkanShaderModule takes GLSL source or SPIR-V blobs";
				return nullptr;
			}

			// Engine GLSL: cross-compile both stages, DDC/pak-cached as
			// .spv + .refl; entry point in the SPIR-V is "main".
			ShaderCompileInput input;
			input.name = name;
			input.vertexSource.assign(reinterpret_cast<const char *>(vertex.Data.data()), vertex.Data.size());
			input.fragmentSource.assign(reinterpret_cast<const char *>(fragment.Data.data()), fragment.Data.size());

			ShaderModuleCompile compiled;
			bool cacheHit = false;
			std::string cacheError;
			if (!CompileOrLoadVulkanShader(input, compiled, &cacheHit, cacheError))
			{
				FURYE << name << ": GLSL cross-compile failed:\n" << cacheError;
				return nullptr;
			}
			FURYD << name << ": Vulkan shader " << (cacheHit ? "DDC hit" : "compiled");

			// Idempotent backstop: enforce the stage-separated set contract.
			std::string setError;
			if (!RewriteVulkanDescriptorSets(compiled.vertex.spirv, 0, "vertex", setError)
				|| !RewriteVulkanDescriptorSets(compiled.fragment.spirv, 1, "fragment", setError))
			{
				FURYE << name << ": " << setError;
				return nullptr;
			}

			out->m_Vertex = CreateModule(device, compiled.vertex.spirv, name, "vertex");
			out->m_Fragment = CreateModule(device, compiled.fragment.spirv, name, "fragment");
			if (out->m_Vertex == VK_NULL_HANDLE || out->m_Fragment == VK_NULL_HANDLE)
				return nullptr;

			AdoptStageGlobals(compiled.vertex, out->m_Globals[0]);
			AdoptStageGlobals(compiled.fragment, out->m_Globals[1]);
			for (const auto &si : compiled.vertex.stageInputs)
				out->m_StageInputs[si.name] = si.location;

			std::string reflectError;
			if (!ReflectStage(compiled.vertex.spirv, VK_SHADER_STAGE_VERTEX_BIT, reflected, out->m_StageInputs,
					name, reflectError)
				|| !ReflectStage(compiled.fragment.spirv, VK_SHADER_STAGE_FRAGMENT_BIT, reflected, out->m_StageInputs,
					name, reflectError))
			{
				FURYE << reflectError;
				return nullptr;
			}
		}
		else
		{
			// Precompiled SPIR-V blobs: stage-separated sets are rewritten in,
			// then descriptors are reflected from the same bytes.
			std::vector<unsigned char> vertSpv = vertex.Data;
			std::vector<unsigned char> fragSpv = fragment.Data;
			std::string setError;
			if (!RewriteVulkanDescriptorSets(vertSpv, 0, "vertex", setError)
				|| !RewriteVulkanDescriptorSets(fragSpv, 1, "fragment", setError))
			{
				FURYE << name << ": " << setError;
				return nullptr;
			}
			out->m_Vertex = CreateModule(device, vertSpv, name, "vertex");
			out->m_Fragment = CreateModule(device, fragSpv, name, "fragment");
			if (out->m_Vertex == VK_NULL_HANDLE || out->m_Fragment == VK_NULL_HANDLE)
				return nullptr;

			std::string reflectError;
			if (!ReflectStage(vertSpv, VK_SHADER_STAGE_VERTEX_BIT, reflected, out->m_StageInputs, name, reflectError)
				|| !ReflectStage(fragSpv, VK_SHADER_STAGE_FRAGMENT_BIT, reflected, out->m_StageInputs,
					name, reflectError))
			{
				FURYE << reflectError;
				return nullptr;
			}
		}

		MergeResources(reflected, out->m_Resources, out->m_ResourceIndex);

		// Merge both stages' member maps into the combined index.
		for (int stage = 0; stage < 2; ++stage)
		{
			for (const auto &m : out->m_Globals[stage].members)
			{
				auto it = out->m_MemberIndex.find(m.name);
				if (it == out->m_MemberIndex.end())
				{
					GlobalMember member;
					member.name = m.name;
					member.size = m.size;
					member.offset[stage] = static_cast<long>(m.offset);
					out->m_MemberIndex[m.name] = static_cast<int>(out->m_Members.size());
					out->m_Members.push_back(std::move(member));
					continue;
				}
				out->m_Members[it->second].offset[stage] = static_cast<long>(m.offset);
			}
		}

		return out;
	}

	std::shared_ptr<VulkanShaderModule> VulkanShaderModule::CreateCompute(VkDevice device, const std::string &name,
		const RHIShaderBlob &compute)
	{
		if (device == VK_NULL_HANDLE || compute.Data.empty())
			return nullptr;
		if (compute.IsSource)
		{
			FURYE << name << ": Vulkan compute takes SPIR-V blobs (GLSL compute has no cross-compile path)";
			return nullptr;
		}

		auto out = std::make_shared<VulkanShaderModule>();
		out->m_Device = device;
		out->m_Name = name;
		std::vector<unsigned char> computeSpv = compute.Data;
		std::string setError;
		if (!RewriteVulkanDescriptorSets(computeSpv, 0, "compute", setError))
		{
			FURYE << name << ": " << setError;
			return nullptr;
		}
		out->m_Compute = CreateModule(device, computeSpv, name, "compute");
		if (out->m_Compute == VK_NULL_HANDLE)
			return nullptr;

		std::vector<VulkanShaderResource> reflected;
		std::string reflectError;
		if (!ReflectStage(computeSpv, VK_SHADER_STAGE_COMPUTE_BIT, reflected, out->m_StageInputs, name, reflectError))
		{
			FURYE << reflectError;
			return nullptr;
		}
		MergeResources(reflected, out->m_Resources, out->m_ResourceIndex);
		return out;
	}

	VulkanShaderModule::~VulkanShaderModule()
	{
		ReleaseNative();
	}

	void VulkanShaderModule::ReleaseNative()
	{
		if (m_Device == VK_NULL_HANDLE)
			return;
		if (m_Compute != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_Compute, nullptr);
		if (m_Fragment != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_Fragment, nullptr);
		if (m_Vertex != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_Vertex, nullptr);
		m_Vertex = VK_NULL_HANDLE;
		m_Fragment = VK_NULL_HANDLE;
		m_Compute = VK_NULL_HANDLE;
		m_Device = VK_NULL_HANDLE;
	}

	RHIUniformHandle VulkanShaderModule::ResolveUniform(const std::string &name)
	{
		const long memberIndex = FindMemberIndex(name);
		if (memberIndex >= 0)
			return ((static_cast<int>(memberIndex) + 1) & 0xFFFFFF) | (2 << 24) | (2 << 26);
		const auto existing = m_DeferredHandles.find(name);
		if (existing != m_DeferredHandles.end())
			return existing->second;
		m_HandleNames.push_back(name);
		const RHIUniformHandle handle = ((static_cast<int>(m_HandleNames.size() - 1) + 1) & 0xFFFFFF)
			| (3 << 24) | (3 << 26);
		m_DeferredHandles[name] = handle;
		return handle;
	}

	long VulkanShaderModule::FindMemberIndex(const std::string &name) const
	{
		const auto it = m_MemberIndex.find(name);
		return it != m_MemberIndex.end() ? it->second : -1;
	}

	long VulkanShaderModule::FindResourceIndex(const std::string &name) const
	{
		const auto it = m_ResourceIndex.find(name);
		return it != m_ResourceIndex.end() ? it->second : -1;
	}

	std::vector<size_t> VulkanShaderModule::FindResourceIndices(const std::string &name) const
	{
		std::vector<size_t> out;
		for (size_t i = 0; i < m_Resources.size(); ++i)
		{
			if (m_Resources[i].name == name)
				out.push_back(i);
		}
		return out;
	}

	int VulkanShaderModule::ResolveAttribLocation(const std::string &name)
	{
		const auto it = m_StageInputs.find(name);
		return it != m_StageInputs.end() ? it->second : -1;
	}
}

#endif // FURY_RHI_VULKAN
