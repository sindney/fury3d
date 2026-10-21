#include "Fury/RHI/RHIBackend.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Fury/Log.h"
#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/RHIDevice.h"

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
			if (backend != RHIBackend::OPENGL && backend != RHIBackend::METAL)
			{
				// Each backend session implements its own probe arm; until then
				// a non-GL device always falls back to OpenGL.
				FURYE << "RHI probe: not implemented for backend '"
					<< BackendToString(backend) << "'";
				return false;
			}

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

			const char *vert = backend == RHIBackend::OPENGL ? kVertGL : kVertMSL;
			const char *frag = backend == RHIBackend::OPENGL ? kFragGL : kFragMSL;
			RHIShaderBlob vertBlob, fragBlob;
			vertBlob.Data.assign(vert, vert + std::strlen(vert));
			fragBlob.Data.assign(frag, frag + std::strlen(frag));
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
			if (GRHIDevice->CreateGraphicsPipeline(pipeDesc, module) == nullptr)
			{
				FURYE << "RHI probe: pipeline creation failed";
				return false;
			}

			GRHIDevice->BeginFrame();
			GRHIDevice->GetCommandList()->BeginBackbuffer(width, height, ClearMode::COLOR, Color(0, 0, 0, 1));
			GRHIDevice->GetCommandList()->EndRenderTargets();
			GRHIDevice->EndFrame();
			GRHIDevice->Present();
			return true;
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
