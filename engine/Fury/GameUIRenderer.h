#ifndef _FURY_GAME_UI_RENDERER_H_
#define _FURY_GAME_UI_RENDERER_H_

#include <memory>
#include <vector>

#include <RmlUi/Core/RenderInterface.h>

#include "Fury/Macros.h"

namespace fury
{
	class Texture;

	// One frame of recorded UI draw data: built on the game thread by
	// GameUIRenderer (via Context::Render), carried by the frame packet,
	// replayed on the GL thread. Mirrors the Gui.cpp snapshot pattern.
	struct GameUIFrameData
	{
		struct Geometry
		{
			std::vector<Rml::Vertex> vertices;
			std::vector<int> indices;
		};
		struct Batch
		{
			// Resolved at record time from the renderer's persistent
			// geometry store (RmlUi retains handles across frames).
			std::shared_ptr<Geometry> geometry;
			Rml::Vector2f translation {0, 0};
			std::shared_ptr<Texture> texture; // null: untextured (vertex color only)
			Rml::Rectanglei scissor;
			bool scissorEnabled = false;
		};
		std::vector<Batch> batches;
		Rml::Vector2i dimensions {0, 0}; // context size at build time
	};

	// RmlUi RenderInterface that only records: geometry spans are copied
	// into the frame under construction; textures become engine Texture
	// objects whose GL upload is dispatched through the render thread.
	// Never touches GL on the game thread.
	class FURY_API GameUIRenderer final : public Rml::RenderInterface
	{
	public:

		// Game-thread frame bracket around Context::Render().
		void BeginBuild(const Rml::Vector2i &dimensions);
		std::shared_ptr<GameUIFrameData> EndBuild();

		// GL-thread replay inside the frame executor.
		void Replay(const std::shared_ptr<GameUIFrameData> &frame);

		// RenderInterface (game thread only).
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
		void ReleaseTexture(Rml::TextureHandle texture) override;
		void EnableScissorRegion(bool enable) override;
		void SetScissorRegion(Rml::Rectanglei region) override;

	private:

		std::shared_ptr<GameUIFrameData> m_Build;
		Rml::Rectanglei m_Scissor;
		bool m_ScissorEnabled = false;
		// Live textures, handle = slot index + 1 (0 = invalid).
		std::vector<std::shared_ptr<Texture>> m_Textures;
		// Compiled geometry, retained across frames per the RenderInterface
		// contract (RmlUi compiles lazily once, re-renders every frame).
		// Handle = slot index + 1. Slots empty on ReleaseGeometry.
		std::vector<std::shared_ptr<GameUIFrameData::Geometry>> m_GeometryStore;
	};
}

#endif // _FURY_GAME_UI_RENDERER_H_
