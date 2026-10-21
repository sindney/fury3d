#include "Fury/RHI/Metal/MetalShaderModule.h"

#if FURY_RHI_METAL

#include "Fury/Log.h"
#include "Fury/ShaderCompiler.h"

namespace fury
{
	long MetalStageGlobals::FindMember(const std::string &name) const
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
		NS::String *ToNS(const char *utf8)
		{
			return NS::String::alloc()->init(const_cast<char *>(utf8), NS::UTF8StringEncoding);
		}

		MTL::Library *CompileSource(MTL::Device *device, const std::string &name, const std::string &source)
		{
			NS::SharedPtr<NS::String> nsSource = NS::TransferPtr(ToNS(source.c_str()));
			NS::Error *error = nullptr;
			MTL::Library *library = device->newLibrary(nsSource.get(), nullptr, &error);
			if (library == nullptr)
			{
				FURYE << name << ": MSL compile failed: "
					<< (error != nullptr ? error->localizedDescription()->utf8String() : "unknown");
			}
			return library;
		}

		MTL::Function *GetFunction(MTL::Library *library, const char *entry, const std::string &name, const char *stage)
		{
			if (library == nullptr)
				return nullptr;
			NS::SharedPtr<NS::String> nsName = NS::TransferPtr(ToNS(entry));
			MTL::Function *fn = library->newFunction(nsName.get());
			if (fn == nullptr)
				FURYE << name << ": MSL entry point '" << entry << "' not found (" << stage << ")";
			return fn;
		}

		bool IsGlslSource(const std::string &source)
		{
			return source.find("#version") != std::string::npos;
		}

		void AdoptStageGlobals(const ShaderStageCompile &compiled, MetalStageGlobals &out)
		{
			out.blockName = compiled.globalsBlockName;
			out.blockSize = compiled.globalsBlockSize;
			out.blockIndex = compiled.globalsBlockIndex;
			out.members.reserve(compiled.globals.size());
			for (const auto &m : compiled.globals)
				out.members.push_back({ m.name, m.offset, m.size });
		}
	}

	std::shared_ptr<MetalShaderModule> MetalShaderModule::Create(MTL::Device *device, const std::string &name,
		const RHIShaderBlob &vertex, const RHIShaderBlob &fragment, const RHIShaderBlob &geometry)
	{
		if (device == nullptr || vertex.Data.empty() || fragment.Data.empty())
			return nullptr;
		if (!vertex.IsSource || !fragment.IsSource)
		{
			FURYE << name << ": MetalShaderModule takes source blobs";
			return nullptr;
		}
		if (!geometry.Data.empty())
			FURYW << name << ": Metal has no geometry stage; stage ignored";

		const std::string vertSource((const char *)vertex.Data.data(), vertex.Data.size());
		const std::string fragSource((const char *)fragment.Data.data(), fragment.Data.size());

		auto out = std::make_shared<MetalShaderModule>();
		out->m_Name = name;

		if (IsGlslSource(vertSource) || IsGlslSource(fragSource))
		{
			// Engine GLSL: cross-compile per stage (Slang front-end -> SPIR-V
			// -> SPIRV-Cross), then one MSL library per stage.
			ShaderCompileInput input;
			input.name = name;
			input.vertexSource = vertSource;
			input.fragmentSource = fragSource;

			ShaderModuleCompile compiled;
			if (!CompileGlslToMSL(input, compiled))
			{
				FURYE << name << ": GLSL cross-compile failed:\n" << compiled.error;
				return nullptr;
			}

			if (getenv("FURY_METAL_DUMP_MSL") != nullptr)
			{
				const std::string base = std::string("/tmp/msl_") + (name.find_last_of('/') != std::string::npos ? name.substr(name.find_last_of('/') + 1) : name);
				if (FILE *f = fopen((base + ".vert.metal").c_str(), "wb"))
				{
					fwrite(compiled.vertex.msl.data(), 1, compiled.vertex.msl.size(), f);
					fclose(f);
				}
				if (FILE *f = fopen((base + ".frag.metal").c_str(), "wb"))
				{
					fwrite(compiled.fragment.msl.data(), 1, compiled.fragment.msl.size(), f);
					fclose(f);
				}
			}

			out->m_VertexLibrary = CompileSource(device, name + " (vertex)", compiled.vertex.msl);
			out->m_FragmentLibrary = CompileSource(device, name + " (fragment)", compiled.fragment.msl);
			if (out->m_VertexLibrary == nullptr || out->m_FragmentLibrary == nullptr)
				return nullptr;
			out->m_CrossCompiled = true;
			out->m_Vertex = GetFunction(out->m_VertexLibrary, "vertexMain", name, "vertex");
			out->m_Fragment = GetFunction(out->m_FragmentLibrary, "fragmentMain", name, "fragment");

			AdoptStageGlobals(compiled.vertex, out->m_Globals[0]);
			AdoptStageGlobals(compiled.fragment, out->m_Globals[1]);
			for (const auto &si : compiled.vertex.stageInputs)
				out->m_StageInputs[si.name] = si.location;

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
		}
		else
		{
			// Hand-authored MSL twin: one library from the concatenated pair.
			out->m_Library = CompileSource(device, name, vertSource + "\n" + fragSource);
			if (out->m_Library == nullptr)
				return nullptr;
			out->m_Vertex = GetFunction(out->m_Library, "vertexMain", name, "vertex");
			out->m_Fragment = GetFunction(out->m_Library, "fragmentMain", name, "fragment");
		}

		if (out->m_Vertex == nullptr || out->m_Fragment == nullptr)
			return nullptr;
		return out;
	}

	std::shared_ptr<MetalShaderModule> MetalShaderModule::CreateCompute(MTL::Device *device, const std::string &name,
		const RHIShaderBlob &compute)
	{
		if (device == nullptr || compute.Data.empty() || !compute.IsSource)
			return nullptr;

		MTL::Library *library = CompileSource(device, name,
			std::string((const char *)compute.Data.data(), compute.Data.size()));
		if (library == nullptr)
			return nullptr;

		auto out = std::make_shared<MetalShaderModule>();
		out->m_Library = library;
		out->m_Compute = GetFunction(library, "computeMain", name, "compute");
		if (out->m_Compute == nullptr)
			return nullptr;
		return out;
	}

	MetalShaderModule::~MetalShaderModule()
	{
		if (m_Compute != nullptr) m_Compute->release();
		if (m_Fragment != nullptr) m_Fragment->release();
		if (m_Vertex != nullptr) m_Vertex->release();
		if (m_FragmentLibrary != nullptr) m_FragmentLibrary->release();
		if (m_VertexLibrary != nullptr) m_VertexLibrary->release();
		if (m_Library != nullptr) m_Library->release();
	}

	fury::RHIUniformHandle MetalShaderModule::ResolveUniform(const std::string &name)
	{
		// Globals members carry their offsets: kind-2 handle directly.
		if (FindMemberIndex(name) >= 0)
			return ((static_cast<int>(FindMemberIndex(name)) + 1) & 0xFFFFFF) | (2 << 24) | (2 << 26);
		// Everything else defers by name (resolved per PSO at bind time).
		m_HandleNames.push_back(name);
		return (static_cast<int>(m_HandleNames.size() - 1) + 1) | (3 << 24) | (3 << 26);
	}

	long MetalShaderModule::FindMemberIndex(const std::string &name) const
	{
		const auto it = m_MemberIndex.find(name);
		return it != m_MemberIndex.end() ? it->second : -1;
	}

	int MetalShaderModule::ResolveAttribLocation(const std::string &name)
	{
		const auto it = m_StageInputs.find(name);
		return it != m_StageInputs.end() ? it->second : -1;
	}
}

#endif // FURY_RHI_METAL
