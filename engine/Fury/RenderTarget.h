#ifndef _FURY_RENDER_TARGET_H_
#define _FURY_RENDER_TARGET_H_

#include <memory>

#include "Fury/Macros.h"

namespace fury
{
	class Texture;

	// A simple offscreen render target: one RGBA8 color texture + one
	// DEPTH24 depth texture attached to a dedicated FBO. Used by the
	// editor's dockable Viewport window to capture the 3D scene and
	// present it via ImGui::Image.
	//
	// Lifecycle: lazy-allocated on the first Resize() with a non-zero
	// size; recreated when the size changes. Call Release() to tear
	// down the GL objects. Not serialized -- owned by the editor at
	// runtime.
	class FURY_API RenderTarget
	{
	public:

		typedef std::shared_ptr<RenderTarget> Ptr;

		static Ptr Create(const std::string &name);

		RenderTarget(const std::string &name);

		~RenderTarget();

		// No copy -- owns GL resources.
		RenderTarget(const RenderTarget&) = delete;
		RenderTarget& operator=(const RenderTarget&) = delete;

		// Ensure the FBO + attachments exist at the given size. Returns
		// true when the FBO is usable (complete). A no-op (returns true)
		// when the size already matches. Returns false for zero/negative
		// sizes or incomplete FBO.
		bool Resize(int width, int height);

		// Tear down GL objects. Safe to call multiple times.
		void Release();

		unsigned int GetFBO() const { return m_FBO; }

		std::shared_ptr<Texture> GetColorTexture() const { return m_Color; }

		std::shared_ptr<Texture> GetDepthTexture() const { return m_Depth; }

		int GetWidth() const { return m_Width; }

		int GetHeight() const { return m_Height; }

		bool IsAllocated() const { return m_FBO != 0; }

	private:

		std::string m_Name;

		unsigned int m_FBO = 0;

		std::shared_ptr<Texture> m_Color;
		std::shared_ptr<Texture> m_Depth;

		int m_Width = 0;
		int m_Height = 0;
	};
}

#endif // _FURY_RENDER_TARGET_H_
