#include "Fury/InstancedMeshStreamer.h"

#include <cstdlib>

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Mesh.h"
#include "Fury/RenderUtil.h"
#include "Fury/Shader.h"

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
			// SSBO is core in GL 4.3; the loader resolves the entry points
			// only when the driver exposes them.
			if (gl::HasGLContext() && gl::IsVersionGEQ(4, 3) && glBindBufferBase != nullptr)
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
		if (!gl::HasGLContext())
			return;
		if (m_InstanceVbo != 0)
		{
			glDeleteBuffers(1, &m_InstanceVbo);
			m_InstanceVbo = 0;
		}
		if (m_InstanceSsbo != 0)
		{
			glDeleteBuffers(1, &m_InstanceSsbo);
			m_InstanceSsbo = 0;
		}
	}

	void InstancedMeshStreamer::DrawInstanced(const std::shared_ptr<Shader> &shader,
		const std::shared_ptr<Mesh> &mesh, int subMesh,
		const std::vector<Matrix4> &matrices)
	{
		if (matrices.empty() || mesh == nullptr || shader == nullptr)
			return;

		shader->BindMesh(mesh);
		if (subMesh >= 0)
			shader->BindSubMesh(mesh, static_cast<unsigned int>(subMesh));

		const GLsizeiptr byteSize = static_cast<GLsizeiptr>(matrices.size() * sizeof(Matrix4));
		const GLsizei instanceCount = static_cast<GLsizei>(matrices.size());

		if (UseSSBO())
		{
			if (m_InstanceSsbo == 0)
				glGenBuffers(1, &m_InstanceSsbo);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_InstanceSsbo);
			glBufferData(GL_SHADER_STORAGE_BUFFER, byteSize, matrices.data(), GL_STREAM_DRAW);
			// binding point 2 matches layout(std430, binding = 2) in the shaders
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_InstanceSsbo);

			GLsizei count = subMesh >= 0
				? static_cast<GLsizei>(mesh->GetSubMeshAt(subMesh)->Indices.Data.size())
				: static_cast<GLsizei>(mesh->Indices.Data.size());
			glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, instanceCount);

			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		}
		else
		{
			if (m_InstanceVbo == 0)
				glGenBuffers(1, &m_InstanceVbo);
			glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVbo);
			glBufferData(GL_ARRAY_BUFFER, byteSize, matrices.data(), GL_STREAM_DRAW);

			int locs[4];
			for (int r = 0; r < 4; ++r)
				locs[r] = glGetAttribLocation(shader->GetProgram(), kInstanceAttrNames[r]);

			if (locs[0] == -1)
			{
				// Not an INSTANCED shader -- refuse to draw garbage.
				FURYW << "DrawInstanced: shader '" << shader->GetName() << "' has no instance_row0 attribute!";
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				return;
			}

			for (int r = 0; r < 4; ++r)
			{
				if (locs[r] == -1)
					continue;
				glEnableVertexAttribArray(locs[r]);
				glVertexAttribPointer(locs[r], 4, GL_FLOAT, GL_FALSE, sizeof(Matrix4),
					reinterpret_cast<const void*>(static_cast<size_t>(r) * sizeof(float) * 4));
				glVertexAttribDivisor(locs[r], 1);
			}

			GLsizei count = subMesh >= 0
				? static_cast<GLsizei>(mesh->GetSubMeshAt(subMesh)->Indices.Data.size())
				: static_cast<GLsizei>(mesh->Indices.Data.size());
			glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, instanceCount);

			// Reset divisor state on the shared mesh VAO so later
			// non-instanced draws of this mesh are unaffected.
			for (int r = 0; r < 4; ++r)
			{
				if (locs[r] == -1)
					continue;
				glVertexAttribDivisor(locs[r], 0);
				glDisableVertexAttribArray(locs[r]);
			}
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		RenderUtil::Instance()->IncreaseDrawCall();
		GLsizei triCount = subMesh >= 0
			? static_cast<GLsizei>(mesh->GetSubMeshAt(subMesh)->Indices.Data.size())
			: static_cast<GLsizei>(mesh->Indices.Data.size());
		RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(triCount) * instanceCount);
	}
}
