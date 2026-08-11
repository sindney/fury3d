#if WITH_EDITOR

#include "Fury/Editor/EditorSkyWindow.h"

#include <string>
#include <unordered_map>

#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorAssetPicker.h"
#include "Fury/SceneNode.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/Texture.h"
#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			std::unordered_map<std::string, std::weak_ptr<SceneNode>> g_OpenSkyEditors;
		}

		void OpenSkyEditor(const std::shared_ptr<SceneNode> &node)
		{
			if (!node || !node->GetComponent<SkyAtmosphere>())
				return;
			g_OpenSkyEditors[node->GetUUID()] = node;
		}

		void RenderAllOpenSkyEditors()
		{
			for (auto it = g_OpenSkyEditors.begin(); it != g_OpenSkyEditors.end();)
			{
				auto node = it->second.lock();
				bool open = (node != nullptr);
				if (open)
				{
					ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
					const std::string title = "Sky Atmosphere: " + node->GetName() + "##" + it->first;
					if (ImGui::Begin(title.c_str(), &open))
					{
						auto sky = node->GetComponent<SkyAtmosphere>();
						if (!sky)
						{
							open = false;
						}
						else
						{
							if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen))
							{
								float sunI = sky->GetSunIntensityParam();
								if (ImGui::DragFloat("Sun Intensity", &sunI, 0.05f, 0.0f, 50.0f))
								{
									sky->SetSunIntensity(sunI);
									Editor::MarkSceneDirty();
								}
								float discI = sky->GetSunDiscIntensity();
								if (ImGui::DragFloat("Disc Intensity", &discI, 0.1f, 0.0f, 100.0f))
								{
									sky->SetSunDiscIntensity(discI);
									Editor::MarkSceneDirty();
								}
								float angR = sky->GetSunAngularRadius();
								if (ImGui::DragFloat("Angular Radius (rad)", &angR, 0.0005f, 0.001f, 0.05f, "%.4f"))
								{
									sky->SetSunAngularRadius(angR);
									Editor::MarkSceneDirty();
								}
							}

							if (ImGui::CollapsingHeader("Clouds"))
							{
								bool clouds = sky->GetCloudsEnabled();
								if (ImGui::Checkbox("Enabled##clouds", &clouds))
								{
									sky->SetCloudsEnabled(clouds);
									Editor::MarkSceneDirty();
								}
								float cov = sky->GetCloudCoverage();
								if (ImGui::SliderFloat("Coverage", &cov, 0.0f, 1.0f))
								{
									sky->SetCloudCoverage(cov);
									Editor::MarkSceneDirty();
								}
								float alt = sky->GetCloudAltitudeKm();
								if (ImGui::DragFloat("Altitude (km)", &alt, 0.01f, 0.02f, 10.0f))
								{
									sky->SetCloudAltitudeKm(alt);
									Editor::MarkSceneDirty();
								}
								float thick = sky->GetCloudThicknessKm();
								if (ImGui::DragFloat("Thickness (km)", &thick, 0.01f, 0.02f, 5.0f))
								{
									sky->SetCloudThicknessKm(thick);
									Editor::MarkSceneDirty();
								}
								float scale = sky->GetCloudScale();
								if (ImGui::DragFloat("Noise Scale", &scale, 0.01f, 0.05f, 5.0f))
								{
									sky->SetCloudScale(scale);
									Editor::MarkSceneDirty();
								}
								float dens = sky->GetCloudDensity();
								if (ImGui::DragFloat("Density", &dens, 0.5f, 1.0f, 100.0f))
								{
									sky->SetCloudDensity(dens);
									Editor::MarkSceneDirty();
								}
								float wind = sky->GetCloudWindSpeed();
								if (ImGui::DragFloat("Wind (cm/s)", &wind, 10.0f, 0.0f, 20000.0f))
								{
									sky->SetCloudWindSpeed(wind);
									Editor::MarkSceneDirty();
								}
								float fadeKm = sky->GetCloudFadeKm();
								if (ImGui::DragFloat("Fade Distance (km)", &fadeKm, 0.1f, 0.5f, 20.0f))
								{
									sky->SetCloudFadeKm(fadeKm);
									Editor::MarkSceneDirty();
								}
								RenderLinkedTextureRow("Noise Texture", "se_cloud_noise",
									sky->GetCloudNoisePath(), sky->GetCloudNoiseTexture(),
									[sky](std::shared_ptr<Texture> tex) {
										sky->SetCloudNoisePath(tex ? tex->GetFilePath() : "");
										Editor::MarkSceneDirty();
									});
							}

							if (ImGui::CollapsingHeader("Moon"))
							{
								bool moon = sky->GetMoonEnabled();
								if (ImGui::Checkbox("Enabled##moon", &moon))
								{
									sky->SetMoonEnabled(moon);
									Editor::MarkSceneDirty();
								}
								float moonI = sky->GetMoonIntensity();
								if (ImGui::DragFloat("Intensity##moon", &moonI, 0.01f, 0.0f, 5.0f))
								{
									sky->SetMoonIntensity(moonI);
									Editor::MarkSceneDirty();
								}
								float moonR = sky->GetMoonAngularRadius();
								if (ImGui::DragFloat("Angular Radius (rad)##moon", &moonR, 0.0005f, 0.001f, 0.05f, "%.4f"))
								{
									sky->SetMoonAngularRadius(moonR);
									Editor::MarkSceneDirty();
								}
								RenderLinkedTextureRow("Moon Texture", "se_moon_tex",
									sky->GetMoonTexturePath(), sky->GetMoonTexture(),
									[sky](std::shared_ptr<Texture> tex) {
										sky->SetMoonTexturePath(tex ? tex->GetFilePath() : "");
										Editor::MarkSceneDirty();
									});
							}

							if (ImGui::CollapsingHeader("Atmosphere"))
							{
								Vector4 ray = sky->GetRayleighScattering();
								float rayCols[3] = { ray.x * 1000.0f, ray.y * 1000.0f, ray.z * 1000.0f };
								if (ImGui::DragFloat3("Rayleigh x1e-3", rayCols, 0.1f, 0.0f, 100.0f))
								{
									sky->SetRayleighScattering(Vector4(rayCols[0] * 0.001f, rayCols[1] * 0.001f, rayCols[2] * 0.001f, 0.0f));
									Editor::MarkSceneDirty();
								}
								float mie = sky->GetMieScattering() * 1000.0f;
								if (ImGui::DragFloat("Mie x1e-3", &mie, 0.05f, 0.0f, 50.0f))
								{
									sky->SetMieScattering(mie * 0.001f);
									Editor::MarkSceneDirty();
								}
								float g = sky->GetMieG();
								if (ImGui::SliderFloat("Mie Phase g", &g, 0.0f, 0.95f))
								{
									sky->SetMieG(g);
									Editor::MarkSceneDirty();
								}
								Vector4 ga = sky->GetGroundAlbedo();
								float gaCols[3] = { ga.x, ga.y, ga.z };
								if (ImGui::ColorEdit3("Ground Albedo", gaCols))
								{
									sky->SetGroundAlbedo(Vector4(gaCols[0], gaCols[1], gaCols[2], 0.0f));
									Editor::MarkSceneDirty();
								}
								float apRange = sky->GetApRangeKm();
								if (ImGui::DragFloat("AP Range (km)", &apRange, 0.5f, 1.0f, 128.0f))
								{
									sky->SetApRangeKm(apRange);
									Editor::MarkSceneDirty();
								}
							}
						}
						ImGui::End();
					}
				}

				if (!open)
					it = g_OpenSkyEditors.erase(it);
				else
					++it;
			}
		}
	}
}

#endif // WITH_EDITOR
