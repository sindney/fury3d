#if WITH_EDITOR

#include "Fury/Editor/EditorTerrainWindow.h"

#include <cstring>
#include <string>
#include <unordered_map>

#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorAssetPicker.h"
#include "Fury/Heightmap.h"
#include "Fury/Material.h"
#include "Fury/SceneNode.h"
#include "Fury/Terrain.h"
#include "Fury/Texture.h"
#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			std::unordered_map<std::string, std::weak_ptr<SceneNode>> g_OpenTerrainEditors;
		}

		void OpenTerrainEditor(const std::shared_ptr<SceneNode> &node)
		{
			if (!node || !node->GetComponent<Terrain>())
				return;
			g_OpenTerrainEditors[node->GetUUID()] = node;
		}

		void RenderAllOpenTerrainEditors()
		{
			for (auto it = g_OpenTerrainEditors.begin(); it != g_OpenTerrainEditors.end();)
			{
				auto node = it->second.lock();
				bool open = (node != nullptr);
				if (open)
				{
					ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
					const std::string title = "Terrain: " + node->GetName() + "##" + it->first;
					if (ImGui::Begin(title.c_str(), &open))
					{
						auto terrain = node->GetComponent<Terrain>();
						if (!terrain)
						{
							open = false;
						}
						else
						{
							// heightmap asset row (Change/jump/clear idiom)
							const bool hasHm = !terrain->GetHeightmapName().empty();
							if (ImGui::Button("Change##hm"))
								ImGui::OpenPopup("TerrainEditorHmPicker");
							ImGui::SameLine();
							if (!hasHm) ImGui::BeginDisabled();
							if (ImGui::Button("->##hm"))
								Editor::SelectAssetInBrowser(typeid(Heightmap), terrain->GetHeightmapName());
							if (!hasHm) ImGui::EndDisabled();
							ImGui::SameLine();
							if (!hasHm) ImGui::BeginDisabled();
							if (ImGui::Button("x##hm"))
							{
								terrain->SetHeightmapName("");
								Editor::MarkSceneDirty();
							}
							if (!hasHm) ImGui::EndDisabled();
							ImGui::SameLine();
							ImGui::TextUnformatted("Heightmap:");
							ImGui::SameLine();
							if (hasHm)
								ImGui::TextUnformatted(terrain->GetHeightmapName().c_str());
							else
								ImGui::TextDisabled("(none)");

							RenderAssetPickerModal("TerrainEditorHmPicker", "Pick Heightmap",
								typeid(Heightmap),
								[terrain](std::shared_ptr<void> p) {
									auto hm = std::static_pointer_cast<Heightmap>(p);
									terrain->SetHeightmapName(hm ? hm->GetName() : "");
									terrain->Rebuild();
									Editor::MarkSceneDirty();
								});

							if (terrain->HasHeights())
							{
								ImGui::Text("%d x %d texels, %.0f x %.0f cm, height %.0f cm",
									terrain->GetResolution(), terrain->GetResolution(),
									terrain->GetWorldSizeX(), terrain->GetWorldSizeZ(),
									terrain->GetHeightScale());
							}

							ImGui::Separator();

							auto mat = terrain->GetMaterial();
							RenderLinkedTextureRow("Splatmap", "te_splat",
								terrain->GetSplatmapPath(),
								mat ? mat->GetTexture("u_splat_map") : nullptr,
								[terrain](std::shared_ptr<Texture> tex) {
									terrain->SetSplatmapPath(tex ? tex->GetFilePath() : "");
									terrain->ReloadTextures();
									Editor::MarkSceneDirty();
								});

							for (int i = 0; i < 4; i++)
							{
								ImGui::PushID(i);
								auto &layer = terrain->GetLayer(i);
								if (ImGui::TreeNode("Layer", "Layer %d (%s)", i, layer.Name.c_str()))
								{
									RenderLinkedTextureRow("Texture",
										("te_layer" + std::to_string(i)).c_str(),
										layer.TexturePath,
										mat ? mat->GetTexture("u_layer" + std::to_string(i)) : nullptr,
										[terrain, i](std::shared_ptr<Texture> tex) {
											terrain->GetLayer(i).TexturePath = tex ? tex->GetFilePath() : "";
											terrain->ReloadTextures();
											Editor::MarkSceneDirty();
										});
									if (ImGui::DragFloat("Tiling (cm)", &layer.TilingCm, 10.0f, 10.0f, 20000.0f))
										Editor::MarkSceneDirty();
									ImGui::TreePop();
								}
								ImGui::PopID();
							}

							ImGui::Separator();

							int chunks = terrain->GetChunkCount();
							if (ImGui::DragInt("Chunks per side", &chunks, 0.5f, 1, 64))
							{
								terrain->SetChunkCount(chunks);
								Editor::MarkSceneDirty();
							}
							int lods = terrain->GetLodCount();
							if (ImGui::DragInt("LOD count", &lods, 0.5f, 1, 5))
							{
								terrain->SetLodCount(lods);
								Editor::MarkSceneDirty();
							}
							ImGui::TextDisabled("chunk/LOD changes apply on Rebuild");

							if (ImGui::Button("Rebuild", ImVec2(-1.0f, 0.0f)))
							{
								terrain->Rebuild();
								Editor::MarkSceneDirty();
							}

							// live height probe
							static float probeX = 0.0f, probeZ = 0.0f;
							ImGui::SetNextItemWidth(100.0f);
							ImGui::InputFloat("X##probe", &probeX);
							ImGui::SameLine();
							ImGui::SetNextItemWidth(100.0f);
							ImGui::InputFloat("Z##probe", &probeZ);
							ImGui::SameLine();
							ImGui::Text("h = %.1f cm", terrain->GetHeight(probeX, probeZ));
						}
						ImGui::End();
					}
				}

				if (!open)
					it = g_OpenTerrainEditors.erase(it);
				else
					++it;
			}
		}
	}
}

#endif // WITH_EDITOR
