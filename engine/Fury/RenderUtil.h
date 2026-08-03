#ifndef _FURY_RENDER_UTIL_H_
#define _FURY_RENDER_UTIL_H_

#include <vector>

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/System/Clock.hpp>

#include "Fury/ArrayBuffers.h"
#include "Fury/Color.h"
#include "Fury/Singleton.h"
#include "Fury/Signal.h"
#include "Fury/EnumUtil.h"
#include "Fury/Pass.h"
#include "Fury/Matrix4.h"

namespace fury
{
	class Mesh;

	class BoxBounds;

	class Frustum;

	class SceneNode;

	class Shader;

	class Texture;

	class FURY_API RenderUtil final : public Singleton <RenderUtil>
	{
	public:

		typedef std::shared_ptr<RenderUtil> Ptr;

	private:

		std::shared_ptr<Shader> m_DebugShader;

		std::shared_ptr<Shader> m_BlitShader;

		std::shared_ptr<Pass> m_BlitPass;

		unsigned int m_LineVAO = 0;

		unsigned int m_LineVBO = 0;

		unsigned int m_DrawCall = 0;

		unsigned int m_MeshCount = 0;

		unsigned int m_TriangleCount = 0;

		unsigned int m_SkinnedMeshCount = 0;

		unsigned int m_LightCount = 0;

		// Snapshot of the last fully-completed frame's counters. Public getters
		// return these so the GUI can read stable values regardless of when in
		// the frame it queries.
		unsigned int m_LastDrawCall = 0;

		unsigned int m_LastMeshCount = 0;

		unsigned int m_LastTriangleCount = 0;

		unsigned int m_LastSkinnedMeshCount = 0;

		unsigned int m_LastLightCount = 0;

		sf::Clock m_FrameClock;

		bool m_DrawingLine = false;

		bool m_DrawingMesh = false;

	public:

		Signal<>::Ptr OnBeginFrame = Signal<>::Create();

		// frame time in ms
		Signal<int>::Ptr OnEndFrame = Signal<int>::Create();

		RenderUtil();

		virtual ~RenderUtil();

		void Blit(const std::shared_ptr<Texture> &src, const std::shared_ptr<Texture> &dest, 
			ClearMode clearMode = ClearMode::COLOR_DEPTH_STENCIL, 
			BlendMode blendMode = BlendMode::REPLACE);

		void Blit(const std::shared_ptr<Texture> &src, const std::shared_ptr<Texture> &dest, 
			const std::shared_ptr<Shader> &shader, ClearMode clearMode = ClearMode::COLOR_DEPTH_STENCIL, 
			BlendMode blendMode = BlendMode::REPLACE);

		void BeginDrawLines(const std::shared_ptr<SceneNode> &camera);

		void DrawLines(const float* positions, unsigned int size, Color color, LineMode lineMode = LineMode::LINES);

		void DrawBoxBounds(const BoxBounds &aabb, Color color);

		void DrawFrustum(const Frustum &frustum, Color color);

		void EndDrawLines();

		void BeginDrawMeshs(const std::shared_ptr<SceneNode> &camera);

		void DrawMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 &worldMatrix, Color color);

		void EndDrawMeshes();

		void BeginFrame();

		void EndFrame();

		void IncreaseDrawCall(unsigned int count = 1);

		unsigned int GetDrawCall();

		void IncreaseMeshCount(unsigned int count = 1);

		unsigned int GetMeshCount();

		void IncreaseTriangleCount(unsigned int count = 1);

		unsigned int GetTriangleCount();

		void IncreaseSkinnedMeshCount(unsigned int count = 1);

		unsigned int GetSkinnedMeshCount();

		void IncreaseLightCount(unsigned int count = 1);

		unsigned int GetLightCount();
	};

	// Cached simple-Lambert shader shared by the mesh thumbnail, mesh editor
	// preview, and `fury render-mesh` CLI.
	std::shared_ptr<Shader> GetSimpleLambertShader();

	// Cached particle billboard shader. v1 samples the bound diffuse
	// texture (Material::DIFFUSE_TEXTURE) and multiplies by u_Tint.
	// ParticleRenderer::Draw binds this — see the particle-system spec.
	std::shared_ptr<Shader> GetParticleShader();

	// 1×1 fallback textures for shadow samplers. Core GL rejects a draw
	// when ANY declared sampler's texture target mismatches (an unbound
	// samplerCube defaults to unit 0's 2D diffuse → glDrawElements
	// fails with GL_INVALID_OPERATION), so every shader that declares
	// shadow_buffer/shadow_map must have them bound to a valid-target
	// texture even when shadow sampling is off.
	std::shared_ptr<Texture> GetDummyCubeTexture();
	std::shared_ptr<Texture> GetDummyTexture2D();

	// Renders `mesh` into the currently-bound FBO (caller owns FBO + viewport
	// + clear) with the simple-Lambert shader, using a fixed orbit camera
	// framed on the mesh's local AABB. Returns the bounding-sphere radius.
	float RenderMeshLambert(const std::shared_ptr<Mesh> &mesh, int w, int h);
}

#endif // _FURY_RENDER_UTIL_H_