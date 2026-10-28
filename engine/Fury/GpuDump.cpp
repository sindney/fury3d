#include "Fury/GpuDump.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "Fury/Engine.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/FramePacket.h"
#include "Fury/GameUI.h"
#include "Fury/Log.h"
#include "Fury/Pass.h"
#include "Fury/Pipeline.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/RHI/RHIBackend.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"

#include "stb_image_write.h"

namespace fury
{
	namespace
	{
		std::string SanitizeName(const std::string &name)
		{
			std::string out = name.empty() ? "unnamed" : name;
			for (char &c : out)
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
					c = '_';
			return out;
		}

		// Readbacks arrive in GL bottom-up row order on every backend; PNG is
		// top-down.
		void FlipRows(std::vector<unsigned char> &pixels, int width, int height)
		{
			const size_t rowBytes = static_cast<size_t>(width) * 4;
			std::vector<unsigned char> tmp(rowBytes);
			for (int y = 0; y < height / 2; ++y)
			{
				unsigned char *a = pixels.data() + static_cast<size_t>(y) * rowBytes;
				unsigned char *b = pixels.data() + static_cast<size_t>(height - 1 - y) * rowBytes;
				std::memcpy(tmp.data(), a, rowBytes);
				std::memcpy(a, b, rowBytes);
				std::memcpy(b, tmp.data(), rowBytes);
			}
		}

		struct TexStats
		{
			float min = 0.0f;
			float max = 0.0f;
			float mean = 0.0f;
		};

		TexStats ComputeStats(const std::vector<unsigned char> &pixels)
		{
			TexStats s;
			if (pixels.empty())
				return s;
			float mn = 1e9f, mx = -1e9f;
			double sum = 0.0;
			for (size_t i = 0; i + 3 < pixels.size(); i += 4)
			{
				const float v = pixels[i] / 255.0f;
				mn = std::min(mn, v);
				mx = std::max(mx, v);
				sum += v;
			}
			s.min = mn;
			s.max = mx;
			s.mean = static_cast<float>(sum / (pixels.size() / 4));
			return s;
		}

		std::string JsonEscape(const std::string &s)
		{
			std::string out;
			out.reserve(s.size() + 8);
			for (char c : s)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				default: out += c; break;
				}
			}
			return out;
		}
	}

	bool GpuDump::Write(const std::string &outDir, const FramePacket &packet)
	{
		if (GRHIDevice == nullptr || Pipeline::Active == nullptr)
		{
			FURYE << "GpuDump: no device or active pipeline";
			return false;
		}
		const auto &pipeline = Pipeline::Active;

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(outDir) / "textures", ec);
		if (ec)
		{
			FURYE << "GpuDump: cannot create '" << outDir << "': " << ec.message();
			return false;
		}

		// Textures: readback + PNG per slice + stats. Covers the pipeline's
		// registered textures plus every texture used as a render target this
		// frame (shadow maps, sky LUTs, chain temps never register).
		std::vector<RHITextureRef> dumpTextures;
		std::unordered_map<std::string, RHITextureRef> namedTex;
		pipeline->ForEachTexture([&](const std::shared_ptr<Texture> &tex) -> bool {
			if (tex != nullptr && tex->GetRHITexture() != nullptr)
				namedTex[tex->GetName()] = tex->GetRHITexture();
			return true;
		});
		for (const auto &rhiTex : GRHIDevice->GetFrameRenderTargets())
		{
			if (rhiTex == nullptr)
				continue;
			bool known = false;
			for (const auto &kv : namedTex)
				known = known || kv.second.get() == rhiTex.get();
			if (!known)
				dumpTextures.push_back(rhiTex);
		}
		for (const auto &kv : namedTex)
			dumpTextures.push_back(kv.second);

		// UI font atlases/document images are neither pipeline-registered nor
		// render targets; pull them from the UI renderer explicitly.
		std::vector<std::shared_ptr<Texture>> uiTextures;
		GameUI::CollectUITextures(uiTextures);
		for (const auto &tex : uiTextures)
			if (tex->GetRHITexture() != nullptr)
				dumpTextures.push_back(tex->GetRHITexture());

		std::ostringstream texJson;
		texJson << "[\n";
		bool firstTex = true;
		int texCount = 0, texWritten = 0;
		std::unordered_map<std::string, int> usedNames;
		for (const auto &rhiTex : dumpTextures)
		{
			++texCount;
			RHIPixelData pixels;
			if (!GRHIDevice->Readback(rhiTex, pixels) || pixels.Pixels.empty())
				continue;
			const int slices = std::max(1, pixels.Depth);
			const size_t sliceBytes = static_cast<size_t>(pixels.Width) * pixels.Height * 4;
			std::string base = SanitizeName(rhiTex->GetDesc().Name);
			if (base.empty() || base == "unnamed")
				base = "rt_" + std::to_string(texCount);
			// Temp-pool textures share auto-generated names; keep the dump
			// filenames unique.
			if (++usedNames[base] > 1)
				base += "_" + std::to_string(usedNames[base]);
			std::string pngPaths;
			for (int slice = 0; slice < slices; ++slice)
			{
				std::vector<unsigned char> rgba(pixels.Pixels.data() + slice * sliceBytes,
					pixels.Pixels.data() + (slice + 1) * sliceBytes);
				FlipRows(rgba, pixels.Width, pixels.Height);
				const std::string fileName = base + (slices > 1 ? "_s" + std::to_string(slice) : "") + ".png";
				const std::string path = outDir + "/textures/" + fileName;
				if (stbi_write_png(path.c_str(), pixels.Width, pixels.Height, 4, rgba.data(), pixels.Width * 4) != 0)
				{
					++texWritten;
					if (!pngPaths.empty()) pngPaths += ", ";
					pngPaths += "\"textures/" + fileName + "\"";
				}
			}
			const TexStats stats = ComputeStats(pixels.Pixels);
			if (!firstTex) texJson << ",\n";
			firstTex = false;
			texJson << "    {\"name\": \"" << JsonEscape(rhiTex->GetDesc().Name) << "\", \"w\": " << pixels.Width
				<< ", \"h\": " << pixels.Height << ", \"format\": \"" << EnumUtil::TextureFormatToString(rhiTex->GetDesc().Format)
				<< "\", \"type\": \"" << EnumUtil::TextureTypeToString(rhiTex->GetDesc().Type) << "\", \"slices\": " << slices
				<< ", \"png\": [" << pngPaths << "], \"stats\": [" << stats.min << ", " << stats.max << ", " << stats.mean << "]}";
		}
		texJson << "\n  ]";

		// Final composite (UI included) lands on the backbuffer, not in a
		// render target; dump it alongside so a dump alone shows the result.
		{
			RHIPixelData bb;
			if (GRHIDevice->ReadbackBackbuffer(bb) && !bb.Pixels.empty())
			{
				FlipRows(bb.Pixels, bb.Width, bb.Height);
				if (stbi_write_png((outDir + "/backbuffer.png").c_str(), bb.Width, bb.Height, 4,
					bb.Pixels.data(), bb.Width * 4) != 0)
					++texWritten;
			}
		}

		// Passes: graph nodes with config + input/output texture edges.
		std::ostringstream passJson;
		passJson << "[\n";
		bool firstPass = true;
		pipeline->ForEachPass([&](const std::shared_ptr<Pass> &pass) -> bool {
			if (pass == nullptr)
				return true;
			if (!firstPass) passJson << ",\n";
			firstPass = false;
			const Color cc = pass->GetClearColor();
			passJson << "    {\"name\": \"" << JsonEscape(pass->GetName()) << "\", \"renderIndex\": " << pass->GetRenderIndex()
				<< ", \"clearMode\": \"" << EnumUtil::ClearModeToString(pass->GetClearMode())
				<< "\", \"clearColor\": [" << cc.r << ", " << cc.g << ", " << cc.b << ", " << cc.a << "]"
				<< ", \"depthTest\": \"" << EnumUtil::CompareModeToString(pass->GetCompareMode())
				<< "\", \"blend\": \"" << EnumUtil::BlendModeToString(pass->GetBlendMode())
				<< "\", \"cull\": \"" << EnumUtil::CullModeToString(pass->GetCullMode()) << "\"";
			std::ostringstream shaders;
			bool firstShader = true;
			for (unsigned int t = 0; t < 4; ++t)
			{
				if (auto shader = pass->GetShader(static_cast<ShaderType>(t)))
				{
					if (!firstShader) shaders << ", ";
					firstShader = false;
					shaders << "\"" << JsonEscape(shader->GetName()) << "\"";
				}
			}
			passJson << ", \"shaders\": [" << shaders.str() << "]";
			for (int input = 0; input < 2; ++input)
			{
				passJson << (input ? ", \"inputs\": [" : ", \"outputs\": [");
				bool firstTexName = true;
				for (unsigned int i = 0; i < pass->GetTextureCount(input != 0); ++i)
				{
					auto tex = pass->GetTextureAt(i, input != 0);
					if (tex == nullptr)
						continue;
					if (!firstTexName) passJson << ", ";
					firstTexName = false;
					passJson << "\"" << JsonEscape(tex->GetName()) << "\"";
				}
				passJson << "]";
			}
			passJson << "}";
			return true;
		});
		passJson << "\n  ]";

		// Postfx chain (effects with shader + inputs).
		std::ostringstream chainJson;
		chainJson << "[\n";
		bool firstEffect = true;
		for (const auto &effect : packet.chain)
		{
			if (!effect)
				continue;
			if (!firstEffect) chainJson << ",\n";
			firstEffect = false;
			chainJson << "    {\"name\": \"" << JsonEscape(effect->GetName()) << "\", \"shader\": \""
				<< JsonEscape(effect->GetShaderPath()) << "\", \"inputs\": [";
			bool firstInput = true;
			for (const auto &in : effect->GetInputs())
			{
				if (!firstInput) chainJson << ", ";
				firstInput = false;
				chainJson << "\"" << JsonEscape(in) << "\"";
			}
			chainJson << "]}";
		}
		chainJson << "\n  ]";

		std::ostringstream graph;
		graph << "{\n  \"frame\": {\"index\": " << packet.frameIndex << ", \"backend\": \""
			<< RHI::BackendToString(GRHIDevice->GetBackend()) << "\", \"size\": [" << packet.windowW << ", " << packet.windowH << "]},\n"
			<< "  \"textures\": " << texJson.str() << ",\n"
			<< "  \"passes\": " << passJson.str() << ",\n"
			<< "  \"chain\": " << chainJson.str() << "\n}\n";
		const std::string graphPath = outDir + "/rendergraph.json";
		{
			std::ofstream out(graphPath);
			out << graph.str();
			if (!out.good())
			{
				FURYE << "GpuDump: failed writing '" << graphPath << "'";
				return false;
			}
		}

		FURYI << "GpuDump: frame " << packet.frameIndex << " -> " << outDir << " (" << texWritten << "/" << texCount << " textures)";
		return true;
	}
}
