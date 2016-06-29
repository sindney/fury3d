#ifndef _FURY_RENDER_UTIL_H_
#define _FURY_RENDER_UTIL_H_

#include <vector>

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/System/Clock.hpp>

#include "ArrayBuffers.h"
#include "Color.h"
#include "Singleton.h"
#include "Signal.h"
#include "EnumUtil.h"
#include "Pass.h"
#include "Matrix4.h"

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

		std::shared_ptr<Shader> m_BlurShader;

		std::shared_ptr<Pass> m_BlitPass;

		unsigned int m_LineVAO = 0;

		unsigned int m_LineVBO = 0;

		unsigned int m_DrawCall = 0;

		unsigned int m_MeshCount = 0;

		unsigned int m_TriangleCount = 0;

		unsigned int m_SkinnedMeshCount = 0;

		unsigned int m_LightCount = 0;

		sf::Clock m_FrameClock;

		bool m_DrawingLine = false;

		bool m_DrawingMesh = false;

	public:

		Signal<> OnBeginFrame;

		// frame time in ms
		Signal<int> OnEndFrame;

		RenderUtil();

		virtual ~RenderUtil();

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
}

#endif // _FURY_RENDER_UTIL_H_