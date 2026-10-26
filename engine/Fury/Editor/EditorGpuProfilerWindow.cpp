#if WITH_EDITOR

#include "Fury/Editor/EditorGpuProfilerWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui-node-editor/imgui_node_editor.h>
#include <rapidjson/document.h>

#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Texture.h"
#include "ImGui/imgui.h"
#include "nfd.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			// Pin/node id encoding: disjoint uintptr ranges per id kind.
			constexpr uintptr_t PassNodeBase = 1;            // + pass index
			constexpr uintptr_t ChainNodeBase = 100000;      // + chain index
			constexpr uintptr_t PassInPinBase = 1000000;     // + pass*4096 + input
			constexpr uintptr_t PassOutPinBase = 2000000;    // + pass*4096 + output
			constexpr uintptr_t ChainInPinBase = 3000000;    // + chain*4096 + input

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

				// Lazy preview; upload is render-thread queued, so GetID()
				// lands a frame or two after CreateFromImage.
				Texture::Ptr preview;

				int previewSlice = 0;
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
			};

			struct ChainEntry
			{
				std::string name;

				std::string shader;

				std::vector<std::string> inputs;
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

			bool g_WindowOpen = false;

			std::string g_LoadError;

			std::unique_ptr<Capture> g_Capture;

			ax::NodeEditor::EditorContext *g_Editor = nullptr;

			bool g_NeedsLayout = false;

			int g_SelectedPass = -1;

			int g_SelectedChain = -1;

			int g_SelectedTex = -1;

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
						const uintptr_t startPin = PassOutPinBase + pi * 4096 + oi;
						bool consumed = false;
						for (size_t qi = 0; qi < cap.passes.size(); ++qi)
						{
							const auto &in = cap.passes[qi].inputs;
							const auto ii = std::find(in.begin(), in.end(), pass.outputs[oi]);
							if (qi == pi || ii == in.end())
								continue;
							consumed = true;
							cap.links.push_back({nextId++, texIdx, static_cast<int>(startPin),
								static_cast<int>(PassInPinBase + qi * 4096 + (ii - in.begin()))});
						}
						for (size_t ci = 0; ci < cap.chain.size(); ++ci)
						{
							const auto &in = cap.chain[ci].inputs;
							const auto ii = std::find(in.begin(), in.end(), pass.outputs[oi]);
							if (ii == in.end())
								continue;
							consumed = true;
							cap.links.push_back({nextId++, texIdx, static_cast<int>(startPin),
								static_cast<int>(ChainInPinBase + ci * 4096 + (ii - in.begin()))});
						}
						(void)consumed;
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

			void EnsureEditor()
			{
				if (g_Editor != nullptr)
					return;
				ax::NodeEditor::Config config;
				config.SettingsFile = nullptr;
				g_Editor = ax::NodeEditor::CreateEditor(&config);
			}

			Texture::Ptr LoadPreview(const std::string &dir, const std::string &relPath, const std::string &name)
			{
				Texture::Ptr tex = Texture::Create(name);
				tex->CreateFromImage(dir + "/" + relPath, false, false);
				return tex;
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
				ImGui::Image((ImTextureID)(intptr_t)id, ImVec2(w * scale, h * scale));
			}

			// Pass a 0..1 float through a gamma so linear gbuffer dumps read
			// correctly on screen.
			float Gamma(float v)
			{
				return std::pow(std::max(v, 0.0f), 1.0f / 2.2f);
			}

			void RenderDetailsPane(Capture &cap)
			{
				ImGui::BeginChild("gpu_profiler_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

				if (g_SelectedTex >= 0 && g_SelectedTex < static_cast<int>(cap.textures.size()))
				{
					TexEntry &tex = cap.textures[static_cast<size_t>(g_SelectedTex)];
					ImGui::TextUnformatted(tex.name.c_str());
					ImGui::Text("%dx%d x%d slices", tex.w, tex.h, tex.slices);
					ImGui::Text("format: %s (%s)", tex.format.c_str(), tex.type.c_str());
					ImGui::Text("stats (lin): min %.4f max %.4f mean %.4f", tex.stats[0], tex.stats[1], tex.stats[2]);
					ImGui::Text("stats (srgb): min %.2f max %.2f mean %.2f",
						Gamma(tex.stats[0]), Gamma(tex.stats[1]), Gamma(tex.stats[2]));
					if (tex.pngs.empty())
					{
						ImGui::TextDisabled("no PNG readback for this texture");
					}
					else
					{
						if (tex.pngs.size() > 1)
						{
							if (ImGui::SliderInt("slice", &tex.previewSlice, 0, static_cast<int>(tex.pngs.size()) - 1))
								tex.preview.reset();
						}
						const int slice = std::min(tex.previewSlice, static_cast<int>(tex.pngs.size()) - 1);
						if (!tex.preview && !tex.pngs.empty())
							tex.preview = LoadPreview(cap.dir, tex.pngs[static_cast<size_t>(slice)], "GpuProfiler_" + tex.name);
						DrawPreview(tex.preview, 280.0f, 280.0f);
					}
				}
				else if (g_SelectedPass >= 0 && g_SelectedPass < static_cast<int>(cap.passes.size()))
				{
					const PassEntry &pass = cap.passes[static_cast<size_t>(g_SelectedPass)];
					ImGui::TextUnformatted(pass.name.c_str());
					ImGui::Text("renderIndex: %d", pass.renderIndex);
					ImGui::Text("clearMode: %s", pass.clearMode.c_str());
					ImGui::ColorEdit4("clearColor", const_cast<float *>(pass.clearColor),
						ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker);
					ImGui::Text("depthTest: %s", pass.depthTest.c_str());
					ImGui::Text("blend: %s", pass.blend.c_str());
					ImGui::Text("cull: %s", pass.cull.c_str());
					ImGui::Separator();
					ImGui::TextUnformatted("Shaders:");
					for (const std::string &s : pass.shaders)
						ImGui::BulletText("%s", s.c_str());
					if (ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen))
						for (const std::string &s : pass.outputs)
						{
							auto it = cap.texByName.find(s);
							if (ImGui::Selectable(s.c_str(), false) && it != cap.texByName.end())
								g_SelectedTex = static_cast<int>(it->second);
						}
					if (ImGui::CollapsingHeader("Inputs"))
						for (const std::string &s : pass.inputs)
						{
							auto it = cap.texByName.find(s);
							if (ImGui::Selectable(s.c_str(), false) && it != cap.texByName.end())
								g_SelectedTex = static_cast<int>(it->second);
						}
				}
				else if (g_SelectedChain >= 0 && g_SelectedChain < static_cast<int>(cap.chain.size()))
				{
					const ChainEntry &chain = cap.chain[static_cast<size_t>(g_SelectedChain)];
					ImGui::TextUnformatted(chain.name.c_str());
					ImGui::TextWrapped("shader: %s", chain.shader.c_str());
					ImGui::Separator();
					ImGui::TextUnformatted("Inputs:");
					for (const std::string &s : chain.inputs)
					{
						auto it = cap.texByName.find(s);
						if (ImGui::Selectable(s.c_str(), false) && it != cap.texByName.end())
							g_SelectedTex = static_cast<int>(it->second);
					}
				}
				else
				{
					ImGui::TextDisabled("Select a node or link.");
					if (ImGui::CollapsingHeader("Backbuffer", ImGuiTreeNodeFlags_DefaultOpen))
					{
						if (cap.hasBackbuffer)
						{
							if (!cap.backbufferPreview)
								cap.backbufferPreview = LoadPreview(cap.dir, "backbuffer.png", "GpuProfiler_backbuffer");
							DrawPreview(cap.backbufferPreview, 280.0f, 200.0f);
						}
						else
						{
							ImGui::TextDisabled("backbuffer.png missing");
						}
					}
				}

				ImGui::EndChild();
			}

			void RenderGraphEditor(Capture &cap)
			{
				EnsureEditor();
				ax::NodeEditor::SetCurrentEditor(g_Editor);

				const float detailsWidth = 310.0f;
				const ImVec2 avail = ImGui::GetContentRegionAvail();
				ax::NodeEditor::Begin("gpu_profiler_graph",
					ImVec2(std::max(100.0f, avail.x - detailsWidth), avail.y));

				if (g_NeedsLayout)
				{
					// Columns ordered by renderIndex (passes were sorted at
					// load); chain effects get a row beneath the last column.
					float x = 0.0f;
					float y = 0.0f;
					int inColumn = 0;
					for (size_t pi = 0; pi < cap.passes.size(); ++pi)
					{
						if (inColumn >= 6)
						{
							x += 320.0f;
							y = 0.0f;
							inColumn = 0;
						}
						ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(PassNodeBase + pi),
							ImVec2(x, y));
						y += 140.0f;
						++inColumn;
					}
					for (size_t ci = 0; ci < cap.chain.size(); ++ci)
						ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(ChainNodeBase + ci),
							ImVec2(80.0f + static_cast<float>(ci) * 260.0f, y + 80.0f));
					g_NeedsLayout = false;
					ax::NodeEditor::NavigateToContent(0.0f);
				}

				ax::NodeEditor::ClearSelection();
				for (size_t pi = 0; pi < cap.passes.size(); ++pi)
				{
					const PassEntry &pass = cap.passes[pi];
					const bool selected = static_cast<int>(pi) == g_SelectedPass;
					if (selected)
						ax::NodeEditor::SelectNode(ax::NodeEditor::NodeId(PassNodeBase + pi));
					ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(PassNodeBase + pi));
					ImGui::TextUnformatted(pass.name.c_str());
					for (size_t ii = 0; ii < pass.inputs.size(); ++ii)
					{
						ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(PassInPinBase + pi * 4096 + ii),
							ax::NodeEditor::PinKind::Input);
						ImGui::Text("-> %s", pass.inputs[ii].c_str());
						ax::NodeEditor::EndPin();
					}
					for (size_t oi = 0; oi < pass.outputs.size(); ++oi)
					{
						ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(PassOutPinBase + pi * 4096 + oi),
							ax::NodeEditor::PinKind::Output);
						ImGui::Text("%s ->", pass.outputs[oi].c_str());
						ax::NodeEditor::EndPin();
					}
					ax::NodeEditor::EndNode();
				}

				for (size_t ci = 0; ci < cap.chain.size(); ++ci)
				{
					const ChainEntry &chain = cap.chain[ci];
					const bool selected = static_cast<int>(ci) == g_SelectedChain;
					if (selected)
						ax::NodeEditor::SelectNode(ax::NodeEditor::NodeId(ChainNodeBase + ci));
					ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(ChainNodeBase + ci));
					ImGui::TextUnformatted(chain.name.c_str());
					ImGui::TextDisabled("%s", chain.shader.c_str());
					for (size_t ii = 0; ii < chain.inputs.size(); ++ii)
					{
						ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(ChainInPinBase + ci * 4096 + ii),
							ax::NodeEditor::PinKind::Input);
						ImGui::Text("-> %s", chain.inputs[ii].c_str());
						ax::NodeEditor::EndPin();
					}
					ax::NodeEditor::EndNode();
				}

				for (const LinkRec &link : cap.links)
				{
					ax::NodeEditor::Link(ax::NodeEditor::LinkId(link.id),
						ax::NodeEditor::PinId(static_cast<uintptr_t>(link.startPin)),
						ax::NodeEditor::PinId(static_cast<uintptr_t>(link.endPin)),
						ImVec4(0.9f, 0.9f, 0.9f, 1.0f), 1.5f);
				}

				// Read back selection after submission.
				ax::NodeEditor::NodeId nodeIds[4];
				const int nodeCount = ax::NodeEditor::GetSelectedNodes(nodeIds, 4);
				ax::NodeEditor::LinkId linkIds[4];
				const int linkCount = ax::NodeEditor::GetSelectedLinks(linkIds, 4);
				if (nodeCount == 1)
				{
					const uintptr_t id = nodeIds[0].Get();
					if (id >= ChainNodeBase)
					{
						g_SelectedChain = static_cast<int>(id - ChainNodeBase);
						g_SelectedPass = -1;
					}
					else
					{
						g_SelectedPass = static_cast<int>(id - PassNodeBase);
						g_SelectedChain = -1;
					}
					g_SelectedTex = -1;
				}
				int texFromLink = -1;
				if (linkCount == 1)
				{
					const uintptr_t id = linkIds[0].Get();
					for (const LinkRec &link : cap.links)
						if (link.id == id)
							texFromLink = link.texIdx;
				}
				if (const uintptr_t dbl = ax::NodeEditor::GetDoubleClickedLink().Get())
					for (const LinkRec &link : cap.links)
						if (link.id == dbl)
							texFromLink = link.texIdx;
				if (texFromLink >= 0)
				{
					g_SelectedTex = texFromLink;
					g_SelectedPass = -1;
					g_SelectedChain = -1;
				}
				if (ax::NodeEditor::IsBackgroundClicked())
				{
					g_SelectedPass = -1;
					g_SelectedChain = -1;
					g_SelectedTex = -1;
				}

				ax::NodeEditor::End();
			}

			void RenderTexturesTab(Capture &cap)
			{
				ImGui::Text("%zu textures", cap.textures.size());
				ImGui::Separator();
				for (size_t i = 0; i < cap.textures.size(); ++i)
				{
					TexEntry &tex = cap.textures[i];
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(tex.name.c_str(), static_cast<int>(i) == g_SelectedTex,
							ImGuiSelectableFlags_AllowOverlap))
					{
						g_SelectedTex = static_cast<int>(i);
						g_SelectedPass = -1;
						g_SelectedChain = -1;
					}
					ImGui::SameLine();
					ImGui::TextDisabled("%dx%d", tex.w, tex.h);
					if (static_cast<int>(i) == g_SelectedTex)
					{
						ImGui::Indent();
						ImGui::Text("format %s, %d slices", tex.format.c_str(), tex.slices);
						ImGui::Text("stats (lin): %.4f .. %.4f, mean %.4f",
							tex.stats[0], tex.stats[1], tex.stats[2]);
						if (!tex.pngs.empty())
						{
							if (tex.pngs.size() > 1 && ImGui::SliderInt("slice", &tex.previewSlice, 0,
									static_cast<int>(tex.pngs.size()) - 1))
								tex.preview.reset();
							const int slice = std::min(tex.previewSlice, static_cast<int>(tex.pngs.size()) - 1);
							if (!tex.preview)
								tex.preview = LoadPreview(cap.dir, tex.pngs[static_cast<size_t>(slice)],
									"GpuProfiler_" + tex.name);
							DrawPreview(tex.preview, 320.0f, 320.0f);
						}
						else
						{
							ImGui::TextDisabled("no PNG readback");
						}
						ImGui::Unindent();
					}
					ImGui::PopID();
				}
			}
		}

		void OpenGpuProfilerWindow(const std::string &captureDir)
		{
			g_WindowOpen = true;
			g_OpenDir = captureDir;
			g_Capture.reset();
			g_LoadError.clear();
			g_SelectedPass = -1;
			g_SelectedChain = -1;
			g_SelectedTex = -1;
			g_NeedsLayout = false;
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
			if (!g_WindowOpen)
				return;

			ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
			bool open = true;
			if (!ImGui::Begin("GPU Profiler", &open))
			{
				ImGui::End();
				if (!open)
				{
					g_WindowOpen = false;
					g_Capture.reset();
					g_LoadError.clear();
				}
				return;
			}

			char dirBuf[512];
			std::snprintf(dirBuf, sizeof(dirBuf), "%s", g_Capture ? g_Capture->dir.c_str() : g_OpenDir.c_str());
			if (ImGui::InputTextWithHint("Capture dir", "--gpu-dump output directory", dirBuf, sizeof(dirBuf),
					ImGuiInputTextFlags_EnterReturnsTrue))
				OpenGpuProfilerWindow(dirBuf);
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
			if (ImGui::Button("Reload") && g_Capture)
				OpenGpuProfilerWindow(g_Capture->dir);

			if (g_Capture)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("frame %d, %s, %dx%d", g_Capture->frameIndex,
					g_Capture->backend.c_str(), g_Capture->sizeW, g_Capture->sizeH);
			}
			else if (!g_LoadError.empty())
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_LoadError.c_str());
			}

			if (g_Capture)
			{
				if (ImGui::BeginTabBar("gpu_profiler_tabs"))
				{
					if (ImGui::BeginTabItem("Render Graph"))
					{
						RenderGraphEditor(*g_Capture);
						ImGui::SameLine();
						RenderDetailsPane(*g_Capture);
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Textures"))
					{
						RenderTexturesTab(*g_Capture);
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
			}

			ImGui::End();
			if (!open)
			{
				g_WindowOpen = false;
				g_Capture.reset();
				g_LoadError.clear();
			}
		}

		void ShutdownGpuProfilerWindow()
		{
			g_Capture.reset();
			if (g_Editor != nullptr)
			{
				ax::NodeEditor::DestroyEditor(g_Editor);
				g_Editor = nullptr;
			}
			g_WindowOpen = false;
		}
	}
}

#endif // WITH_EDITOR
