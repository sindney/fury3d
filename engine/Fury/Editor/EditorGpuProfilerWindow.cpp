#if WITH_EDITOR

// The node editor's public API exposes no view pan or zoom; its internal
// header does (EditorContext::NavigateTo, GetView). Pinned with the
// submodule, so a submodule bump that moves these shows up as a compile
// error here.
#define IMGUI_DEFINE_MATH_OPERATORS

#include "Fury/Editor/EditorGpuProfilerWindow.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui-node-editor/imgui_node_editor_internal.h>
#include <rapidjson/document.h>

// Declarations only: the implementations live in FileUtil.cpp and
// Engine.cpp, which are in the same target.
#include "stb_image.h"
#include "stb_image_write.h"

#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Texture.h"
#include "ImGui/imgui.h"
#include "nfd.h"

namespace fury
{
#if PLATFORM_MACOS
	// Defined in Engine_dpi_mac.mm: the precise trackpad scroll and the
	// pinch gesture, which SFML does not report.
	void furyMacOSInstallTrackpadMonitor();
	void furyMacOSPollTrackpadGesture(float &scrollX, float &scrollY, float &magnify);
#endif

	namespace Editor
	{
		namespace
		{
			// Pin/node id encoding: disjoint uintptr ranges per id kind.
			constexpr uintptr_t PassNodeBase = 1;            // + pass index
			constexpr uintptr_t ChainNodeBase = 100000;      // + chain index
			// The pins of one owner are PinStride apart; the kind bases are
			// a million apart, so the ranges hold thousands of owners.
			constexpr uintptr_t PinStride = 256;
			constexpr uintptr_t PassInPinBase = 1000000;     // + pass*PinStride + input
			constexpr uintptr_t PassOutPinBase = 2000000;    // + pass*PinStride + output
			constexpr uintptr_t ChainInPinBase = 3000000;    // + chain*PinStride + input
			constexpr uintptr_t ChainOutPinBase = 4000000;   // + chain*PinStride

			// Graph layout: the gap between the nodes.
			constexpr float LayoutGap = 60.0f;
			// A fit that shrinks the labels below this is worse than starting
			// at 1:1 and letting the user pan.
			constexpr float MinFitScale = 0.5f;
			// Zoom range of the editor.
			constexpr float MinZoom = 0.1f;
			constexpr float MaxZoom = 3.0f;
			constexpr float SmoothZoomPower = 1.1f;

			constexpr float DetailsWidth = 320.0f;
			constexpr float DirFieldWidth = 300.0f;
			constexpr float PreviewHeight = 220.0f;

			struct TexEntry
			{
				std::string name;

				int w = 0;

				int h = 0;

				int slices = 1;

				std::string format;

				std::string type;

				std::vector<std::string> pngs;

				float stats[3] = {0.0f, 0.0f, 0.0f};

				// Lazy previews; upload is render-thread queued, so GetID()
				// lands a frame or two after the upload.
				Texture::Ptr preview;

				Texture::Ptr previewDisplay;

				int previewSlice = 0;

				// The preview shows the dumped values unless the display
				// view is picked.
				bool displayView = false;

				// The dumped values are already display values (an sRGB
				// target), so the display view is the raw one.
				bool srgbEncoded = false;
			};

			struct PassEntry
			{
				std::string name;

				int renderIndex = 0;

				std::string clearMode;

				float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

				std::string depthTest;

				std::string blend;

				std::string cull;

				std::vector<std::string> shaders;

				std::vector<std::string> outputs;

				std::vector<std::string> inputs;

				// Pin labels as drawn in the node: "-> name" and "name ->".
				std::vector<std::string> inputLabels;

				std::vector<std::string> outputLabels;
			};

			struct ChainEntry
			{
				std::string name;

				std::string shader;

				std::vector<std::string> inputs;

				std::vector<std::string> inputLabels;
			};

			struct LinkRec
			{
				uintptr_t id = 0;

				int texIdx = -1;

				int startPin = 0;

				int endPin = 0;
			};

			struct Capture
			{
				std::string dir;

				int frameIndex = 0;

				std::string backend;

				int sizeW = 0;

				int sizeH = 0;

				bool hasBackbuffer = false;

				std::vector<TexEntry> textures;

				std::vector<PassEntry> passes;

				std::vector<ChainEntry> chain;

				std::vector<LinkRec> links;

				std::unordered_map<std::string, size_t> texByName;

				Texture::Ptr backbufferPreview;
			};

			std::string g_OpenDir;

			char g_DirBuf[1024] = "";

			bool g_WindowOpen = false;

			std::string g_LoadError;

			std::unique_ptr<Capture> g_Capture;

			ax::NodeEditor::EditorContext *g_Editor = nullptr;

			bool g_NeedsLayout = false;

			bool g_FitAfterDraw = false;

			bool g_FitPending = false;

			ImVec2 g_LastCanvasSize = ImVec2(0.0f, 0.0f);

			ImVec2 g_ContentSize = ImVec2(0.0f, 0.0f);

			int g_SettleFrames = 0;

			struct NodePosition
			{
				uintptr_t id = 0;

				ImVec2 pos = ImVec2(0.0f, 0.0f);
			};

			std::vector<NodePosition> g_NodePositions;

			int g_SelectedPass = -1;

			int g_SelectedChain = -1;

			int g_SelectedTex = -1;

			// The texture was opened from a node's input/output list, so the
			// details pane can go back to it.
			bool g_TextureFromNode = false;

			// Last selection seen in the node editor; the editor keeps its own
			// selection, so the panel state only follows a change.
			uintptr_t g_EditorNodeSel = 0;

			uintptr_t g_EditorLinkSel = 0;

			// Trackpad gestures: a two-finger scroll pans, a pinch zooms.
			// The zoom is in wheel units, the way a wheel consumer expects.
			struct TrackpadGesture
			{
				float scrollX = 0.0f;

				float scrollY = 0.0f;

				float magnify = 0.0f;
			};

			TrackpadGesture PollTrackpadGesture()
			{
				TrackpadGesture gesture;
#if PLATFORM_MACOS
				furyMacOSPollTrackpadGesture(gesture.scrollX, gesture.scrollY, gesture.magnify);
#endif
				return gesture;
			}

			std::string ReadFile(const std::string &path)
			{
				std::string out;
				if (!FileUtil::LoadString(path, out))
					return "";
				return out;
			}

			std::vector<std::string> StringArray(const rapidjson::Value &v)
			{
				std::vector<std::string> out;
				if (v.IsArray())
					for (rapidjson::SizeType i = 0; i < v.Size(); ++i)
						if (v[i].IsString())
							out.emplace_back(v[i].GetString());
				return out;
			}

			// The post-process chain names its inputs after the pipeline's
			// textures: a "$name" input is a reserved scene texture, while
			// "$scene" and bare names take the previous chain output. The
			// first effect takes the lighting output instead.
			std::string ResolveChainInput(const Capture &cap, size_t chainIdx, size_t inputIdx)
			{
				const std::string &name = cap.chain[chainIdx].inputs[inputIdx];
				if (!name.empty() && name[0] == '$' && name != "$scene")
					return name.substr(1);
				if (chainIdx == 0)
				{
					if (cap.texByName.count("hdr_composite"))
						return "hdr_composite";
					if (cap.texByName.count("gbuffer_light"))
						return "gbuffer_light";
				}
				return "";
			}

			// The pass that produces a texture, or -1.
			int ProducerPass(const Capture &cap, const std::string &texName)
			{
				for (size_t pi = 0; pi < cap.passes.size(); ++pi)
					for (const std::string &out : cap.passes[pi].outputs)
						if (out == texName)
							return static_cast<int>(pi);
				return -1;
			}

			// The dumped values of an sRGB target are already display values.
			bool FormatHoldsDisplayValues(const std::string &format)
			{
				std::string lower = format;
				std::transform(lower.begin(), lower.end(), lower.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return lower.find("srgb") != std::string::npos;
			}

			// Producer pass -> consumer pass/chain edges keyed by texture
			// name; the texture index rides along for link selection.
			void BuildLinks(Capture &cap)
			{
				cap.links.clear();
				uintptr_t nextId = 1;
				for (size_t pi = 0; pi < cap.passes.size(); ++pi)
				{
					const PassEntry &pass = cap.passes[pi];
					for (size_t oi = 0; oi < pass.outputs.size(); ++oi)
					{
						const auto it = cap.texByName.find(pass.outputs[oi]);
						if (it == cap.texByName.end())
							continue;
						const int texIdx = static_cast<int>(it->second);
						const uintptr_t startPin = PassOutPinBase + pi * PinStride + oi;
						for (size_t qi = 0; qi < cap.passes.size(); ++qi)
						{
							const auto &in = cap.passes[qi].inputs;
							const auto ii = std::find(in.begin(), in.end(), pass.outputs[oi]);
							if (qi == pi || ii == in.end())
								continue;
							cap.links.push_back({nextId++, texIdx, static_cast<int>(startPin),
								static_cast<int>(PassInPinBase + qi * PinStride + (ii - in.begin()))});
						}
					}
				}

				// The lighting output the first effect falls back to.
				std::string lighting = cap.texByName.count("hdr_composite") ? "hdr_composite" :
					(cap.texByName.count("gbuffer_light") ? "gbuffer_light" : "");
				int lightingTex = -1;
				if (!lighting.empty())
				{
					const auto tex = cap.texByName.find(lighting);
					if (tex != cap.texByName.end())
						lightingTex = static_cast<int>(tex->second);
				}
				// The texture the chain flow starts from: the first effect's
				// named input, or the lighting output.
				int chainOriginTex = lightingTex;
				for (size_t ci = 0; ci < cap.chain.size() && chainOriginTex < 0; ++ci)
					for (size_t ii = 0; ii < cap.chain[ci].inputs.size(); ++ii)
					{
						const std::string texName = ResolveChainInput(cap, ci, ii);
						if (texName.empty())
							continue;
						const auto tex = cap.texByName.find(texName);
						if (tex != cap.texByName.end())
						{
							chainOriginTex = static_cast<int>(tex->second);
							break;
						}
					}

				// Chain inputs: a reserved scene texture links to the pass
				// that writes it, the rest to the previous chain output.
				for (size_t ci = 0; ci < cap.chain.size(); ++ci)
				{
					for (size_t ii = 0; ii < cap.chain[ci].inputs.size(); ++ii)
					{
						const uintptr_t endPin = ChainInPinBase + ci * PinStride + ii;
						const std::string texName = ResolveChainInput(cap, ci, ii);
						if (!texName.empty())
						{
							const int producer = ProducerPass(cap, texName);
							if (producer < 0)
								continue;
							const PassEntry &pass = cap.passes[static_cast<size_t>(producer)];
							const auto out = std::find(pass.outputs.begin(), pass.outputs.end(), texName);
							if (out == pass.outputs.end())
								continue;
							const auto tex = cap.texByName.find(texName);
							if (tex == cap.texByName.end())
								continue;
							cap.links.push_back({nextId++, static_cast<int>(tex->second),
								static_cast<int>(PassOutPinBase + static_cast<size_t>(producer) * PinStride +
									(out - pass.outputs.begin())),
								static_cast<int>(endPin)});
							continue;
						}
						// The chain flow: the previous effect, or the pass that
						// writes the lighting output.
						if (ci == 0)
						{
							if (lighting.empty() || lightingTex < 0)
								continue;
							const int producer = ProducerPass(cap, lighting);
							if (producer < 0)
								continue;
							const PassEntry &pass = cap.passes[static_cast<size_t>(producer)];
							const auto out = std::find(pass.outputs.begin(), pass.outputs.end(), lighting);
							if (out == pass.outputs.end())
								continue;
							cap.links.push_back({nextId++, lightingTex,
								static_cast<int>(PassOutPinBase + static_cast<size_t>(producer) * PinStride +
									(out - pass.outputs.begin())),
								static_cast<int>(endPin)});
						}
						else
						{
							// The previous effect writes a chain temp; the link
							// carries the texture the chain started from.
							cap.links.push_back({nextId++, chainOriginTex,
								static_cast<int>(ChainOutPinBase + (ci - 1) * PinStride),
								static_cast<int>(endPin)});
						}
					}
				}
			}

			bool LoadCapture(const std::string &dir, Capture &out)
			{
				out.dir = dir;
				const std::string json = ReadFile(dir + "/rendergraph.json");
				if (json.empty())
				{
					FURYE << "GpuProfiler: no rendergraph.json in '" << dir << "'";
					return false;
				}

				rapidjson::Document doc;
				if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject())
				{
					FURYE << "GpuProfiler: rendergraph.json in '" << dir << "' is not valid JSON";
					return false;
				}

				if (doc.HasMember("frame") && doc["frame"].IsObject())
				{
					const auto &frame = doc["frame"];
					if (frame.HasMember("index") && frame["index"].IsInt())
						out.frameIndex = frame["index"].GetInt();
					if (frame.HasMember("backend") && frame["backend"].IsString())
						out.backend = frame["backend"].GetString();
					if (frame.HasMember("size") && frame["size"].IsArray() && frame["size"].Size() >= 2)
					{
						out.sizeW = frame["size"][0].GetInt();
						out.sizeH = frame["size"][1].GetInt();
					}
				}

				if (doc.HasMember("textures") && doc["textures"].IsArray())
				{
					for (const auto &t : doc["textures"].GetArray())
					{
						if (!t.IsObject() || !t.HasMember("name") || !t["name"].IsString())
							continue;
						TexEntry tex;
						tex.name = t["name"].GetString();
						if (t.HasMember("w") && t["w"].IsInt()) tex.w = t["w"].GetInt();
						if (t.HasMember("h") && t["h"].IsInt()) tex.h = t["h"].GetInt();
						if (t.HasMember("slices") && t["slices"].IsInt()) tex.slices = t["slices"].GetInt();
						if (t.HasMember("format") && t["format"].IsString()) tex.format = t["format"].GetString();
						if (t.HasMember("type") && t["type"].IsString()) tex.type = t["type"].GetString();
						if (t.HasMember("png") && t["png"].IsArray())
							for (rapidjson::SizeType pi = 0; pi < t["png"].Size(); ++pi)
								if (t["png"][pi].IsString())
									tex.pngs.emplace_back(t["png"][pi].GetString());
						if (t.HasMember("stats") && t["stats"].IsArray() && t["stats"].Size() >= 3)
							for (int i = 0; i < 3; ++i)
								tex.stats[i] = t["stats"][static_cast<rapidjson::SizeType>(i)].GetFloat();
						tex.srgbEncoded = FormatHoldsDisplayValues(tex.format);
						out.texByName[tex.name] = out.textures.size();
						out.textures.push_back(std::move(tex));
					}
				}

				if (doc.HasMember("passes") && doc["passes"].IsArray())
				{
					for (const auto &p : doc["passes"].GetArray())
					{
						if (!p.IsObject() || !p.HasMember("name") || !p["name"].IsString())
							continue;
						PassEntry pass;
						pass.name = p["name"].GetString();
						if (p.HasMember("renderIndex") && p["renderIndex"].IsInt())
							pass.renderIndex = p["renderIndex"].GetInt();
						if (p.HasMember("clearMode") && p["clearMode"].IsString()) pass.clearMode = p["clearMode"].GetString();
						if (p.HasMember("depthTest") && p["depthTest"].IsString()) pass.depthTest = p["depthTest"].GetString();
						if (p.HasMember("blend") && p["blend"].IsString()) pass.blend = p["blend"].GetString();
						if (p.HasMember("cull") && p["cull"].IsString()) pass.cull = p["cull"].GetString();
						if (p.HasMember("clearColor") && p["clearColor"].IsArray() && p["clearColor"].Size() >= 4)
							for (int i = 0; i < 4; ++i)
								pass.clearColor[i] = p["clearColor"][static_cast<rapidjson::SizeType>(i)].GetFloat();
						pass.shaders = p.HasMember("shaders") ? StringArray(p["shaders"]) : std::vector<std::string>();
						pass.outputs = p.HasMember("outputs") ? StringArray(p["outputs"]) : std::vector<std::string>();
						pass.inputs = p.HasMember("inputs") ? StringArray(p["inputs"]) : std::vector<std::string>();
						for (const std::string &s : pass.inputs)
							pass.inputLabels.push_back("-> " + s);
						for (const std::string &s : pass.outputs)
							pass.outputLabels.push_back(s + " ->");
						out.passes.push_back(std::move(pass));
					}
				}

				if (doc.HasMember("chain") && doc["chain"].IsArray())
				{
					for (const auto &e : doc["chain"].GetArray())
					{
						if (!e.IsObject() || !e.HasMember("name") || !e["name"].IsString())
							continue;
						ChainEntry chain;
						chain.name = e["name"].GetString();
						if (e.HasMember("shader") && e["shader"].IsString()) chain.shader = e["shader"].GetString();
						chain.inputs = e.HasMember("inputs") ? StringArray(e["inputs"]) : std::vector<std::string>();
						for (const std::string &s : chain.inputs)
							chain.inputLabels.push_back("-> " + s);
						out.chain.push_back(std::move(chain));
					}
				}

				std::error_code ec;
				out.hasBackbuffer = std::filesystem::exists(std::filesystem::path(dir) / "backbuffer.png", ec);

				if (out.passes.empty() && out.textures.empty() && out.chain.empty())
				{
					FURYE << "GpuProfiler: rendergraph.json in '" << dir << "' has no passes/textures/chain";
					return false;
				}

				std::stable_sort(out.passes.begin(), out.passes.end(),
					[](const PassEntry &a, const PassEntry &b) { return a.renderIndex < b.renderIndex; });
				BuildLinks(out);
				return true;
			}

			// The editor's view state. The public API has no pan or zoom
			// setter, so the internal context is used for the viewport
			// changes the trackpad gestures drive.
			ax::NodeEditor::Detail::EditorContext *EditorContext()
			{
				return reinterpret_cast<ax::NodeEditor::Detail::EditorContext *>(g_Editor);
			}

			void DestroyEditor()
			{
				if (g_Editor == nullptr)
					return;
				ax::NodeEditor::DestroyEditor(g_Editor);
				g_Editor = nullptr;
			}

			void EnsureEditor()
			{
				if (g_Editor != nullptr)
					return;
				ax::NodeEditor::Config config;
				config.SettingsFile = nullptr;
				// Mouse behaviour is the editor default: left drag moves a
				// node, left click selects, right drag pans the view.
				// Continuous zoom, so a pinch moves in fine steps.
				config.EnableSmoothZoom = true;
				config.SmoothZoomPower = SmoothZoomPower;
				// Range the smooth zoom is clamped to, so a wide graph can
				// still be pinched down to a full overview.
				config.CustomZoomLevels.push_back(MinZoom);
				config.CustomZoomLevels.push_back(MaxZoom);
				g_Editor = ax::NodeEditor::CreateEditor(&config);
#if PLATFORM_MACOS
				furyMacOSInstallTrackpadMonitor();
#endif
			}

			// A capture is a plain directory, not a scene asset: read it off disk.
			std::string CapturePath(const std::string &dir, const std::string &relPath)
			{
				std::error_code ec;
				const std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(dir) / relPath, ec);
				if (ec)
					return dir + "/" + relPath;
				return path.string();
			}

			bool ReadCaptureFile(const std::string &path, std::vector<unsigned char> &out)
			{
				std::ifstream stream(path, std::ios::binary);
				if (!stream)
					return false;
				stream.seekg(0, std::ios::end);
				const std::streamsize size = stream.tellg();
				stream.seekg(0, std::ios::beg);
				if (size <= 0)
					return false;
				out.resize(static_cast<size_t>(size));
				stream.read(reinterpret_cast<char *>(out.data()), size);
				return stream.gcount() == size;
			}

			void AppendBytes(void *context, void *data, int size)
			{
				std::vector<unsigned char> *out = static_cast<std::vector<unsigned char> *>(context);
				const unsigned char *bytes = static_cast<const unsigned char *>(data);
				out->insert(out->end(), bytes, bytes + size);
			}

			// The sRGB encode of a byte value.
			unsigned char SrgbEncodeByte(unsigned char value)
			{
				static const std::array<unsigned char, 256> table = []()
				{
					std::array<unsigned char, 256> t{};
					for (int i = 0; i < 256; ++i)
						t[static_cast<size_t>(i)] = static_cast<unsigned char>(std::lround(
							std::pow(static_cast<float>(i) / 255.0f, 1.0f / 2.2f) * 255.0f));
					return t;
				}();
				return table[value];
			}

			// The sRGB encode of a 0..1 value, so linear dumps read the way
			// they do on screen.
			float SrgbEncode(float v)
			{
				return std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / 2.2f);
			}

			// The value as it appears on screen: the dumped values of an
			// sRGB target are already display values.
			float DisplayValue(const TexEntry &tex, float v)
			{
				return tex.srgbEncoded ? v : SrgbEncode(v);
			}

			// The dumped values, as they are.
			Texture::Ptr LoadPreview(const std::string &dir, const std::string &relPath, const std::string &name)
			{
				Texture::Ptr tex = Texture::Create(name);
				tex->CreateFromImage(CapturePath(dir, relPath), false, false);
				return tex;
			}

			// The dumped values as they appear on screen: the sRGB encode of
			// the linear values, applied on the CPU.
			Texture::Ptr LoadDisplayPreview(const std::string &dir, const std::string &relPath, const std::string &name)
			{
				std::vector<unsigned char> file;
				if (!ReadCaptureFile(CapturePath(dir, relPath), file))
					return nullptr;

				int width = 0;
				int height = 0;
				int channels = 0;
				unsigned char *pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
					&width, &height, &channels, 4);
				if (pixels == nullptr || width <= 0 || height <= 0)
				{
					if (pixels != nullptr)
						stbi_image_free(pixels);
					return nullptr;
				}

				std::vector<unsigned char> encoded(static_cast<size_t>(width) * height * 4);
				for (size_t i = 0; i < encoded.size(); i += 4)
				{
					encoded[i + 0] = SrgbEncodeByte(pixels[i + 0]);
					encoded[i + 1] = SrgbEncodeByte(pixels[i + 1]);
					encoded[i + 2] = SrgbEncodeByte(pixels[i + 2]);
					encoded[i + 3] = pixels[i + 3];
				}
				stbi_image_free(pixels);

				std::vector<unsigned char> png;
				stbi_write_png_to_func(AppendBytes, &png, width, height, 4, encoded.data(), width * 4);

				Texture::Ptr tex = Texture::Create(name);
				tex->CreateFromMemory(png.data(), png.size(), false, false);
				return tex;
			}

			// The width a preview can use without overflowing the panel.
			float PreviewWidth()
			{
				return std::max(64.0f, ImGui::GetContentRegionAvail().x - 4.0f);
			}

			void DrawPreview(Texture::Ptr tex, float maxW, float maxH)
			{
				uintptr_t id = tex ? tex->GetID() : 0;
				if (id == 0)
				{
					ImGui::TextDisabled("(upload pending)");
					return;
				}
				float w = static_cast<float>(tex->GetWidth());
				float h = static_cast<float>(tex->GetHeight());
				if (w <= 0.0f || h <= 0.0f)
				{
					ImGui::TextDisabled("(no size)");
					return;
				}
				const float scale = std::min(maxW / w, maxH / h);
				const ImVec2 imageSize(w * scale, h * scale);
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				ImGui::Image((ImTextureID)(intptr_t)id, imageSize);
				// A fully black dump (a depth buffer, say) is invisible
				// against the panel, so outline the image.
				ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + imageSize.x, pos.y + imageSize.y),
					IM_COL32(255, 255, 255, 90));
			}

			// Truncates a string to fit a width, with an ellipsis.
			std::string ClipToWidth(const std::string &text, float maxWidth)
			{
				if (maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
					return text;
				const std::string ellipsis = "...";
				const float ellipsisWidth = ImGui::CalcTextSize(ellipsis.c_str()).x;
				size_t len = text.size();
				while (len > 0 && ImGui::CalcTextSize(text.substr(0, len).c_str()).x + ellipsisWidth > maxWidth)
					--len;
				return text.substr(0, len) + ellipsis;
			}

			// A label on one line: clipped to the panel width, with the full
			// text in a tooltip.
			void TextClipped(const std::string &text)
			{
				const std::string clipped = ClipToWidth(text, ImGui::GetContentRegionAvail().x);
				ImGui::TextUnformatted(clipped.c_str());
				if (clipped != text && ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", text.c_str());
			}

			void RenderTextureDetails(Capture &cap, int texIdx)
			{
				TexEntry &tex = cap.textures[static_cast<size_t>(texIdx)];
				ImGui::PushID(texIdx);
				TextClipped(tex.name);
				ImGui::Text("%dx%d, %d slice(s)", tex.w, tex.h, tex.slices);
				TextClipped("format: " + tex.format + " (" + tex.type + ")");
				ImGui::Text("stats (raw): min %.2f max %.2f mean %.2f", tex.stats[0], tex.stats[1], tex.stats[2]);
				ImGui::Text("stats (display): min %.2f max %.2f mean %.2f",
					DisplayValue(tex, tex.stats[0]), DisplayValue(tex, tex.stats[1]), DisplayValue(tex, tex.stats[2]));
				if (tex.pngs.empty())
				{
					ImGui::TextDisabled("no PNG readback for this texture");
				}
				else
				{
					if (tex.pngs.size() > 1)
					{
						if (ImGui::SliderInt("slice", &tex.previewSlice, 0, static_cast<int>(tex.pngs.size()) - 1))
						{
							tex.preview.reset();
							tex.previewDisplay.reset();
						}
					}
					const int slice = std::min(tex.previewSlice, static_cast<int>(tex.pngs.size()) - 1);
					const bool display = tex.displayView && !tex.srgbEncoded;
					Texture::Ptr &preview = display ? tex.previewDisplay : tex.preview;
					if (!preview)
					{
						const std::string rel = tex.pngs[static_cast<size_t>(slice)];
						preview = display ? LoadDisplayPreview(cap.dir, rel, "GpuProfiler_display_" + tex.name)
							: LoadPreview(cap.dir, rel, "GpuProfiler_" + tex.name);
					}
					DrawPreview(preview, PreviewWidth(), PreviewHeight);
					if (!tex.srgbEncoded && ImGui::BeginPopupContextItem("gpu_profiler_preview"))
					{
						if (ImGui::MenuItem("Raw", nullptr, !tex.displayView))
							tex.displayView = false;
						if (ImGui::MenuItem("Display", nullptr, tex.displayView))
							tex.displayView = true;
						ImGui::EndPopup();
					}
				}
				ImGui::PopID();
			}

			// A texture name in a node's input/output list, with its capture
			// stats; clicking it opens the texture in the details pane.
			void RenderTextureRow(Capture &cap, const std::string &name)
			{
				auto it = cap.texByName.find(name);
				const int texIdx = (it != cap.texByName.end()) ? static_cast<int>(it->second) : -1;
				ImGui::PushID(name.c_str());
				if (ImGui::Selectable(name.c_str(), texIdx >= 0 && g_SelectedTex == texIdx) && texIdx >= 0)
				{
					g_SelectedTex = texIdx;
					g_TextureFromNode = true;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", name.c_str());
				if (texIdx >= 0)
				{
					const TexEntry &tex = cap.textures[static_cast<size_t>(texIdx)];
					ImGui::TextDisabled("  %dx%d %s", tex.w, tex.h, tex.format.c_str());
					ImGui::TextDisabled("  min %.2f max %.2f mean %.2f", tex.stats[0], tex.stats[1], tex.stats[2]);
				}
				else
				{
					ImGui::TextDisabled("  (not in capture)");
				}
				ImGui::PopID();
			}

			void RenderPassDetails(Capture &cap, int passIdx)
			{
				const PassEntry &pass = cap.passes[static_cast<size_t>(passIdx)];
				ImGui::PushID(passIdx);
				TextClipped(pass.name);
				ImGui::Text("render index: %d", pass.renderIndex);
				TextClipped("clear mode: " + pass.clearMode);
				ImGui::ColorEdit4("clear color", const_cast<float *>(pass.clearColor),
					ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker);
				ImGui::Text("depth test: %s", pass.depthTest.c_str());
				ImGui::Text("blend: %s", pass.blend.c_str());
				ImGui::Text("cull: %s", pass.cull.c_str());
				if (!pass.shaders.empty())
				{
					ImGui::Separator();
					ImGui::TextUnformatted("Shaders:");
					for (const std::string &s : pass.shaders)
					{
						ImGui::Bullet();
						TextClipped(s);
					}
				}
				if (!pass.outputs.empty())
				{
					ImGui::Separator();
					ImGui::TextUnformatted("Outputs:");
					for (const std::string &s : pass.outputs)
						RenderTextureRow(cap, s);
				}
				if (!pass.inputs.empty())
				{
					ImGui::Separator();
					ImGui::TextUnformatted("Inputs:");
					for (const std::string &s : pass.inputs)
						RenderTextureRow(cap, s);
				}
				ImGui::PopID();
			}

			void RenderChainDetails(Capture &cap, int chainIdx)
			{
				const ChainEntry &chain = cap.chain[static_cast<size_t>(chainIdx)];
				ImGui::PushID(chainIdx);
				TextClipped(chain.name);
				TextClipped("shader: " + chain.shader);
				if (!chain.inputs.empty())
				{
					ImGui::Separator();
					ImGui::TextUnformatted("Inputs:");
					for (size_t ii = 0; ii < chain.inputs.size(); ++ii)
					{
						// A "$name" input is a scene texture, a bare name (and
						// "$scene") takes the previous chain output.
						const std::string resolved = ResolveChainInput(cap, static_cast<size_t>(chainIdx), ii);
						if (resolved.empty())
						{
							ImGui::TextUnformatted(chain.inputs[ii].c_str());
							ImGui::TextDisabled("  (previous chain output)");
						}
						else
						{
							RenderTextureRow(cap, resolved);
						}
					}
				}
				ImGui::PopID();
			}

			void RenderCaptureSummary(Capture &cap)
			{
				ImGui::TextUnformatted("Capture");
				ImGui::Text("frame: %d", cap.frameIndex);
				TextClipped("backend: " + cap.backend);
				ImGui::Text("size: %dx%d", cap.sizeW, cap.sizeH);
				ImGui::Text("passes: %zu", cap.passes.size());
				ImGui::Text("textures: %zu", cap.textures.size());
				ImGui::Text("chain: %zu", cap.chain.size());
				ImGui::Separator();
				if (ImGui::CollapsingHeader("Backbuffer", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (cap.hasBackbuffer)
					{
						if (!cap.backbufferPreview)
							cap.backbufferPreview = LoadPreview(cap.dir, "backbuffer.png", "GpuProfiler_backbuffer");
						DrawPreview(cap.backbufferPreview, PreviewWidth(), 160.0f);
					}
					else
					{
						ImGui::TextDisabled("backbuffer.png missing");
					}
				}
			}

			void FormatGraph(Capture &cap);

			// The button that returns from a texture to the node it was
			// opened from.
			bool RenderBackToNode(Capture &cap)
			{
				if (!g_TextureFromNode)
					return false;
				const char *name = nullptr;
				if (g_SelectedPass >= 0 && g_SelectedPass < static_cast<int>(cap.passes.size()))
					name = cap.passes[static_cast<size_t>(g_SelectedPass)].name.c_str();
				else if (g_SelectedChain >= 0 && g_SelectedChain < static_cast<int>(cap.chain.size()))
					name = cap.chain[static_cast<size_t>(g_SelectedChain)].name.c_str();
				if (name == nullptr)
					return false;
				const std::string label = std::string("<- ") + name;
				if (!ImGui::Button(label.c_str()))
					return false;
				g_SelectedTex = -1;
				g_TextureFromNode = false;
				return true;
			}

			void RenderDetailsPane(Capture &cap, const char *id, const ImVec2 &size, bool graphTab)
			{
				ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
				const bool showingTexture = g_SelectedTex >= 0 &&
					g_SelectedTex < static_cast<int>(cap.textures.size());
				if (showingTexture && RenderBackToNode(cap))
					ImGui::Separator();
				if (graphTab)
				{
					ImGui::TextDisabled("right drag / 2-finger scroll: pan");
					ImGui::TextDisabled("pinch: zoom   click: select");
					if (ImGui::Button("Format graph"))
						FormatGraph(cap);
					ImGui::Separator();
				}
				if (g_SelectedTex >= 0 && g_SelectedTex < static_cast<int>(cap.textures.size()))
					RenderTextureDetails(cap, g_SelectedTex);
				else if (g_SelectedPass >= 0 && g_SelectedPass < static_cast<int>(cap.passes.size()))
					RenderPassDetails(cap, g_SelectedPass);
				else if (g_SelectedChain >= 0 && g_SelectedChain < static_cast<int>(cap.chain.size()))
					RenderChainDetails(cap, g_SelectedChain);
				else if (graphTab)
					RenderCaptureSummary(cap);
				else
					ImGui::TextDisabled("Select a texture.");
				ImGui::EndChild();
			}

			// The node editor resolves its selection while ending the frame, so
			// the panel state is read back after End().
			void SyncEditorSelection(Capture &cap, bool canvasClicked)
			{
				ax::NodeEditor::NodeId nodeIds[4];
				const int nodeCount = ax::NodeEditor::GetSelectedNodes(nodeIds, 4);
				ax::NodeEditor::LinkId linkIds[4];
				const int linkCount = ax::NodeEditor::GetSelectedLinks(linkIds, 4);
				const uintptr_t nodeId = (nodeCount == 1) ? nodeIds[0].Get() : 0;
				const uintptr_t linkId = (linkCount == 1) ? linkIds[0].Get() : 0;

				// A click on the canvas follows the selection even when the
				// editor kept it, so the panel shows the node again.
				if (nodeId != g_EditorNodeSel || linkId != g_EditorLinkSel || canvasClicked)
				{
					g_EditorNodeSel = nodeId;
					g_EditorLinkSel = linkId;
					g_SelectedPass = -1;
					g_SelectedChain = -1;
					g_SelectedTex = -1;
					if (nodeId != 0)
					{
						if (nodeId >= ChainNodeBase)
							g_SelectedChain = static_cast<int>(nodeId - ChainNodeBase);
						else
							g_SelectedPass = static_cast<int>(nodeId - PassNodeBase);
					}
					else if (linkId != 0)
					{
						for (const LinkRec &link : cap.links)
							if (link.id == linkId)
							{
								if (link.texIdx >= 0)
									g_SelectedTex = link.texIdx;
								break;
							}
					}
				}

				if (ax::NodeEditor::IsBackgroundClicked())
				{
					g_EditorNodeSel = 0;
					g_EditorLinkSel = 0;
					g_SelectedPass = -1;
					g_SelectedChain = -1;
					g_SelectedTex = -1;
					ax::NodeEditor::ClearSelection();
				}
			}

			// The widest line sets the node width. Output labels are right
			// aligned to it so their links attach on the node's right edge.
			float NodeContentWidth(const std::vector<std::string> &lines)
			{
				float width = 0.0f;
				for (const std::string &line : lines)
					width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
				return width;
			}

			// Node width including the editor's node padding.
			float NodeWidth(const std::vector<std::string> &lines)
			{
				return NodeContentWidth(lines) + 16.0f;
			}

			void DrawInputPin(uintptr_t pinId, const std::string &label)
			{
				ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(pinId), ax::NodeEditor::PinKind::Input);
				ax::NodeEditor::PinPivotAlignment(ImVec2(0.0f, 0.5f));
				ImGui::TextUnformatted(label.c_str());
				ax::NodeEditor::EndPin();
			}

			void DrawOutputPin(uintptr_t pinId, const std::string &label, float contentWidth)
			{
				ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(pinId), ax::NodeEditor::PinKind::Output);
				ax::NodeEditor::PinPivotAlignment(ImVec2(1.0f, 0.5f));
				const float lineX = ImGui::GetCursorPosX();
				ImGui::SetCursorPosX(lineX + contentWidth - ImGui::CalcTextSize(label.c_str()).x);
				ImGui::TextUnformatted(label.c_str());
				ax::NodeEditor::EndPin();
			}

			// The node a pin belongs to: the passes first, then the chain.
			int PinOwner(const Capture &cap, uintptr_t pin)
			{
				const size_t passCount = cap.passes.size();
				if (pin >= ChainOutPinBase)
					return static_cast<int>(passCount + (pin - ChainOutPinBase) / PinStride);
				if (pin >= ChainInPinBase)
					return static_cast<int>(passCount + (pin - ChainInPinBase) / PinStride);
				if (pin >= PassOutPinBase)
					return static_cast<int>((pin - PassOutPinBase) / PinStride);
				return static_cast<int>((pin - PassInPinBase) / PinStride);
			}

			uintptr_t NodeIdOf(const Capture &cap, int node)
			{
				const size_t passCount = cap.passes.size();
				return static_cast<size_t>(node) < passCount ? PassNodeBase + static_cast<uintptr_t>(node)
					: ChainNodeBase + static_cast<uintptr_t>(node) - passCount;
			}

			std::vector<std::string> PassNodeLines(const PassEntry &pass)
			{
				std::vector<std::string> lines;
				lines.push_back(pass.name);
				lines.insert(lines.end(), pass.inputLabels.begin(), pass.inputLabels.end());
				lines.insert(lines.end(), pass.outputLabels.begin(), pass.outputLabels.end());
				return lines;
			}

			std::vector<std::string> ChainNodeLines(const ChainEntry &chain)
			{
				std::vector<std::string> lines;
				lines.push_back(chain.name);
				lines.push_back(chain.shader);
				lines.insert(lines.end(), chain.inputLabels.begin(), chain.inputLabels.end());
				lines.push_back("chain out ->");
				return lines;
			}

			// Layered layout: the nodes are placed in columns by dependency
			// depth and ordered within a column by the barycenter of their
			// neighbours, which keeps the links from crossing.
			ImVec2 LayoutGraph(Capture &cap)
			{
				const size_t passCount = cap.passes.size();
				const size_t nodeCount = passCount + cap.chain.size();
				g_NodePositions.clear();
				if (nodeCount == 0)
					return ImVec2(0.0f, 0.0f);

				std::vector<std::vector<std::string>> lines(nodeCount);
				for (size_t pi = 0; pi < passCount; ++pi)
					lines[pi] = PassNodeLines(cap.passes[pi]);
				for (size_t ci = 0; ci < cap.chain.size(); ++ci)
					lines[passCount + ci] = ChainNodeLines(cap.chain[ci]);

				std::vector<float> widths(nodeCount, 0.0f);
				std::vector<float> heights(nodeCount, 0.0f);
				const float lineHeight = ImGui::GetTextLineHeight();
				const float lineSpacing = ImGui::GetStyle().ItemSpacing.y;
				for (size_t i = 0; i < nodeCount; ++i)
				{
					widths[i] = NodeWidth(lines[i]);
					// One text line per label, the spacing between them and
					// the editor's node padding.
					const float count = static_cast<float>(lines[i].size());
					heights[i] = count * lineHeight + std::max(count - 1.0f, 0.0f) * lineSpacing + 16.0f;
				}

				// Node edges, taken from the pin links.
				std::vector<std::pair<int, int>> edges;
				for (const LinkRec &link : cap.links)
				{
					const int from = PinOwner(cap, static_cast<uintptr_t>(link.startPin));
					const int to = PinOwner(cap, static_cast<uintptr_t>(link.endPin));
					if (from >= 0 && to >= 0 && from != to)
						edges.push_back({from, to});
				}

				// The layer of a node: the longest path from a source.
				std::vector<int> layer(nodeCount, 0);
				for (int iter = 0; iter < static_cast<int>(nodeCount); ++iter)
				{
					bool changed = false;
					for (const auto &e : edges)
						if (layer[static_cast<size_t>(e.second)] < layer[static_cast<size_t>(e.first)] + 1)
						{
							layer[static_cast<size_t>(e.second)] = layer[static_cast<size_t>(e.first)] + 1;
							changed = true;
						}
					if (!changed)
						break;
				}
				// The post-process chain runs after the passes: the first
				// effect takes the lighting output, but that target is not
				// always in the capture, so the chain is pinned after the
				// pass layers.
				int passLayerCount = 0;
				for (size_t i = 0; i < passCount; ++i)
					passLayerCount = std::max(passLayerCount, layer[i] + 1);
				for (size_t ci = 0; ci < cap.chain.size(); ++ci)
					layer[passCount + ci] = passLayerCount + static_cast<int>(ci);

				const int layerCount = *std::max_element(layer.begin(), layer.end()) + 1;
				std::vector<std::vector<int>> layers(layerCount);
				for (size_t i = 0; i < nodeCount; ++i)
					layers[static_cast<size_t>(layer[i])].push_back(static_cast<int>(i));

				// Order within a layer by the average position of the
				// neighbours in the previous layer, then in the next.
				std::vector<std::vector<int>> order = layers;
				for (int pass = 0; pass < 4; ++pass)
				{
					const bool forward = (pass % 2) == 0;
					for (int step = 0; step < layerCount; ++step)
					{
						const int li = forward ? step : layerCount - 1 - step;
						// The neighbours in the adjacent layer decide the order.
						const int neighbourLayer = forward ? li - 1 : li + 1;
						std::vector<int> &row = order[static_cast<size_t>(li)];
						std::vector<double> bary(row.size(), 0.0);
						for (size_t i = 0; i < row.size(); ++i)
						{
							double sum = 0.0;
							int count = 0;
							for (const auto &e : edges)
							{
								const int other = forward ? e.first : e.second;
								const int self = forward ? e.second : e.first;
								if (self != row[i] || layer[static_cast<size_t>(other)] != neighbourLayer)
									continue;
								const auto &otherRow = order[static_cast<size_t>(neighbourLayer)];
								const auto it = std::find(otherRow.begin(), otherRow.end(), other);
								if (it == otherRow.end())
									continue;
								sum += static_cast<double>(it - otherRow.begin());
								++count;
							}
							bary[i] = count > 0 ? sum / count : static_cast<double>(i);
						}
						std::vector<int> sorted(row);
						std::stable_sort(sorted.begin(), sorted.end(), [&](int a, int b) {
							const double ba = bary[static_cast<size_t>(std::find(row.begin(), row.end(), a) - row.begin())];
							const double bb = bary[static_cast<size_t>(std::find(row.begin(), row.end(), b) - row.begin())];
							return ba < bb;
						});
						row = sorted;
					}
				}

				// Column positions, then the rows inside them.
				std::vector<float> columnX(layerCount, 0.0f);
				float columnWidth = 0.0f;
				float contentWidth = 0.0f;
				for (int li = 0; li < layerCount; ++li)
				{
					columnX[static_cast<size_t>(li)] = columnWidth;
					float widest = 0.0f;
					for (int node : order[static_cast<size_t>(li)])
						widest = std::max(widest, widths[static_cast<size_t>(node)]);
					columnWidth += widest + LayoutGap;
					contentWidth = std::max(contentWidth, columnWidth - LayoutGap);
				}

				float tallest = 0.0f;
				for (int li = 0; li < layerCount; ++li)
				{
					float height = 0.0f;
					for (int node : order[static_cast<size_t>(li)])
						height += heights[static_cast<size_t>(node)] + LayoutGap;
					tallest = std::max(tallest, height - LayoutGap);
				}

				for (int li = 0; li < layerCount; ++li)
				{
					float y = 0.0f;
					for (int node : order[static_cast<size_t>(li)])
					{
						g_NodePositions.push_back({NodeIdOf(cap, node),
							ImVec2(columnX[static_cast<size_t>(li)], y)});
						y += heights[static_cast<size_t>(node)] + LayoutGap;
					}
				}

				return ImVec2(contentWidth, tallest);
			}

			// The graph pans by moving its nodes, which keeps the pan rate in
			// world units (the editor exposes no view offset of its own).
			void ApplyNodePositions()
			{
				for (const NodePosition &node : g_NodePositions)
					ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(node.id), node.pos);
			}

			// Moves the visible rect, in world units.
			void PanView(const ImVec2 &worldDelta)
			{
				auto *ctx = EditorContext();
				const ImRect view = ctx->GetViewRect();
				ctx->NavigateTo(ImRect(view.Min - worldDelta, view.Max - worldDelta), false, 0.0f);
			}

			// Puts the origin of the graph back at the top left of the canvas.
			void ResetView()
			{
				if (g_Editor == nullptr)
					return;
				PanView(EditorContext()->GetViewRect().Min);
			}

			// Re-runs the layout and drops the pan.
			void FormatGraph(Capture &cap)
			{
				g_ContentSize = LayoutGraph(cap);
				ApplyNodePositions();
				// The nodes are back at the origin, so the view goes with them.
				ResetView();
				g_FitAfterDraw = true;
			}

			void RenderGraphEditor(Capture &cap, const ImVec2 &size, const TrackpadGesture &gesture)
			{
				// The editor rescales the view when the canvas resizes, which
				// shifts the graph, so it is created once the panel size has
				// settled.
				if (g_Editor == nullptr)
				{
					if (size.x != g_LastCanvasSize.x || size.y != g_LastCanvasSize.y)
					{
						g_LastCanvasSize = size;
						g_SettleFrames = 0;
						return;
					}
					if (++g_SettleFrames < 2)
						return;
					EnsureEditor();
					g_NeedsLayout = true;
				}
				ax::NodeEditor::SetCurrentEditor(g_Editor);

				// The canvas enters a local coordinate space, so the pointer
				// and the canvas origin are read before that.
				const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
				const ImVec2 pointer = ImGui::GetIO().MousePos;
				const bool overCanvas = pointer.x >= canvasOrigin.x && pointer.x < canvasOrigin.x + size.x &&
					pointer.y >= canvasOrigin.y && pointer.y < canvasOrigin.y + size.y;
				const bool canvasClicked = overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

				// The editor zooms on the wheel, so a trackpad scroll is taken
				// over by the pan below and the wheel is suppressed.
				float savedWheel = ImGui::GetIO().MouseWheel;
				bool suppressWheel = false;
				if (overCanvas && (gesture.scrollX != 0.0f || gesture.scrollY != 0.0f))
				{
					// A viewport pan: the view moves, the graph does not.
					auto *ctx = EditorContext();
					const float invZoom = 1.0f / std::max(ctx->GetView().Scale(), 0.01f);
					PanView(ImVec2(gesture.scrollX * invZoom, gesture.scrollY * invZoom));
					suppressWheel = true;
				}
				if (gesture.magnify != 0.0f)
				{
					// A pinch is fed to the editor as a wheel event, which is
					// what its zoom reacts to. The gesture is in wheel units.
					ImGui::GetIO().AddMouseWheelEvent(0.0f, gesture.magnify);
				}

				ax::NodeEditor::Begin("gpu_profiler_graph", size);

				if (g_NeedsLayout)
				{
					g_ContentSize = LayoutGraph(cap);
					ApplyNodePositions();
					g_NeedsLayout = false;
					g_FitAfterDraw = true;
				}

				for (size_t pi = 0; pi < cap.passes.size(); ++pi)
				{
					const PassEntry &pass = cap.passes[pi];
					std::vector<std::string> lines;
					lines.push_back(pass.name);
					lines.insert(lines.end(), pass.inputLabels.begin(), pass.inputLabels.end());
					lines.insert(lines.end(), pass.outputLabels.begin(), pass.outputLabels.end());
					const float width = NodeContentWidth(lines);

					ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(PassNodeBase + pi));
					ImGui::TextUnformatted(pass.name.c_str());
					for (size_t ii = 0; ii < pass.inputLabels.size(); ++ii)
						DrawInputPin(PassInPinBase + pi * PinStride + ii, pass.inputLabels[ii]);
					for (size_t oi = 0; oi < pass.outputLabels.size(); ++oi)
						DrawOutputPin(PassOutPinBase + pi * PinStride + oi, pass.outputLabels[oi], width);
					ax::NodeEditor::EndNode();
				}

				for (size_t ci = 0; ci < cap.chain.size(); ++ci)
				{
					const ChainEntry &chain = cap.chain[ci];
					std::vector<std::string> lines;
					lines.push_back(chain.name);
					lines.push_back(chain.shader);
					lines.insert(lines.end(), chain.inputLabels.begin(), chain.inputLabels.end());
					const float width = NodeContentWidth(lines);

					ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(ChainNodeBase + ci));
					ImGui::TextUnformatted(chain.name.c_str());
					ImGui::TextDisabled("%s", chain.shader.c_str());
					for (size_t ii = 0; ii < chain.inputLabels.size(); ++ii)
						DrawInputPin(ChainInPinBase + ci * PinStride + ii, chain.inputLabels[ii]);
					DrawOutputPin(ChainOutPinBase + ci * PinStride, "chain out ->", width);
					ax::NodeEditor::EndNode();
				}

				for (const LinkRec &link : cap.links)
				{
					ax::NodeEditor::Link(ax::NodeEditor::LinkId(link.id),
						ax::NodeEditor::PinId(static_cast<uintptr_t>(link.startPin)),
						ax::NodeEditor::PinId(static_cast<uintptr_t>(link.endPin)),
						ImVec4(0.9f, 0.9f, 0.9f, 1.0f), 1.5f);
				}

				if (g_FitAfterDraw)
				{
					// The nodes only have a size once they have been drawn.
					g_FitAfterDraw = false;
					g_FitPending = true;
				}

				if (g_FitPending && size.x == g_LastCanvasSize.x && size.y == g_LastCanvasSize.y)
				{
					// Fit once the nodes have a size and the canvas stopped
					// resizing: the editor rescales the view when the canvas
					// size changes, which would undo the fit.
					g_FitPending = false;
					const float fitScale = std::min(size.x / std::max(g_ContentSize.x, 1.0f),
						size.y / std::max(g_ContentSize.y, 1.0f));
					// A wider graph stays at 1:1, anchored on the first node.
					if (fitScale >= MinFitScale)
						ax::NodeEditor::NavigateToContent(0.0f);
				}
				g_LastCanvasSize = size;

				if (suppressWheel)
					ImGui::GetIO().MouseWheel = 0.0f;

				ax::NodeEditor::End();

				if (suppressWheel)
					ImGui::GetIO().MouseWheel = savedWheel;

				SyncEditorSelection(cap, canvasClicked);
			}

			void RenderTextureTable(Capture &cap, const ImVec2 &size)
			{
				const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
					ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
					ImGuiTableFlags_SizingStretchProp;
				if (!ImGui::BeginTable("gpu_profiler_textures", 5, flags, size))
					return;
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 150.0f);
				ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 80.0f);
				ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 70.0f);
				ImGui::TableSetupColumn("Slices", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 45.0f);
				ImGui::TableSetupColumn("Mean", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 55.0f);
				ImGui::TableHeadersRow();
				for (size_t i = 0; i < cap.textures.size(); ++i)
				{
					const TexEntry &tex = cap.textures[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (ImGui::Selectable(tex.name.c_str(), g_SelectedTex == static_cast<int>(i)))
					{
						g_SelectedTex = static_cast<int>(i);
						g_SelectedPass = -1;
						g_SelectedChain = -1;
						g_TextureFromNode = false;
					}
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%dx%d", tex.w, tex.h);
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(tex.format.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%d", tex.slices);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%.3f", tex.stats[2]);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			// Main panel takes the tab minus the details pane and the gap
			// SameLine inserts between them.
			ImVec2 MainPanelSize(const ImVec2 &avail)
			{
				const float gap = ImGui::GetStyle().ItemSpacing.x;
				return ImVec2(std::max(120.0f, avail.x - DetailsWidth - gap), avail.y);
			}

			void ShowCaptureTooltip()
			{
				if (!ImGui::BeginItemTooltip())
					return;
				if (g_Capture)
				{
					ImGui::TextUnformatted(g_Capture->dir.c_str());
					ImGui::Text("frame %d  backend %s  %dx%d", g_Capture->frameIndex,
						g_Capture->backend.c_str(), g_Capture->sizeW, g_Capture->sizeH);
					ImGui::Text("%zu passes  %zu textures  %zu chain", g_Capture->passes.size(),
						g_Capture->textures.size(), g_Capture->chain.size());
				}
				else
				{
					ImGui::TextUnformatted(g_DirBuf);
					ImGui::TextDisabled("no capture loaded");
				}
				ImGui::EndTooltip();
			}
		}

		void OpenGpuProfilerWindow(const std::string &captureDir)
		{
			g_WindowOpen = true;
			g_OpenDir = captureDir;
			std::snprintf(g_DirBuf, sizeof(g_DirBuf), "%s", captureDir.c_str());
			g_Capture.reset();
			g_LoadError.clear();
			g_SelectedPass = -1;
			g_SelectedChain = -1;
			g_SelectedTex = -1;
			g_TextureFromNode = false;
			g_EditorNodeSel = 0;
			g_EditorLinkSel = 0;
			g_NeedsLayout = false;
			g_FitAfterDraw = false;
			g_FitPending = false;
			g_LastCanvasSize = ImVec2(0.0f, 0.0f);
			g_ContentSize = ImVec2(0.0f, 0.0f);
			g_SettleFrames = 0;
			// A fresh editor drops the nodes of the previous capture.
			DestroyEditor();
			if (!captureDir.empty())
			{
				auto cap = std::make_unique<Capture>();
				if (LoadCapture(captureDir, *cap))
				{
					g_NeedsLayout = true;
					g_Capture = std::move(cap);
				}
				else
				{
					g_LoadError = "Failed to load capture from '" + captureDir + "'";
				}
			}
		}

		void RenderGpuProfilerWindow()
		{
			// Drained every frame, so the gestures do not pile up while the
			// window is closed or the graph is not drawn.
			const TrackpadGesture gesture = PollTrackpadGesture();

			if (!g_WindowOpen)
				return;

			ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
			bool open = true;
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
			if (!ImGui::Begin("GPU Profiler", &open, flags))
			{
				ImGui::End();
				if (!open)
				{
					g_WindowOpen = false;
					g_Capture.reset();
					g_LoadError.clear();
					DestroyEditor();
				}
				return;
			}

			ImGui::SetNextItemWidth(DirFieldWidth);
			if (ImGui::InputTextWithHint("##capture_dir", "capture directory", g_DirBuf, sizeof(g_DirBuf),
					ImGuiInputTextFlags_EnterReturnsTrue))
				OpenGpuProfilerWindow(g_DirBuf);
			ShowCaptureTooltip();
			ImGui::SameLine();
			if (ImGui::Button("Load..."))
			{
				nfdu8filteritem_t filter[] = { { "GPU capture graph", "json" } };
				nfdu8char_t *picked = nullptr;
				const std::string initialDir = g_Capture ? g_Capture->dir : g_OpenDir;
				if (NFD_OpenDialogU8(&picked, filter, 1, initialDir.empty() ? nullptr : initialDir.c_str()) == NFD_OKAY && picked)
				{
					OpenGpuProfilerWindow(std::filesystem::path(picked).parent_path().string());
					NFD_FreePathU8(picked);
				}
				ImGui::GetIO().ClearInputKeys();
			}
			ImGui::SameLine();
			if (ImGui::Button("Refresh") && g_Capture)
				OpenGpuProfilerWindow(g_Capture->dir);

			if (g_Capture)
			{
				if (ImGui::BeginTabBar("gpu_profiler_tabs"))
				{
					if (ImGui::BeginTabItem("Render Graph"))
					{
						const ImVec2 avail = ImGui::GetContentRegionAvail();
						RenderGraphEditor(*g_Capture, MainPanelSize(avail), gesture);
						ImGui::SameLine();
						RenderDetailsPane(*g_Capture, "gpu_profiler_details",
							ImVec2(DetailsWidth, avail.y), true);
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Textures"))
					{
						const ImVec2 avail = ImGui::GetContentRegionAvail();
						RenderTextureTable(*g_Capture, MainPanelSize(avail));
						ImGui::SameLine();
						RenderDetailsPane(*g_Capture, "gpu_profiler_tex_details",
							ImVec2(DetailsWidth, avail.y), false);
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
			}
			else if (!g_LoadError.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_LoadError.c_str());
			}
			else
			{
				ImGui::TextDisabled("Enter a capture directory and press Load.");
			}

			ImGui::End();
			if (!open)
			{
				g_WindowOpen = false;
				g_Capture.reset();
				g_LoadError.clear();
				DestroyEditor();
			}
		}

		void ShutdownGpuProfilerWindow()
		{
			g_Capture.reset();
			DestroyEditor();
			g_WindowOpen = false;
		}
	}
}

#endif // WITH_EDITOR
