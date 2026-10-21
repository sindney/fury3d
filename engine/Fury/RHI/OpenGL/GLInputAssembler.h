#ifndef _FURY_GL_INPUT_ASSEMBLER_H_
#define _FURY_GL_INPUT_ASSEMBLER_H_

#include "Fury/Macros.h"
#include "Fury/RHI/RHITypes.h"

namespace fury
{
	// One mesh vertex channel as the GL backend needs it. Attrib is the
	// program-resolved attribute index (-1 = skip). BufferID 0 or Dirty
	// means the attribute falls back to disabled (constant); Empty channels
	// disable silently, dirty ones warn.
	struct GLMeshChannelDesc
	{
		const char *Name = "";

		int Attrib = -1;

		unsigned int BufferID = 0;

		bool Dirty = false;

		bool Empty = true;

		int Components = 3;

		bool Integer = false;
	};

	struct GLMeshGeometryDesc
	{
		static const int MAX_CHANNELS = 8;

		GLMeshChannelDesc Channels[MAX_CHANNELS];

		int ChannelCount = 0;

		const char *MeshName = "";
	};

	// One attribute of an interleaved vertex stream (UI-style layouts the
	// mesh channel desc can't express: offsets, stride, normalized bytes).
	struct GLInterleavedAttrib
	{
		int Attrib = -1;

		int Components = 0;

		// GL_UNSIGNED_BYTE normalized vs GL_FLOAT.
		bool ByteNormalized = false;

		unsigned int Offset = 0;
	};

	// Binds (creating when needed) the mesh VAO and configures attributes.
	// Render thread only.
	FURY_API void GLBindMeshGeometry(unsigned int &vao, const RHIMeshGeometryDesc &desc);

	// Binds (creating when needed) a VAO for an interleaved vertex stream:
	// vbo is captured into each attribute, ebo as the index source.
	FURY_API void GLBindInterleavedGeometry(unsigned int &vao, unsigned int vbo, unsigned int ebo,
		const GLInterleavedAttrib *attribs, int attribCount, unsigned int stride);

	FURY_API void GLDeleteVertexArray(unsigned int &vao);

	// By-value variant for queued destruction (the owner may be gone).
	FURY_API void GLDestroyVertexArray(unsigned int vao);

	// State hygiene after a mesh draw (Shader::UnBind).
	FURY_API void GLUnbindGeometry();

	// Points the module's per-instance matrix-row attributes (4 float4
	// rows named attrNames[0..3]) at the buffer with divisor 1 on the
	// currently bound VAO. Returns false when the shader has no
	// attrNames[0] (not an INSTANCED variant).
	FURY_API bool GLBindInstanceMatrixAttribs(const RHIShaderModuleRef &module,
		const RHIBufferRef &buffer, const char *const attrNames[4]);

	// Restores divisor 0 and disables the rows (shared-VAO hygiene).
	FURY_API void GLUnbindInstanceMatrixAttribs(const RHIShaderModuleRef &module,
		const char *const attrNames[4]);
}

#endif // _FURY_GL_INPUT_ASSEMBLER_H_
