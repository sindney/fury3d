#include "Fury/GameUIRenderer.h"

#include <cstring>
#include <algorithm>

#include "stb_image.h"

#include "Fury/AssetBackend.h"
#include "Fury/Log.h"
#include "Fury/RenderThread.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/Texture.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/OpenGL/GLInputAssembler.h"

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

	// MSL twin of the GLSL pair above. vertex_id + buffer(0) stream instead
	// of stage_in attributes (descriptor-free), so uv is scalar floats to
	// match the Rml::Vertex packing (float2 pos, byte4 color, float2 uv = 20).
	const char *kUIVertMSL = R"(#include <metal_stdlib>
using namespace metal;

struct UIVertex {
	float px;
	float py;
	uchar4 color;
	float u;
	float v;
};

struct Varyings {
	float4 position [[position]];
	float4 color;
	float2 uv;
};

vertex Varyings vertexMain(constant UIVertex* vertices [[buffer(0)]],
	constant float2& uViewport [[buffer(8)]],
	constant float2& uTranslate [[buffer(9)]],
	uint vid [[vertex_id]])
{
	constant UIVertex& vtx = vertices[vid];
	Varyings out;
	float2 p = float2(vtx.px, vtx.py) + uTranslate;
	float2 clip = float2(p.x / uViewport.x * 2.0 - 1.0, 1.0 - p.y / uViewport.y * 2.0);
	out.position = float4(clip, 0.0, 1.0);
	out.color = float4(vtx.color) / 255.0;
	out.uv = float2(vtx.u, vtx.v);
	return out;
}
)";

	// Varyings is defined in the vertex blob (the sources concatenate).
	const char *kUIFragMSL = R"(#include <metal_stdlib>
using namespace metal;

fragment float4 fragmentMain(Varyings frag [[stage_in]],
	texture2d<float> uTex [[texture(0)]],
	sampler uTexSampler [[sampler(0)]],
	constant int& uUseTexture [[buffer(8)]])
{
	float4 t = (uUseTexture != 0) ? uTex.sample(uTexSampler, frag.uv) : float4(1.0);
	return frag.color * t;
}
)";

	struct UIReplayRHI
	{
		fury::RHIShaderModuleRef module;

		fury::RHIBufferRef vbo;

		fury::RHIBufferRef ebo;

		unsigned int vao = 0;
	};

	UIReplayRHI &ReplayState()
	{
		static UIReplayRHI s;
		return s;
	}

	fury::RHIShaderBlob SourceBlob(const char *source)
	{
		fury::RHIShaderBlob blob;
		blob.Data.assign(source, source + std::strlen(source));
		blob.IsSource = true;
		return blob;
	}

	bool EnsureReplay(UIReplayRHI &s)
	{
		if (s.module != nullptr)
			return true;
		if (fury::GRHIDevice == nullptr)
			return false;

		s.module = fury::GRHIDevice->CreateShaderModule("GameUI",
			SourceBlob(fury::GRHIDevice->GetBackend() == fury::RHIBackend::METAL ? kUIVertMSL : kUIVert),
			SourceBlob(fury::GRHIDevice->GetBackend() == fury::RHIBackend::METAL ? kUIFragMSL : kUIFrag),
			fury::RHIShaderBlob());
		if (s.module == nullptr || !s.module->IsValid())
		{
			FURYW << "GameUI shader module failed";
			s.module = nullptr;
			return false;
		}

		fury::RHIBufferDesc vboDesc;
		vboDesc.Name = "GameUI/vbo";
		vboDesc.Type = fury::RHIBufferType::VERTEX;
		vboDesc.Usage = fury::RHIBufferUsage::DYNAMIC;
		vboDesc.Size = 0;
		s.vbo = fury::GRHIDevice->CreateBuffer(vboDesc);

		fury::RHIBufferDesc eboDesc;
		eboDesc.Name = "GameUI/ebo";
		eboDesc.Type = fury::RHIBufferType::INDEX;
		eboDesc.Usage = fury::RHIBufferUsage::DYNAMIC;
		eboDesc.Size = 0;
		s.ebo = fury::GRHIDevice->CreateBuffer(eboDesc);

		if (s.vbo == nullptr || s.ebo == nullptr)
		{
			FURYW << "GameUI stream buffers failed";
			s.module = nullptr;
			s.vbo = nullptr;
			s.ebo = nullptr;
			return false;
		}
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
		fury::DispatchRHI(tex.get(), [tex, pixels = std::move(pixels), w, h]() mutable
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
		FURY_RHI_THREAD_GUARD();
		if (GRHIDevice == nullptr) return;
		auto &s = ReplayState();
		if (!EnsureReplay(s)) return;
		auto *cmd = GRHIDevice->GetCommandList();

		// RmlUi emits y-down pixel coordinates; the vertex shader maps
		// them to clip space with an ortho flip. The postfx chain may
		// have left an arbitrary viewport; UI draws to the full window.
		// The previous target + viewport return via PopRenderTarget.
		cmd->PushRenderTarget();
		cmd->SetViewport(0, 0, frame->dimensions.x, frame->dimensions.y);
		cmd->SetRasterState(BlendMode::PREMULALPHA, CompareMode::LESS, false, false, CullMode::NONE);
		cmd->SetPrimitiveMode(RHIPrimitiveMode::TRIANGLES);
		cmd->BindShaderModule(s.module);
		cmd->SetUniformFloat("uViewport", static_cast<float>(frame->dimensions.x),
			static_cast<float>(frame->dimensions.y));

		// The world pass unbinds VAO 0 after every mesh draw, so the UI VAO
		// must be re-bound every replay (GLBindInterleavedGeometry only gens
		// it once).
		const GLInterleavedAttrib attribs[3] = {
			{ 0, 2, false, (unsigned int)offsetof(Rml::Vertex, position) },
			{ 1, 4, true, (unsigned int)offsetof(Rml::Vertex, colour) },
			{ 2, 2, false, (unsigned int)offsetof(Rml::Vertex, tex_coord) },
		};
		GLBindInterleavedGeometry(s.vao, (unsigned int)s.vbo->GetNativeHandle(),
			(unsigned int)s.ebo->GetNativeHandle(), attribs, 3, sizeof(Rml::Vertex));

		// Stream binds for backends that don't keep geometry in VAO state
		// (GL no-ops; Metal binds the buffers per draw).
		cmd->SetVertexBuffer(s.vbo);
		cmd->SetIndexBuffer(s.ebo);

		for (const auto &batch : frame->batches)
		{
			{
			const auto &geom = batch.geometry;
			if (geom == nullptr || geom->indices.empty()) continue;

			if (batch.scissorEnabled)
			{
				// RmlUi scissor is y-down; the RHI scissor convention is
				// y-up (see RmlUi_Renderer_GL3.cpp SetScissor).
				const int sx = std::max(0, batch.scissor.Left());
				const int sy = std::max(0, frame->dimensions.y - batch.scissor.Bottom());
				const int sw = batch.scissor.Right() - batch.scissor.Left();
				const int sh = batch.scissor.Bottom() - batch.scissor.Top();
				cmd->SetScissor(true, sx, sy, std::max(0, sw), std::max(0, sh));
			}
			else
			{
				cmd->SetScissor(false, 0, 0, 0, 0);
			}

			cmd->SetUniformFloat("uTranslate", batch.translation.x, batch.translation.y);

			GRHIDevice->UpdateBuffer(s.vbo, geom->vertices.data(),
				geom->vertices.size() * sizeof(Rml::Vertex));
			GRHIDevice->UpdateBuffer(s.ebo, geom->indices.data(),
				geom->indices.size() * sizeof(int));

			RHITextureRef rhiTex = GetDummyTexture2D()->GetRHITexture();
			int useTexture = 0;
			if (batch.texture != nullptr)
			{
				// When the RHI texture is still queued on the render thread
				// (GenerateTexture ran during BuildDrawSnapshot on the game
				// thread, before this frame's pass) skip the batch -- the
				// next frame catches the upload + the glyphs in one replay.
				if (batch.texture->GetRHITexture() == nullptr)
					continue;
				rhiTex = batch.texture->GetRHITexture();
				useTexture = 1;

			}
			// A 2D texture stays bound even when unsampled (core GL kills
			// draws when a sampler reads an incompatible/no texture).
			cmd->SetTextureUnitCursor(0);
			cmd->SetTexture("uTex", rhiTex);
			cmd->SetUniformInt("uUseTexture", useTexture);


			cmd->DrawIndexed(static_cast<unsigned int>(geom->indices.size()));
			}
		}
		cmd->SetScissor(false, 0, 0, 0, 0);
		cmd->PopRenderTarget();
		cmd->SetRasterState(BlendMode::REPLACE, CompareMode::LESS, true, true, CullMode::NONE);
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
