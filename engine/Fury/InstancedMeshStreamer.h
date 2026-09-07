#ifndef _FURY_INSTANCED_MESH_STREAMER_H_
#define _FURY_INSTANCED_MESH_STREAMER_H_

#include <memory>
#include <vector>

#include "Fury/Matrix4.h"

namespace fury
{
	class Mesh;

	class Shader;

	// Streams per-instance world matrices to the GPU and issues instanced
	// draws. Two paths selected by runtime GL capability:
	//
	//   * SSBO (GL 4.3+, preferred): matrices in a shader storage buffer
	//     read by gl_InstanceID (INSTANCE_SSBO define).
	//   * Divisor VBO (GL 3.3/4.1 fallback): matrices as four vec4
	//     attributes (instance_row0..3) with glVertexAttribDivisor(1).
	//
	// The divisor attributes are set on the mesh's VAO around the draw
	// and torn down right after, so the shared VAO is never left with
	// divisor state that would corrupt later non-instanced draws.
	//
	// FURY_INSTANCE_SSBO=0/1 overrides the automatic path selection
	// (used to A/B the paths in validation).
	class FURY_API InstancedMeshStreamer
	{
	public:

		static InstancedMeshStreamer &Get();

		// True when the context supports shader storage buffers (GL 4.3+).
		bool SSBOAvailable() const;

		// True when draws use the SSBO path (capability && env override).
		bool UseSSBO() const;

		// Upload `matrices` and draw `mesh` (whole mesh when subMesh < 0)
		// instanced. The shader must be an INSTANCED variant (and an
		// INSTANCE_SSBO variant when UseSSBO()). Caller has already bound
		// the shader and per-material state.
		void DrawInstanced(const std::shared_ptr<Shader> &shader,
			const std::shared_ptr<Mesh> &mesh, int subMesh,
			const std::vector<Matrix4> &matrices);

		// Delete GL buffers. Called from Engine::Shutdown while the
		// context is still alive.
		void DeleteBuffers();

	private:

		unsigned int m_InstanceVbo = 0;

		unsigned int m_InstanceSsbo = 0;

		// -1 = not yet probed
		mutable int m_SsboAvailable = -1;
	};
}

#endif // _FURY_INSTANCED_MESH_STREAMER_H_
