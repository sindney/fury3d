#include "Fury/GameUIRenderer.h"

#include <cstring>
#include <algorithm>

#include "stb_image.h"

#include "Fury/AssetBackend.h"
#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/RenderThread.h"
#include "Fury/Scene.h"
#include "Fury/Texture.h"

namespace
{
	const char *kUIVert = R"(#version 330 core
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;
uniform vec2 uTranslate; // batch translation, pixels
uniform vec2 uViewport;  // context size, pixels
out vec4 vColor;
out vec2 vUV;
void main()
{
	vColor = inColor;
	vUV = inUV;
	// RmlUi emits y-down pixel coordinates; RenderGeometry's translation
	// is a pixel offset (see RmlUi_Renderer_GL3.cpp). Map to clip space
	// with the ortho flip: y=0 is the top of the window.
	vec2 p = inPos + uTranslate;
	vec2 clip = vec2(p.x / uViewport.x * 2.0 - 1.0, 1.0 - p.y / uViewport.y * 2.0);
	gl_Position = vec4(clip, 0.0, 1.0);
}
)";

	const char *kUIFrag = R"(#version 330 core
in vec4 vColor;
in vec2 vUV;
uniform sampler2D uTex;
uniform int uUseTexture;
out vec4 fragColor;
void main()
{
	vec4 t = (uUseTexture != 0) ? texture(uTex, vUV) : vec4(1.0);
	fragColor = vColor * t;
}
)";

	struct UIReplayGL
	{
		GLuint program = 0;
		GLint uTex = -1, uUseTexture = -1, uTranslate = -1, uViewport = -1;
		GLuint vao = 0, vbo = 0, ebo = 0;
		GLuint whiteTex = 0;
	};

	UIReplayGL &ReplayGL()
	{
		static UIReplayGL s;
		return s;
	}

	GLuint CompileUIShader(GLenum type, const char *src)
	{
		GLuint sh = glCreateShader(type);
		glShaderSource(sh, 1, &src, nullptr);
		glCompileShader(sh);
		GLint ok = GL_FALSE;
		glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
		if (ok != GL_TRUE)
		{
			char log[1024];
			glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
			FURYW << "GameUI shader compile failed: " << log;
			glDeleteShader(sh);
			return 0;
		}
		return sh;
	}

	bool EnsureReplayGL()
	{
		UIReplayGL &s = ReplayGL();
		if (s.program != 0) return true;
		GLuint vs = CompileUIShader(GL_VERTEX_SHADER, kUIVert);
		GLuint fs = CompileUIShader(GL_FRAGMENT_SHADER, kUIFrag);
		if (vs == 0 || fs == 0) return false;
		s.program = glCreateProgram();
		glAttachShader(s.program, vs);
		glAttachShader(s.program, fs);
		glLinkProgram(s.program);
		glDeleteShader(vs);
		glDeleteShader(fs);
		GLint ok = GL_FALSE;
		glGetProgramiv(s.program, GL_LINK_STATUS, &ok);
		if (ok != GL_TRUE)
		{
			char log[1024];
			glGetProgramInfoLog(s.program, sizeof(log), nullptr, log);
			FURYW << "GameUI shader link failed: " << log;
			glDeleteProgram(s.program);
			s.program = 0;
			return false;
		}
		s.uTex = glGetUniformLocation(s.program, "uTex");
		s.uUseTexture = glGetUniformLocation(s.program, "uUseTexture");
		s.uTranslate = glGetUniformLocation(s.program, "uTranslate");
		s.uViewport = glGetUniformLocation(s.program, "uViewport");

		glGenVertexArrays(1, &s.vao);
		glGenBuffers(1, &s.vbo);
		glGenBuffers(1, &s.ebo);
		glBindVertexArray(s.vao);
		glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), (void *)offsetof(Rml::Vertex, position));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), (void *)offsetof(Rml::Vertex, colour));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), (void *)offsetof(Rml::Vertex, tex_coord));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// 1x1 white keeps sampler 0 bound on untextured batches (core GL
		// kills draws when a sampler reads an incompatible/no texture).
		const unsigned char white[4] = {255, 255, 255, 255};
		glGenTextures(1, &s.whiteTex);
		glBindTexture(GL_TEXTURE_2D, s.whiteTex);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
		return true;
	}

	// Shared upload: engine Texture around RGBA8 pixels, created/uploaded
	// on the GL thread (EnqueueJob when called from the game thread; a
	// no-op queue in headless runs, per the metadata-only contract).
	std::shared_ptr<fury::Texture> MakeUITexture(const std::string &name, const unsigned char *rgba, int w, int h)
	{
		auto tex = fury::Texture::Create(name);
		tex->SetWrapMode(fury::WrapMode::CLAMP_TO_EDGE);
		std::vector<unsigned char> pixels(rgba, rgba + static_cast<size_t>(w) * h * 4);
		fury::DispatchGL(tex.get(), [tex, pixels = std::move(pixels), w, h]() mutable
		{
			tex->CreateEmpty(w, h, 1, fury::TextureFormat::RGBA8, fury::TextureType::TEXTURE_2D, false);
			tex->SetPixels(pixels.data());
		});
		return tex;
	}
}

namespace fury
{
	void GameUIRenderer::BeginBuild(const Rml::Vector2i &dimensions)
	{
		m_Build = std::make_shared<GameUIFrameData>();
		m_Build->dimensions = dimensions;
	}

	std::shared_ptr<GameUIFrameData> GameUIRenderer::EndBuild()
	{
		auto out = std::move(m_Build);
		m_Build.reset();
		if (out != nullptr && out->batches.empty())
			return nullptr;
		return out;
	}

	void GameUIRenderer::Replay(const std::shared_ptr<GameUIFrameData> &frame)
	{
		if (frame == nullptr || frame->batches.empty()) return;
		FURY_GL_THREAD_GUARD();
		if (!EnsureReplayGL()) return;
		// Drain sticky errors from earlier passes so the per-batch check
		// below only reports UI-replay errors.
		const GLenum inheritedErr = glGetError();
		if (inheritedErr != GL_NO_ERROR)
			FURYW << "UIReplay inherited GL error 0x" << std::hex << inheritedErr << std::dec;
		UIReplayGL &s = ReplayGL();

		// Back up every state we touch (the ImGui renderer runs after us).
		GLint prevProgram = 0, prevVao = 0, prevArrayBuffer = 0, prevElementBuffer = 0, prevTexture = 0;
		GLint prevScissorBox[4] = {0, 0, 0, 0};
		GLint prevViewport[4] = {0, 0, 0, 0};
		GLint prevBlendSrc = 0, prevBlendDst = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementBuffer);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
		glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
		glGetIntegerv(GL_VIEWPORT, prevViewport);
		glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
		glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
		const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
		const GLboolean prevBlend = glIsEnabled(GL_BLEND);
		const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);

		// RmlUi emits y-down pixel coordinates; the vertex shader maps
		// them to clip space with an ortho flip. The postfx chain may
		// have left an arbitrary viewport; UI draws to the full window.
		glViewport(0, 0, frame->dimensions.x, frame->dimensions.y);

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // RmlUi emits premultiplied alpha
		glUseProgram(s.program);
		glUniform1i(s.uTex, 0);
		glUniform2f(s.uViewport, static_cast<float>(frame->dimensions.x),
			static_cast<float>(frame->dimensions.y));
		glActiveTexture(GL_TEXTURE0);
		glBindVertexArray(s.vao);

		for (const auto &batch : frame->batches)
		{
			const auto &geom = batch.geometry;
			if (geom == nullptr || geom->indices.empty()) continue;

			if (batch.scissorEnabled)
			{
				// RmlUi scissor is y-down; GL is y-up (see
				// RmlUi_Renderer_GL3.cpp SetScissor).
				const int sx = std::max(0, batch.scissor.Left());
				const int sy = std::max(0, frame->dimensions.y - batch.scissor.Bottom());
				const int sw = batch.scissor.Right() - batch.scissor.Left();
				const int sh = batch.scissor.Bottom() - batch.scissor.Top();
				glEnable(GL_SCISSOR_TEST);
				glScissor(sx, sy, std::max(0, sw), std::max(0, sh));
			}
			else
			{
				glDisable(GL_SCISSOR_TEST);
			}

			glUniform2f(s.uTranslate, batch.translation.x, batch.translation.y);

			glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
			glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geom->vertices.size() * sizeof(Rml::Vertex)),
				geom->vertices.data(), GL_STREAM_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(geom->indices.size() * sizeof(int)),
				geom->indices.data(), GL_STREAM_DRAW);

			GLuint texId = s.whiteTex;
			GLint useTexture = 0;
			if (batch.texture != nullptr)
			{
				const unsigned int id = batch.texture->GetID();
				if (id != 0)
				{
					texId = id;
					useTexture = 1;
				}
				// When id == 0 the upload is still queued on the render
				// thread (GenerateTexture ran during BuildDrawSnapshot on
				// the game thread, before the render-thread pass for this
				// frame). Skip the batch -- the next frame will catch the
				// upload + the glyphs in one replay.
				else
				{
					continue;
				}
			}
			glBindTexture(GL_TEXTURE_2D, texId);
			glUniform1i(s.uUseTexture, useTexture);

			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(geom->indices.size()), GL_UNSIGNED_INT, nullptr);
			const GLenum uiErr = glGetError();
			if (uiErr != GL_NO_ERROR)
				FURYW << "UIReplay draw error 0x" << std::hex << uiErr << std::dec
					<< " batch verts=" << geom->vertices.size();
		}

		glBindVertexArray(prevVao);
		glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prevElementBuffer);
		glBindTexture(GL_TEXTURE_2D, prevTexture);
		glUseProgram(prevProgram);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
		glBlendFunc(prevBlendSrc, prevBlendDst);
		if (prevBlend != GL_TRUE) glDisable(GL_BLEND);
		if (prevDepth == GL_TRUE) glEnable(GL_DEPTH_TEST);
		if (prevScissor == GL_TRUE)
		{
			glEnable(GL_SCISSOR_TEST);
			glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
		}
		else
		{
			glDisable(GL_SCISSOR_TEST);
		}
	}

	Rml::CompiledGeometryHandle GameUIRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
	{
		auto geom = std::make_shared<GameUIFrameData::Geometry>();
		geom->vertices.assign(vertices.begin(), vertices.end());
		geom->indices.assign(indices.begin(), indices.end());
		m_GeometryStore.push_back(std::move(geom));
		return static_cast<Rml::CompiledGeometryHandle>(m_GeometryStore.size()); // 1-based
	}

	void GameUIRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
	{
		if (m_Build == nullptr || geometry == 0 || geometry > m_GeometryStore.size()) return;
		const auto &geom = m_GeometryStore[geometry - 1];
		if (geom == nullptr) return;
		GameUIFrameData::Batch batch;
		batch.geometry = geom;
		batch.translation = translation;
		if (texture != 0 && texture <= m_Textures.size())
			batch.texture = m_Textures[texture - 1];
		batch.scissor = m_Scissor;
		batch.scissorEnabled = m_ScissorEnabled;
		m_Build->batches.push_back(std::move(batch));
	}

	void GameUIRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
	{
		if (geometry == 0 || geometry > m_GeometryStore.size()) return;
		m_GeometryStore[geometry - 1].reset();
	}

	Rml::TextureHandle GameUIRenderer::LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source)
	{
		std::vector<unsigned char> bytes;
		if (!AssetBackend::ReadAssetBytes(Scene::ResolveAsset(source), bytes) &&
			!AssetBackend::ReadAssetBytes(source, bytes))
		{
			FURYW << "GameUI: texture not found '" << source << "'";
			return 0;
		}
		int w = 0, h = 0, channels = 0;
		stbi_uc *decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
		if (decoded == nullptr)
		{
			FURYW << "GameUI: texture decode failed '" << source << "'";
			return 0;
		}
		// RmlUi blends premultiplied (GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
		for (size_t i = 0, n = static_cast<size_t>(w) * h * 4; i < n; i += 4)
		{
			const unsigned char a = decoded[i + 3];
			decoded[i + 0] = static_cast<unsigned char>((decoded[i + 0] * a + 127) / 255);
			decoded[i + 1] = static_cast<unsigned char>((decoded[i + 1] * a + 127) / 255);
			decoded[i + 2] = static_cast<unsigned char>((decoded[i + 2] * a + 127) / 255);
		}
		auto tex = MakeUITexture("ui/" + source, decoded, w, h);
		stbi_image_free(decoded);
		texture_dimensions = Rml::Vector2i(w, h);
		m_Textures.push_back(std::move(tex));
		return static_cast<Rml::TextureHandle>(m_Textures.size());
	}

	Rml::TextureHandle GameUIRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
	{
		if (source_dimensions.x <= 0 || source_dimensions.y <= 0 ||
			source.size() != static_cast<size_t>(source_dimensions.x * source_dimensions.y * 4))
		{
			return 0;
		}
		auto tex = MakeUITexture("ui/atlas" + std::to_string(m_Textures.size()),
			reinterpret_cast<const unsigned char *>(source.data()), source_dimensions.x, source_dimensions.y);
		m_Textures.push_back(std::move(tex));
		return static_cast<Rml::TextureHandle>(m_Textures.size());
	}

	void GameUIRenderer::ReleaseTexture(Rml::TextureHandle texture)
	{
		if (texture == 0 || texture > m_Textures.size()) return;
		auto &slot = m_Textures[texture - 1];
		if (slot == nullptr) return;
		auto tex = std::move(slot);
		slot.reset();
		// Texture destruction deletes the GL object; run it on the GL thread.
		if (RenderThread::Get().MayUseGL())
			tex.reset();
		else
			RenderThread::Get().EnqueueJob([tex = std::move(tex)]() mutable { tex.reset(); });
	}

	void GameUIRenderer::EnableScissorRegion(bool enable)
	{
		m_ScissorEnabled = enable;
	}

	void GameUIRenderer::SetScissorRegion(Rml::Rectanglei region)
	{
		m_Scissor = region;
	}
}
