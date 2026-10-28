#include "Fury/InstancedMeshStreamer.h"
#include "Fury/RenderThread.h"

#include <cstdlib>

#include "Fury/Log.h"
#include "Fury/Mesh.h"
#include "Fury/RenderUtil.h"
#include "Fury/Shader.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/OpenGL/GLInputAssembler.h"

namespace fury
{
	namespace
	{
		const char *const kInstanceAttrNames[4] = {
			"instance_row0", "instance_row1", "instance_row2", "instance_row3"
		};
	}

	InstancedMeshStreamer &InstancedMeshStreamer::Get()
	{
		static InstancedMeshStreamer s_Instance;
		return s_Instance;
	}

	bool InstancedMeshStreamer::SSBOAvailable() const
	{
		if (m_SsboAvailable < 0)
		{
			m_SsboAvailable = 0;
			if (GRHIDevice != nullptr && GRHIDevice->GetCapabilities().SupportsSSBO)
				m_SsboAvailable = 1;
		}
		return m_SsboAvailable != 0;
	}

	bool InstancedMeshStreamer::UseSSBO() const
	{
		const char *env = std::getenv("FURY_INSTANCE_SSBO");
		if (env != nullptr && env[0] != '\0')
			return env[0] == '1' && SSBOAvailable();
		return SSBOAvailable();
	}

	void InstancedMeshStreamer::DeleteBuffers()
	{
		m_InstanceVbo = nullptr;
		m_InstanceSsbo = nullptr;
	}

	void InstancedMeshStreamer::EnsureStreamBuffer(RHIBufferRef &buffer, RHIBufferType type, const char *name)
	{
		if (buffer != nullptr)
			return;
		// Created empty; each Update reallocs to the frame's matrix count.
		RHIBufferDesc desc;
		desc.Name = name;
		desc.Type = type;
		desc.Usage = RHIBufferUsage::DYNAMIC;
		desc.Size = 0;
		buffer = GRHIDevice->CreateBuffer(desc);
	}

	void InstancedMeshStreamer::DrawInstanced(const std::shared_ptr<Shader> &shader,
		const std::shared_ptr<Mesh> &mesh, int subMesh,
		const std::vector<Matrix4> &matrices)
	{
		FURY_RHI_THREAD_GUARD();
		if (matrices.empty() || mesh == nullptr || shader == nullptr)
			return;
		if (GRHIDevice == nullptr)
			return;

		shader->BindMesh(mesh);
		if (subMesh >= 0)
			shader->BindSubMesh(mesh, static_cast<unsigned int>(subMesh));

		auto *cmd = GRHIDevice->GetCommandList();
		const size_t byteSize = matrices.size() * sizeof(Matrix4);
		const unsigned int instanceCount = static_cast<unsigned int>(matrices.size());
		const unsigned int indexCount = subMesh >= 0
			? static_cast<unsigned int>(mesh->GetSubMeshAt(subMesh)->Indices.Data.size())
			: static_cast<unsigned int>(mesh->Indices.Data.size());

		if (UseSSBO())
		{
			EnsureStreamBuffer(m_InstanceSsbo, RHIBufferType::STORAGE, "instance_ssbo");
			GRHIDevice->UpdateBuffer(m_InstanceSsbo, matrices.data(), byteSize);
			// binding point 2 matches layout(std430, binding = 2) in the shaders
			cmd->SetStorageBufferNamed("InstanceBuffer", 2, m_InstanceSsbo);
			cmd->DrawIndexedInstanced(indexCount, instanceCount);
			cmd->SetStorageBufferNamed("InstanceBuffer", 2, nullptr);
		}
		else
		{
			EnsureStreamBuffer(m_InstanceVbo, RHIBufferType::VERTEX, "instance_vbo");
			GRHIDevice->UpdateBuffer(m_InstanceVbo, matrices.data(), byteSize);

			{
				// Not an INSTANCED shader -- refuse to draw garbage.
				const bool hasInstancing = shader->GetRHIModule() != nullptr
					&& shader->GetRHIModule()->ResolveAttribLocation(kInstanceAttrNames[0]) >= 0;
				if (!hasInstancing)
				{
					FURYW << "DrawInstanced: shader '" << shader->GetName() << "' has no instance_row0 attribute!";
					return;
				}
				cmd->SetInstanceMatrixStream(m_InstanceVbo, kInstanceAttrNames);
			}

			cmd->DrawIndexedInstanced(indexCount, instanceCount);

			// Reset the instance stream so later non-instanced draws are
			// unaffected.
			cmd->SetInstanceMatrixStream(nullptr, kInstanceAttrNames);
		}

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(indexCount * instanceCount);
	}
}
