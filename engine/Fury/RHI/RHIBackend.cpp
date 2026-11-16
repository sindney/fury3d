#include "Fury/RHI/RHIBackend.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "Fury/Log.h"
#include "Fury/ShaderCache.h"
#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/RHIDevice.h"

#if FURY_RHI_VULKAN
#include "Fury/RHI/Vulkan/VulkanProbeShader.h"
#endif

namespace fury
{
	// Backend factories, one per backend translation unit.
	RHIDevice *CreateOpenGLDevice(const RHIWindowInfo &window);

#if FURY_RHI_VULKAN
	RHIDevice *CreateVulkanDevice(const RHIWindowInfo &window);
#endif

#if FURY_RHI_METAL
	RHIDevice *CreateMetalDevice(const RHIWindowInfo &window);
#endif

	RHIDevice *GRHIDevice = nullptr;

	void RHIDevice::TrackRenderTarget(const RHITextureRef &texture)
	{
		if (texture == nullptr)
			return;
		const RHITexture *raw = texture.get();
		for (const auto &existing : m_FrameRenderTargets)
			if (existing.get() == raw)
				return;
		m_FrameRenderTargets.push_back(texture);
	}

	void RHIDevice::BeginFrameTracking()
	{
		m_FrameRenderTargets.clear();
	}

	namespace RHI
	{
		std::string BackendToString(RHIBackend backend)
		{
			switch (backend)
			{
			case RHIBackend::OPENGL: return "opengl";
			case RHIBackend::VULKAN: return "vulkan";
			case RHIBackend::METAL: return "metal";
			}
			return "unknown";
		}

		bool BackendFromString(const std::string &name, RHIBackend &backend)
		{
			std::string lower = name;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

			if (lower == "opengl" || lower == "gl")
			{
				backend = RHIBackend::OPENGL;
				return true;
			}
			if (lower == "vulkan" || lower == "vk")
			{
				backend = RHIBackend::VULKAN;
				return true;
			}
			if (lower == "metal" || lower == "mtl")
			{
				backend = RHIBackend::METAL;
				return true;
			}
			return false;
		}

		bool IsBackendCompiled(RHIBackend backend)
		{
			switch (backend)
			{
			case RHIBackend::OPENGL:
				return true;
			case RHIBackend::VULKAN:
				return FURY_RHI_VULKAN != 0;
			case RHIBackend::METAL:
				return FURY_RHI_METAL != 0;
			}
			return false;
		}

		bool IsBackendSupported(RHIBackend backend)
		{
			if (!IsBackendCompiled(backend))
				return false;

			switch (backend)
			{
			case RHIBackend::OPENGL:
				return PLATFORM_DESKTOP != 0;
			case RHIBackend::VULKAN:
				return PLATFORM_DESKTOP != 0;
			case RHIBackend::METAL:
				return PLATFORM_MACOS != 0;
			}
			return false;
		}

		std::vector<RHIBackend> GetSupportedBackends()
		{
			std::vector<RHIBackend> result;
			for (RHIBackend backend : { RHIBackend::OPENGL, RHIBackend::VULKAN, RHIBackend::METAL })
			{
				if (IsBackendSupported(backend))
					result.push_back(backend);
			}
			return result;
		}

		std::vector<std::string> GetSupportedBackendNames()
		{
			std::vector<std::string> result;
			for (RHIBackend backend : GetSupportedBackends())
				result.push_back(BackendToString(backend));
			return result;
		}

		namespace
		{
			// Selection sources. Empty = unset; resolution order in ResolveBackend.
			std::string s_CliBackend;

			std::string s_IniBackend;

			RHIBackend ResolveValue(const std::string &value, const char *source)
			{
				RHIBackend backend;
				if (!BackendFromString(value, backend))
				{
					FURYW << "Unknown RHI backend '" << value << "' (" << source << "); using 'opengl'";
					return RHIBackend::OPENGL;
				}
				if (!IsBackendSupported(backend))
				{
					FURYW << "RHI backend '" << value << "' (" << source
						<< ") is not supported on this platform/build; using 'opengl'";
					return RHIBackend::OPENGL;
				}
				FURYI << "RHI backend '" << BackendToString(backend) << "' selected by " << source;
				return backend;
			}
		}

		void SetCommandLineBackend(const std::string &name)
		{
			s_CliBackend = name;
		}

		void SetIniBackend(const std::string &name)
		{
			s_IniBackend = name;
		}

		RHIBackend ResolveBackend(const std::string &luaOption)
		{
			if (!s_CliBackend.empty())
				return ResolveValue(s_CliBackend, "--rhi");
			if (const char *env = std::getenv("FURY_RHI"); env != nullptr && env[0] != '\0')
				return ResolveValue(env, "FURY_RHI");
			if (!luaOption.empty())
				return ResolveValue(luaOption, "Engine.run option");
			if (!s_IniBackend.empty())
				return ResolveValue(s_IniBackend, "editor ini");
			return RHIBackend::OPENGL;
		}

		bool ProbeActiveDevice(int width, int height)
		{
			if (GRHIDevice == nullptr)
				return false;

			const RHIBackend backend = GRHIDevice->GetBackend();

			RHITextureDesc texDesc;
			texDesc.Name = "RHIProbe";
			texDesc.Width = 64;
			texDesc.Height = 64;
			RHITextureRef tex = GRHIDevice->CreateTexture(texDesc);
			if (tex == nullptr)
			{
				FURYE << "RHI probe: texture creation failed";
				return false;
			}
			std::vector<unsigned char> pixels(64 * 64 * 4, 0xFF);
			GRHIDevice->UpdateTexture(tex, pixels.data(), pixels.size());

			static const char *kVertGL =
				"#version 330 core\n"
				"layout(location = 0) in vec3 a_position;\n"
				"void main() { gl_Position = vec4(a_position, 1.0); }\n";
			static const char *kFragGL =
				"#version 330 core\n"
				"layout(location = 0) out vec4 o_color;\n"
				"void main() { o_color = vec4(0.0, 0.0, 0.0, 1.0); }\n";
			static const char *kVertMSL =
				"#include <metal_stdlib>\n"
				"using namespace metal;\n"
				"vertex float4 vertexMain() { return float4(0.0, 0.0, 0.0, 1.0); }\n";
			static const char *kFragMSL =
				"#include <metal_stdlib>\n"
				"using namespace metal;\n"
				"fragment float4 fragmentMain() { return float4(0.0, 0.0, 0.0, 1.0); }\n";
			const char *vert = backend == RHIBackend::METAL ? kVertMSL : kVertGL;
			const char *frag = backend == RHIBackend::METAL ? kFragMSL : kFragGL;
			RHIShaderBlob vertBlob, fragBlob;
			if (backend == RHIBackend::VULKAN)
			{
#if FURY_RHI_VULKAN
				vertBlob.Data.assign(reinterpret_cast<const unsigned char *>(VulkanProbeVertex),
					reinterpret_cast<const unsigned char *>(VulkanProbeVertex) + sizeof(VulkanProbeVertex));
				fragBlob.Data.assign(reinterpret_cast<const unsigned char *>(VulkanProbeFragment),
					reinterpret_cast<const unsigned char *>(VulkanProbeFragment) + sizeof(VulkanProbeFragment));
				vertBlob.IsSource = false;
				fragBlob.IsSource = false;
#endif
			}
			else
			{
				vertBlob.Data.assign(vert, vert + std::strlen(vert));
				fragBlob.Data.assign(frag, frag + std::strlen(frag));
			}
			RHIShaderModuleRef module = GRHIDevice->CreateShaderModule("RHIProbe", vertBlob, fragBlob, RHIShaderBlob{});
			if (module == nullptr)
			{
				FURYE << "RHI probe: shader compile failed";
				return false;
			}

			RHIGraphicsPipelineDesc pipeDesc;
			pipeDesc.Name = "RHIProbe";
			pipeDesc.DepthTest = false;
			pipeDesc.DepthWrite = false;
			if (backend == RHIBackend::VULKAN)
			{
				RHIVertexAttribute attr;
				attr.Name = "a_position";
				attr.Format = RHIVertexFormat::FLOAT3;
				attr.Location = 0;
				attr.Stride = 12;
				pipeDesc.VertexAttributes.push_back(attr);
			}
			RHIGraphicsPipelineRef pipeline = GRHIDevice->CreateGraphicsPipeline(pipeDesc, module);
			if (pipeline == nullptr)
			{
				FURYE << "RHI probe: pipeline creation failed";
				return false;
			}

			GRHIDevice->BeginFrame();
			GRHIDevice->GetCommandList()->BeginBackbuffer(width, height, ClearMode::COLOR, Color(0, 0, 0, 1));
			if (backend == RHIBackend::VULKAN)
			{
				static const float vertices[] = { -1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f };
				RHIBufferDesc bufferDesc;
				bufferDesc.Name = "RHIProbe vertices";
				bufferDesc.Type = RHIBufferType::VERTEX;
				bufferDesc.Size = sizeof(vertices);
				RHIBufferRef buffer = GRHIDevice->CreateBuffer(bufferDesc, vertices);
				GRHIDevice->GetCommandList()->SetPipeline(pipeline);
				GRHIDevice->GetCommandList()->SetVertexBuffer(buffer);
				GRHIDevice->GetCommandList()->SetTexture("src", tex);
				GRHIDevice->GetCommandList()->Draw(3);
			}
			GRHIDevice->GetCommandList()->EndRenderTargets();
			GRHIDevice->EndFrame();
			GRHIDevice->Present();
			if (backend == RHIBackend::VULKAN)
			{
				RHIPixelData readback;
				if (!GRHIDevice->ReadbackBackbuffer(readback))
					return false;
				const size_t center = (static_cast<size_t>(readback.Height / 2) * readback.Width + readback.Width / 2) * 4;
				FURYI << "RHI probe center RGBA: " << static_cast<int>(readback.Pixels[center]) << ","
					<< static_cast<int>(readback.Pixels[center + 1]) << "," << static_cast<int>(readback.Pixels[center + 2])
					<< "," << static_cast<int>(readback.Pixels[center + 3]);
			}
			return true;
		}

		bool ProbeClusteredCompute()
		{
			if (GRHIDevice == nullptr)
				return false;
			GRHIDevice->SetClusteredLightingSupported(false);
			auto fail = [&](const char *message) {
				FURYW << "RHI clustered compute probe: " << message;
				return false;
			};
			const auto &caps = GRHIDevice->GetCapabilities();
			if (!caps.SupportsCompute || !caps.SupportsSSBO)
				return fail("SKIP (compute or storage buffers unavailable)");

			static const char *source = R"GLSL(#version 430 core
layout(std430, binding = 0) buffer CounterBuffer { uint counter; };
#if defined(VERTEX_SHADER)
void main()
{
	vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#elif defined(FRAGMENT_SHADER)
layout(location = 0) out vec4 outColor;
void main()
{
	outColor = vec4(counter == 128u ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
#elif defined(COMPUTE_SHADER)
layout(local_size_x = 128) in;
void main()
{
	atomicAdd(counter, 1u);
}
#endif
)GLSL";
			const std::string sourceString(source);
			ShaderCompileInput computeInput = BuildComputeShaderCompileInput(
				"RHIClusteredComputeProbe", sourceString, {}, false, false);
			ShaderCompileInput graphicsInput = BuildShaderCompileInput(
				"RHIClusteredGraphicsProbe", sourceString, {}, false, false);
			auto sourceBlob = [](const std::string &text,
				const std::vector<std::pair<std::string, std::string>> &defines = {}) {
				RHIShaderBlob blob;
				blob.Data.assign(text.begin(), text.end());
				blob.Defines = defines;
				blob.IsSource = true;
				return blob;
			};
			auto computeModule = GRHIDevice->CreateComputeModule(
				"RHIClusteredComputeProbe", sourceBlob(computeInput.computeSource, computeInput.defines));
			if (computeModule == nullptr || !computeModule->IsValid())
				return fail("compute module creation failed");
			auto graphicsModule = GRHIDevice->CreateShaderModule("RHIClusteredGraphicsProbe",
				sourceBlob(graphicsInput.vertexSource, graphicsInput.defines),
				sourceBlob(graphicsInput.fragmentSource, graphicsInput.defines), RHIShaderBlob{});
			if (graphicsModule == nullptr || !graphicsModule->IsValid())
				return fail("graphics module creation failed");

			RHIGraphicsPipelineDesc pipelineDesc;
			pipelineDesc.Name = "RHIClusteredGraphicsProbe";
			pipelineDesc.DepthTest = false;
			pipelineDesc.DepthWrite = false;
			auto pipeline = GRHIDevice->CreateGraphicsPipeline(pipelineDesc, graphicsModule);
			if (pipeline == nullptr)
				return fail("graphics pipeline creation failed");
			RHIBufferDesc bufferDesc;
			bufferDesc.Name = "RHIClusteredCounter";
			bufferDesc.Type = RHIBufferType::STORAGE;
			bufferDesc.Usage = RHIBufferUsage::DYNAMIC;
			bufferDesc.Size = sizeof(unsigned int);
			bufferDesc.Stride = sizeof(unsigned int);
			auto counter = GRHIDevice->CreateBuffer(bufferDesc);
			if (counter == nullptr)
				return fail("counter creation failed");
			RHITextureDesc textureDesc;
			textureDesc.Name = "RHIClusteredReadback";
			textureDesc.Width = 1;
			textureDesc.Height = 1;
			textureDesc.RenderTarget = true;
			auto texture = GRHIDevice->CreateTexture(textureDesc);
			if (texture == nullptr)
				return fail("readback target creation failed");
			RHIRenderTargetSetDesc targetDesc;
			targetDesc.Colors.resize(1);
			targetDesc.Colors[0].Texture = texture;
			auto targets = GRHIDevice->CreateRenderTargetSet(targetDesc);
			if (targets == nullptr)
				return fail("render target creation failed");

			GRHIDevice->BeginFrame();
			auto *cmd = GRHIDevice->GetCommandList();
			cmd->ClearStorageBuffer(counter);
			cmd->Barrier();
			cmd->BindShaderModule(computeModule);
			cmd->SetStorageBufferNamed("CounterBuffer", 0, counter);
			cmd->Dispatch(1, 1, 1);
			cmd->Barrier();
			cmd->BeginRenderTargets(targets, ClearMode::NONE, Color(0, 0, 0, 1));
			cmd->SetPipeline(pipeline);
			cmd->SetStorageBufferNamed("CounterBuffer", 0, counter);
			cmd->Draw(3);
			cmd->EndRenderTargets();
			GRHIDevice->EndFrame();
			GRHIDevice->Present();

			RHIPixelData readback;
			if (!GRHIDevice->Readback(texture, readback) || readback.Pixels.size() < 4)
				return fail("readback failed");
			if (readback.Pixels[0] != 255 || readback.Pixels[1] != 0)
			{
				FURYW << "RHI clustered compute probe RGBA: " << static_cast<int>(readback.Pixels[0]) << ","
					<< static_cast<int>(readback.Pixels[1]) << "," << static_cast<int>(readback.Pixels[2])
					<< "," << static_cast<int>(readback.Pixels[3]);
				return fail("compute-to-graphics counter mismatch");
			}
			GRHIDevice->SetClusteredLightingSupported(true);
			FURYI << "RHI clustered compute probe: PASS";
			return true;
		}

		int ProbeIndexedIndirectSmoke()
		{
			if (GRHIDevice == nullptr || (GRHIDevice->GetBackend() != RHIBackend::VULKAN
				&& GRHIDevice->GetBackend() != RHIBackend::METAL))
				return 1;
			const char *backend = GRHIDevice->GetBackend() == RHIBackend::METAL ? "Metal" : "Vulkan";
			const char *output = GRHIDevice->GetBackend() == RHIBackend::METAL ? "metal_indirect_smoke" : "vulkan_indirect_smoke";
			const auto &caps = GRHIDevice->GetCapabilities();
			if (!caps.SupportsMultiDrawIndirect || !caps.SupportsIndirectFirstInstance
				|| !caps.SupportsIndirectCount)
			{
				FURYW << backend << " indirect smoke: SKIP (required indexed indirect or first-instance support is unavailable)";
				return 77;
			}
			auto fail = [&](const char *message) {
				FURYE << backend << " indirect smoke: " << message;
				return 1;
			};
			size_t capacity = 3;
			if (const char *value = std::getenv("FURY_INDIRECT_SMOKE_CAPACITY"))
				capacity = std::min<size_t>(std::max<size_t>(std::strtoull(value, nullptr, 10), 3), 1000000);
			if (caps.MaxDrawIndirectCount == 0)
				return 77;
			const size_t chunkCount = (capacity + caps.MaxDrawIndirectCount - 1) / caps.MaxDrawIndirectCount;

			const char *computeText = R"GLSL(#version 430 core
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer IndirectArguments { uint arguments[]; };
layout(std430, binding = 1) buffer IndirectCount { uint count; };
layout(std430, binding = 2) buffer Observations { uint observations[16]; };
void WriteCommand(uint command, uint instanceCount, uint firstInstance)
{
	uint base = command * 5u;
	arguments[base] = 3u;
	arguments[base + 1u] = instanceCount;
	arguments[base + 2u] = 0u;
	arguments[base + 3u] = 0u;
	arguments[base + 4u] = firstInstance;
}
void main()
{
	WriteCommand(0u, 1u, 3u);
	WriteCommand(1u, 1u, 7u);
	arguments[7] = 3u;
#if defined(METAL_INDIRECT_PROBE)
	WriteCommand(2u, 1u, 11u);
#else
	WriteCommand(2u, 0u, 11u);
#endif
	count = 2u;
	for (uint i = 0u; i < 16u; ++i) observations[i] = 0u;
}
)GLSL";
			const char *graphicsText = R"GLSL(#version 430 core
#if defined(VERTEX_SHADER)
#extension GL_ARB_shader_draw_parameters : require
in vec3 vertex_position;
in vec4 vertex_tangent;
uniform float probe_scale;
layout(std430, binding = 2) buffer Observations { uint observations[16]; };
void main()
{
	atomicAdd(observations[gl_BaseInstance], 1u);
	if (probe_scale == 1.0 && vertex_tangent.w == 1.0) atomicAdd(observations[13], 1u);
	if (gl_VertexIndex == 0 && vertex_position.x < -0.5) atomicAdd(observations[12], 1u);
	gl_Position = vec4(vertex_position * probe_scale, 1.0);
}
#elif defined(FRAGMENT_SHADER)
layout(binding = 0) uniform sampler2D ProbeTexture;
uniform vec4 probe_tint;
layout(std430, binding = 2) buffer Observations { uint observations[16]; };
layout(location = 0) out vec4 outColor;
void main()
{
	vec4 texel = texture(ProbeTexture, vec2(0.5));
	if (probe_tint == vec4(1.0)) atomicAdd(observations[14], 1u);
	if (texel.r > 0.0 && texel.a > 0.0) atomicAdd(observations[15], 1u);
	outColor = texel * probe_tint;
}
#endif
)GLSL";
			auto blob = [](const std::string &text,
				const std::vector<std::pair<std::string, std::string>> &defines = {}) {
				RHIShaderBlob blob;
				blob.Data.assign(text.begin(), text.end());
				blob.Defines = defines;
				blob.IsSource = true;
				return blob;
			};
			ShaderCompileInput computeInput = BuildComputeShaderCompileInput(
				"RHIIndirectSmokeCompute", computeText,
				GRHIDevice->GetBackend() == RHIBackend::METAL
					? std::vector<std::string>{ "METAL_INDIRECT_PROBE" } : std::vector<std::string>{}, false, false);
			ShaderCompileInput graphicsInput = BuildShaderCompileInput(
				"RHIIndirectSmokeGraphics", graphicsText, { "CLUSTER_BASE_INSTANCE" }, false, false);
			auto compute = GRHIDevice->CreateComputeModule("RHIIndirectSmokeCompute",
				blob(computeInput.computeSource, computeInput.defines));
			if (compute == nullptr || !compute->IsValid())
				return fail("compute module creation failed");
			auto graphics = GRHIDevice->CreateShaderModule("RHIIndirectSmokeGraphics",
				blob(graphicsInput.vertexSource, graphicsInput.defines),
				blob(graphicsInput.fragmentSource, graphicsInput.defines), RHIShaderBlob{});
			if (graphics == nullptr || !graphics->IsValid())
				return fail("graphics module creation failed");
			RHIGraphicsPipelineDesc pipelineDesc;
			pipelineDesc.Name = "RHIIndirectSmokePipeline";
			pipelineDesc.DepthTest = false;
			pipelineDesc.DepthWrite = false;
			RHIVertexAttribute position;
			position.Name = "vertex_position";
			position.Format = RHIVertexFormat::FLOAT3;
			position.Stride = 3 * sizeof(float);
			position.Stream = 0;
			position.Location = graphics->ResolveAttribLocation(position.Name);
			pipelineDesc.VertexAttributes.push_back(position);
			RHIVertexAttribute tangent;
			tangent.Name = "vertex_tangent";
			tangent.Format = RHIVertexFormat::FLOAT4;
			tangent.Stride = 4 * sizeof(float);
			tangent.Stream = 1;
			tangent.Location = graphics->ResolveAttribLocation(tangent.Name);
			tangent.ConstantFill = true;
			pipelineDesc.VertexAttributes.push_back(tangent);
			auto pipeline = GRHIDevice->CreateGraphicsPipeline(pipelineDesc, graphics);
			if (pipeline == nullptr)
				return fail("graphics pipeline creation failed");

			auto makeBuffer = [&](const char *name, RHIBufferType type, size_t size, const void *data = nullptr) {
				RHIBufferDesc desc;
				desc.Name = name;
				desc.Type = type;
				desc.Usage = RHIBufferUsage::STATIC;
				desc.UsageFlags = RHI_BUFFER_TRANSFER_SRC | RHI_BUFFER_INDIRECT;
				desc.Size = size;
				return GRHIDevice->CreateBuffer(desc, data);
			};
			auto arguments = makeBuffer("RHIIndirectSmokeArguments", RHIBufferType::STORAGE,
				capacity * sizeof(RHIIndirectDrawArgs));
			auto counts = makeBuffer("RHIIndirectSmokeCount", RHIBufferType::STORAGE,
				chunkCount * sizeof(std::uint32_t));
			std::vector<std::uint32_t> zeroArguments(capacity * 5, 0);
			std::vector<std::uint32_t> zeroCounts(chunkCount, 0);
			GRHIDevice->UpdateBuffer(arguments, zeroArguments.data(), zeroArguments.size() * sizeof(std::uint32_t));
			GRHIDevice->UpdateBuffer(counts, zeroCounts.data(), zeroCounts.size() * sizeof(std::uint32_t));
			auto observations = makeBuffer("RHIIndirectSmokeObservations", RHIBufferType::STORAGE, 16 * sizeof(std::uint32_t));
			const std::uint32_t indices[] = { 0, 1, 2, 3, 4, 5 };
			auto indexBuffer = makeBuffer("RHIIndirectSmokeIndices", RHIBufferType::INDEX, sizeof(indices), indices);
			const float positions[] = {
				-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f,
				1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f };
			auto positionBuffer = makeBuffer("RHIIndirectSmokePositions", RHIBufferType::VERTEX, sizeof(positions), positions);
			const float constantVertex[] = { 0.0f, 0.0f, 0.0f, 1.0f };
			auto constantBuffer = makeBuffer("RHIIndirectSmokeConstantVertex", RHIBufferType::VERTEX, sizeof(constantVertex), constantVertex);
			RHITextureDesc probeTextureDesc;
			probeTextureDesc.Name = "RHIIndirectSmokeTexture";
			probeTextureDesc.Width = 1;
			probeTextureDesc.Height = 1;
			auto probeTexture = GRHIDevice->CreateTexture(probeTextureDesc);
			const unsigned char probePixel[] = { 17, 34, 51, 255 };
			if (probeTexture)
				GRHIDevice->UpdateTexture(probeTexture, probePixel, sizeof(probePixel));
			if (!arguments || !counts || !observations || !indexBuffer || !positionBuffer || !constantBuffer || !probeTexture)
				return fail("resource creation failed");

			RHITextureDesc textureDesc;
			textureDesc.Name = "RHIIndirectSmokeTarget";
			textureDesc.Width = 1;
			textureDesc.Height = 1;
			textureDesc.RenderTarget = true;
			auto texture = GRHIDevice->CreateTexture(textureDesc);
			RHIRenderTargetSetDesc targetDesc;
			targetDesc.Colors.resize(1);
			targetDesc.Colors[0].Texture = texture;
			auto targets = texture ? GRHIDevice->CreateRenderTargetSet(targetDesc) : nullptr;
			if (!targets)
				return fail("render target creation failed");

			RHIIndirectBatchDesc batchDesc;
			batchDesc.Arguments = arguments;
			batchDesc.Counts = counts;
			batchDesc.Capacity = static_cast<std::uint32_t>(capacity);
			batchDesc.Pipeline = pipeline;
			batchDesc.IndexBuffer = indexBuffer;
			batchDesc.VertexBuffers.push_back({ 0, positionBuffer });
			batchDesc.VertexBuffers.push_back({ 1, constantBuffer });
			batchDesc.StorageBuffers.push_back({ "Observations", 2, observations });
			batchDesc.Textures.push_back({ "ProbeTexture", probeTexture });
			auto batch = GRHIDevice->CreateIndirectBatch(batchDesc);
			if (!batch)
				return fail("indirect batch creation failed");

			GRHIDevice->BeginFrame();
			auto *cmd = GRHIDevice->GetCommandList();
			cmd->BindShaderModule(compute);
			cmd->SetStorageBufferNamed("IndirectArguments", 0, arguments);
			cmd->SetStorageBufferNamed("IndirectCount", 1, counts);
			cmd->SetStorageBufferNamed("Observations", 2, observations);
			cmd->Dispatch(1, 1, 1);
			cmd->BeginRenderTargets(targets, ClearMode::NONE, Color(0, 0, 0, 1));
			RHIMeshGeometryDesc staleGeometry;
			staleGeometry.ChannelCount = 1;
			staleGeometry.Channels[0].Name = "vertex_position";
			staleGeometry.Channels[0].Attrib = 0;
			staleGeometry.Channels[0].Buffer = indexBuffer;
			staleGeometry.Channels[0].Empty = false;
			staleGeometry.Channels[0].Components = 3;
			cmd->SetMeshGeometry(staleGeometry);
			if (!cmd->PrepareIndirectBatch(batch))
				return fail("indirect preparation failed");
			cmd->SetPipeline(pipeline);
			cmd->SetUniformFloat("probe_scale", 1.0f);
			cmd->SetUniformFloat("probe_tint", 1.0f, 1.0f, 1.0f, 1.0f);
			if (!cmd->DrawIndexedIndirect(batch))
				return fail("indirect execution failed");
			cmd->EndRenderTargets();
			GRHIDevice->EndFrame();
			GRHIDevice->Present();

			RHIBufferReadbackRequest countRequest;
			countRequest.Buffer = counts;
			countRequest.Size = sizeof(std::uint32_t);
			countRequest.FrameIndex = 1;
			RHIBufferReadbackResult countResult;
			RHIBufferReadbackRequest observationRequest;
			observationRequest.Buffer = observations;
			observationRequest.Size = 16 * sizeof(std::uint32_t);
			observationRequest.FrameIndex = 1;
			RHIBufferReadbackResult observationResult;
			if (!GRHIDevice->ReadbackBuffer(countRequest, countResult)
				|| !GRHIDevice->ReadbackBuffer(observationRequest, observationResult))
				return fail("buffer readback failed");
			if (countResult.Bytes.size() != sizeof(std::uint32_t)
				|| observationResult.Bytes.size() != 16 * sizeof(std::uint32_t))
				return fail("buffer readback size mismatch");
			std::uint32_t gpuCount = 0;
			std::uint32_t values[16];
			std::memcpy(&gpuCount, countResult.Bytes.data(), sizeof(gpuCount));
			std::memcpy(values, observationResult.Bytes.data(), sizeof(values));
			std::uint32_t rangeLocation = 0;
			std::uint32_t rangeLength = gpuCount;
			std::vector<std::uint32_t> ranges;
			if (batch->GetExecutionRangeBuffer())
			{
				RHIBufferReadbackRequest rangeRequest;
				rangeRequest.Buffer = batch->GetExecutionRangeBuffer();
				rangeRequest.Size = chunkCount * 2 * sizeof(std::uint32_t);
				rangeRequest.FrameIndex = 1;
				RHIBufferReadbackResult rangeResult;
				if (!GRHIDevice->ReadbackBuffer(rangeRequest, rangeResult)
					|| rangeResult.Bytes.size() != rangeRequest.Size)
					return fail("execution-range readback failed");
				ranges.resize(chunkCount * 2);
				std::memcpy(ranges.data(), rangeResult.Bytes.data(), rangeRequest.Size);
				rangeLocation = ranges[0];
				rangeLength = ranges[1];
			}
			RHIPixelData targetPixels;
			const bool targetReadback = GRHIDevice->Readback(texture, targetPixels);
			std::cout << output << ": debug gpuCount=" << gpuCount
				<< " id0=" << values[0] << " id1=" << values[1] << " id3=" << values[3]
				<< " id7=" << values[7] << " id11=" << values[11] << " positions=" << values[12]
				<< " scale=" << values[13] << " tint=" << values[14] << " texture=" << values[15]
				<< " capacity=" << capacity << " chunks=" << chunkCount
				<< " range=" << rangeLocation << "," << rangeLength;
			if (targetPixels.Pixels.size() >= 4)
				std::cout << " rgba=" << static_cast<int>(targetPixels.Pixels[0]) << ","
					<< static_cast<int>(targetPixels.Pixels[1]) << "," << static_cast<int>(targetPixels.Pixels[2])
					<< "," << static_cast<int>(targetPixels.Pixels[3]);
			std::cout << "\n";
			if (gpuCount != 2 || values[3] != 3 || values[7] != 3 || values[11] != 0 || values[12] != 1
				|| values[13] != 6 || values[14] != 1 || values[15] != 1
				|| rangeLocation != 0 || rangeLength != 2)
				return fail("GPU count, draw IDs, vertex/global observations, or execution range mismatch");
			for (size_t chunk = 1; chunk < chunkCount && !ranges.empty(); ++chunk)
				if (ranges[chunk * 2] != chunk * caps.MaxDrawIndirectCount || ranges[chunk * 2 + 1] != 0)
					return fail("split execution range mismatch");
			for (uint32_t i = 0; i < 16; ++i)
			{
				if (i < 12 && i != 3 && i != 7 && values[i] != 0)
					return fail("unexpected draw ID observation");
			}
			if (!targetReadback || targetPixels.Pixels.size() < 4
				|| targetPixels.Pixels[0] != probePixel[0] || targetPixels.Pixels[1] != probePixel[1]
				|| targetPixels.Pixels[2] != probePixel[2] || targetPixels.Pixels[3] != probePixel[3])
				return fail("texture, uniform, or render-target output mismatch");
			const char *mode = caps.SupportsNativeIndirectBatchEncoding ? "gpu-encoded-icb-range" : "mdi-count";
			std::cout << output << ": frame=1 mode=" << mode
				<< " gpuCount=" << gpuCount << " id3=" << values[3]
				<< " id7=" << values[7] << " id11=" << values[11] << " PASS\n";
			return 0;
		}

		RHIDevice *CreateDevice(RHIBackend backend, const RHIWindowInfo &window)
		{
			if (GRHIDevice != nullptr)
			{
				FURYW << "RHI device already created (" << BackendToString(GRHIDevice->GetBackend()) << ")";
				return GRHIDevice;
			}

			if (!IsBackendSupported(backend))
			{
				FURYE << "RHI backend '" << BackendToString(backend) << "' is not supported on this platform/build";
				return nullptr;
			}

			RHIDevice *device = nullptr;
			switch (backend)
			{
			case RHIBackend::OPENGL:
				device = CreateOpenGLDevice(window);
				break;
			case RHIBackend::VULKAN:
#if FURY_RHI_VULKAN
				device = CreateVulkanDevice(window);
#endif
				break;
			case RHIBackend::METAL:
#if FURY_RHI_METAL
				device = CreateMetalDevice(window);
#endif
				break;
			}

			if (device == nullptr)
			{
				FURYE << "RHI backend '" << BackendToString(backend) << "' failed to create a device";
				return nullptr;
			}

			GRHIDevice = device;
			FURYI << "RHI backend: " << BackendToString(backend);
			return device;
		}

		void DestroyDevice()
		{
			delete GRHIDevice;
			GRHIDevice = nullptr;
		}
	}
}
