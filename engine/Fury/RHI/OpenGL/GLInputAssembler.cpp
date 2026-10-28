#include "Fury/RHI/OpenGL/GLInputAssembler.h"

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/RHI/RHIResources.h"

namespace fury
{
	void GLBindMeshGeometry(unsigned int &vao, const RHIMeshGeometryDesc &desc)
	{
		if (vao == 0)
		{
			glGenVertexArrays(1, &vao);
			if (vao == 0)
			{
				FURYW << "Failed to glGenVertexArrays!";
				return;
			}
		}

		glBindVertexArray(vao);

		for (int i = 0; i < desc.ChannelCount; i++)
		{
			const auto &channel = desc.Channels[i];
			if (channel.Attrib == -1)
				continue;

			if (!channel.Dirty && channel.Buffer != nullptr)
			{
				glBindBuffer(GL_ARRAY_BUFFER, (GLuint)channel.Buffer->GetNativeHandle());
				if (channel.Integer)
					glVertexAttribIPointer(channel.Attrib, channel.Components, GL_UNSIGNED_INT, 0, 0);
				else
					glVertexAttribPointer(channel.Attrib, channel.Components, GL_FLOAT, GL_FALSE, 0, 0);
				glEnableVertexAttribArray(channel.Attrib);
			}
			else if (channel.Empty)
			{
				glDisableVertexAttribArray(channel.Attrib);
			}
			else
			{
				FURYW << "Mesh " << desc.MeshName << " " << channel.Name << " data dirty!";
			}
		}

		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void GLBindInterleavedGeometry(unsigned int &vao, unsigned int vbo, unsigned int ebo,
		const GLInterleavedAttrib *attribs, int attribCount, unsigned int stride)
	{
		if (vao == 0)
		{
			glGenVertexArrays(1, &vao);
			if (vao == 0)
			{
				FURYW << "Failed to glGenVertexArrays!";
				return;
			}
		}

		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		for (int i = 0; i < attribCount; i++)
		{
			const auto &attrib = attribs[i];
			if (attrib.Attrib < 0)
				continue;
			glEnableVertexAttribArray(attrib.Attrib);
			if (attrib.ByteNormalized)
			{
				glVertexAttribPointer(attrib.Attrib, attrib.Components, GL_UNSIGNED_BYTE, GL_TRUE,
					stride, reinterpret_cast<const void*>(static_cast<size_t>(attrib.Offset)));
			}
			else
			{
				glVertexAttribPointer(attrib.Attrib, attrib.Components, GL_FLOAT, GL_FALSE,
					stride, reinterpret_cast<const void*>(static_cast<size_t>(attrib.Offset)));
			}
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void GLDeleteVertexArray(unsigned int &vao)
	{
		if (vao != 0)
		{
			glDeleteVertexArrays(1, &vao);
			vao = 0;
		}
	}

	void GLDestroyVertexArray(unsigned int vao)
	{
		if (vao != 0)
			glDeleteVertexArrays(1, &vao);
	}

	void GLUnbindGeometry()
	{
		glBindVertexArray(0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	bool GLBindInstanceMatrixAttribs(const RHIShaderModuleRef &module, const RHIBufferRef &buffer, const char *const attrNames[4])
	{
		if (module == nullptr || buffer == nullptr)
			return false;

		int locs[4];
		for (int r = 0; r < 4; ++r)
			locs[r] = module->ResolveAttribLocation(attrNames[r]);
		if (locs[0] == -1)
			return false;

		glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buffer->GetNativeHandle());
		for (int r = 0; r < 4; ++r)
		{
			if (locs[r] == -1)
				continue;
			glEnableVertexAttribArray(locs[r]);
			glVertexAttribPointer(locs[r], 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float),
				reinterpret_cast<const void*>(static_cast<size_t>(r) * 4 * sizeof(float)));
			glVertexAttribDivisor(locs[r], 1);
		}
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		return true;
	}

	void GLUnbindInstanceMatrixAttribs(const RHIShaderModuleRef &module, const char *const attrNames[4])
	{
		if (module == nullptr)
			return;
		for (int r = 0; r < 4; ++r)
		{
			const int loc = module->ResolveAttribLocation(attrNames[r]);
			if (loc == -1)
				continue;
			glVertexAttribDivisor(loc, 0);
			glDisableVertexAttribArray(loc);
		}
	}
}
