#include "Fury/SkyAtmosphere.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <mutex>

#include "Fury/Engine.h"
#include "Fury/EntityManager.h"
#include "Fury/FileUtil.h"
#include "Fury/FramePacket.h"
#include "Fury/GLLoader.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshUtil.h"
#include "Fury/Pass.h"
#include "Fury/Profiler.h"
#include "Fury/ProfilerGpu.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"

namespace fury
{
	namespace
	{
		std::weak_ptr<SkyAtmosphere> s_ActiveSky;

		// Guards sky LUT texture publishes: EnsureResources runs on the GL
		// thread, SnapshotRenderTextures reads on the game thread.
		std::mutex s_SkyResourceMutex;

		const char* kShaderDir = "Resource/Shader/Atmosphere/";

		// load-or-reuse a project texture and register it in the scene's
		// EntityManager (name == path convention) so the editor's texture
		// picker can offer it back
		std::shared_ptr<Texture> LoadSkyTexture(const std::string &path, bool srgb)
		{
			if (path.empty() || Scene::Active == nullptr)
				return nullptr;
			auto em = Scene::Active->GetEntityManager();
			if (em)
			{
				if (auto existing = em->Get<Texture>(path))
					if (existing->GetFilePath() == path)
						return existing;
			}
			auto tex = Texture::Create(path);
			tex->CreateFromImage(path, srgb, true);
			// a failed load must not come back as a live object: binding a
			// dirty texture silently aliases the sampler to unit 0's texture
			if (!tex->IsContentValid())
				return nullptr;
			if (em)
				em->Add(tex);
			return tex;
		}

		// --- cloud noise disk cache -------------------------------------------
		// Layout: magic, hash, then per texture {w, h, d, channels, bytes}.

		const std::uint32_t kCloudNoiseMagic = 0x434C4E31;   // 'CLN1'

		std::uint64_t CloudNoiseHash(float weatherBias, float weatherTypeContrast)
		{
			std::uint64_t h = 1469598103934665603ull;
			auto mix = [&h](const void* data, size_t len)
			{
				const unsigned char* p = static_cast<const unsigned char*>(data);
				for (size_t i = 0; i < len; i++)
				{
					h ^= p[i];
					h *= 1099511628211ull;
				}
			};
			std::uint32_t version = 1;
			mix(&version, sizeof(version));
			mix(&weatherBias, sizeof(weatherBias));
			mix(&weatherTypeContrast, sizeof(weatherTypeContrast));
			return h;
		}

		std::string CloudNoiseCachePath(std::uint64_t hash)
		{
			char name[64];
			snprintf(name, sizeof(name), "Cache/cloud_noise_%016llx.bin", (unsigned long long)hash);
			return FileUtil::GetAbsPath() + name;
		}

		bool WriteCacheTex(std::ofstream &out, const std::shared_ptr<Texture> &tex)
		{
			std::vector<unsigned char> pixels;
			if (!tex->GetPixels(pixels))
				return false;
			std::uint32_t dims[4] = { (std::uint32_t)tex->GetWidth(), (std::uint32_t)tex->GetHeight(),
				(std::uint32_t)tex->GetDepth(), (std::uint32_t)(pixels.size() /
					((size_t)tex->GetWidth() * tex->GetHeight() * std::max(tex->GetDepth(), 1))) };
			out.write(reinterpret_cast<const char*>(dims), sizeof(dims));
			out.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
			return out.good();
		}

		std::shared_ptr<Texture> ReadCacheTex(std::ifstream &in, const char* name, TextureType type)
		{
			std::uint32_t dims[4];
			in.read(reinterpret_cast<char*>(dims), sizeof(dims));
			if (!in.good())
				return nullptr;
			auto tex = Texture::Create(name);
			TextureFormat fmt = dims[3] == 3 ? TextureFormat::RGB8 : TextureFormat::RGBA8;
			tex->CreateEmpty((int)dims[0], (int)dims[1], (int)dims[2], fmt, type, false);
			std::vector<unsigned char> pixels((size_t)dims[0] * dims[1] * std::max<int>((int)dims[2], 1) * dims[3]);
			in.read(reinterpret_cast<char*>(pixels.data()), pixels.size());
			if (!in.good())
				return nullptr;
			if (type == TextureType::TEXTURE_3D)
				tex->SetPixels3D(pixels.data());
			else
				tex->SetPixels(pixels.data());
			tex->GenerateMipMap();
			tex->SetFilterMode(FilterMode::LINEAR_MIPMAP_LINEAR);
			return tex;
		}
	}

	SkyAtmosphere::Ptr SkyAtmosphere::Create()
	{
		return std::make_shared<SkyAtmosphere>();
	}

	SkyAtmosphere::Ptr SkyAtmosphere::GetActive()
	{
		if (auto ptr = s_ActiveSky.lock())
			return ptr;

		// fallback: scan the active scene for any sky component
		if (Scene::Active == nullptr)
			return nullptr;

		std::shared_ptr<SceneNode> found;
		std::function<void(const std::shared_ptr<SceneNode>&)> walk =
			[&](const std::shared_ptr<SceneNode> &node)
		{
			if (found || node == nullptr)
				return;
			if (auto sky = node->GetComponent<SkyAtmosphere>())
			{
				if (sky->GetEnabled())
					found = node;
				return;
			}
			for (unsigned int i = 0; i < node->GetChildCount(); i++)
				walk(node->GetChildAt(i));
		};
		walk(Scene::Active->GetRootNode());

		if (found)
		{
			s_ActiveSky = found->GetComponent<SkyAtmosphere>();
			return s_ActiveSky.lock();
		}
		return nullptr;
	}

	SkyAtmosphere::SkyAtmosphere()
	{
		m_TypeIndex = typeid(SkyAtmosphere);
	}

	SkyAtmosphere::~SkyAtmosphere()
	{
		Unsubscribe();
	}

	Component::Ptr SkyAtmosphere::Clone() const
	{
		auto ptr = Create();
		*ptr = *this;
		// runtime GL state must not be shared with the source component
		ptr->m_ResourcesCreated = false;
		ptr->m_StaticDirty = true;
		ptr->m_UpdateKey = 0;
		ptr->m_TransmittanceLut = ptr->m_MultiScatterLut = ptr->m_SkyViewLut = nullptr;
		ptr->m_CameraVolume = ptr->m_CloudTarget = nullptr;
		ptr->m_CloudBaseNoise = ptr->m_CloudDetailNoise = ptr->m_WeatherMap = nullptr;
		ptr->m_CirrusNoise = nullptr;
		ptr->m_TransmittancePass = ptr->m_MultiScatterPass = ptr->m_SkyViewPass = nullptr;
		ptr->m_CameraVolumePass = ptr->m_CloudPass = nullptr;
		ptr->m_TransmittanceShader = ptr->m_MultiScatterShader = ptr->m_SkyViewShader = nullptr;
		ptr->m_CameraVolumeShader = ptr->m_CloudShader = ptr->m_NoiseGenShader = nullptr;
		ptr->m_CloudTargetW = ptr->m_CloudTargetH = 0;
		ptr->m_CloudTargetQuality = -1;
		ptr->m_LastCloudFov = -1.0f;
		ptr->m_LastCloudSunY = -999.0f;
		ptr->m_CloudParamsDirty = true;
		ptr->m_LastCloudKey = CloudRenderKey();
		return ptr;
	}

	bool SkyAtmosphere::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "SkyAtmosphere: json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "SkyAtmosphere")
		{
			FURYE << "SkyAtmosphere: invalid type " << str << "!";
			return false;
		}

		LoadMemberValue(wrapper, "enabled", m_Enabled);
		LoadMemberValue(wrapper, "bottom_radius", m_BottomRadiusKm);
		LoadMemberValue(wrapper, "top_radius", m_TopRadiusKm);
		LoadMemberValue(wrapper, "rayleigh_scat", m_RayleighScat);
		LoadMemberValue(wrapper, "rayleigh_exp_scale", m_RayleighExpScale);
		LoadMemberValue(wrapper, "mie_scat", m_MieScat);
		LoadMemberValue(wrapper, "mie_ext", m_MieExt);
		LoadMemberValue(wrapper, "mie_exp_scale", m_MieExpScale);
		LoadMemberValue(wrapper, "mie_g", m_MieG);
		LoadMemberValue(wrapper, "ozone_ext", m_OzoneExt);
		LoadMemberValue(wrapper, "ozone_center", m_OzoneCenterKm);
		LoadMemberValue(wrapper, "ozone_width", m_OzoneWidthKm);
		LoadMemberValue(wrapper, "ground_albedo", m_GroundAlbedo);
		LoadMemberValue(wrapper, "sun_angular_radius", m_SunAngularRadius);
		LoadMemberValue(wrapper, "sun_disc_intensity", m_SunDiscIntensity);
		LoadMemberValue(wrapper, "sun_intensity", m_SunIntensity);
		LoadMemberValue(wrapper, "ap_range_km", m_ApRangeKm);
		LoadMemberValue(wrapper, "moon_enabled", m_MoonEnabled);
		LoadMemberValue(wrapper, "moon_angular_radius", m_MoonAngularRadius);
		LoadMemberValue(wrapper, "moon_intensity", m_MoonIntensity);
		LoadMemberValue(wrapper, "moon_texture", m_MoonTexturePath);
		LoadMemberValue(wrapper, "clouds_enabled", m_CloudsEnabled);
		LoadMemberValue(wrapper, "cloud_coverage", m_CloudCoverage);
		LoadMemberValue(wrapper, "cloud_alt_km", m_CloudAltKm);
		LoadMemberValue(wrapper, "cloud_thick_km", m_CloudThickKm);
		LoadMemberValue(wrapper, "cloud_scale", m_CloudScale);
		LoadMemberValue(wrapper, "cloud_density", m_CloudDensity);
		LoadMemberValue(wrapper, "cloud_wind_speed", m_CloudWindSpeedCm);
		LoadMemberValue(wrapper, "cloud_fade_km", m_CloudFadeKm);
		LoadMemberValue(wrapper, "cloud_noise_texture", m_CloudNoisePath);
		LoadMemberValue(wrapper, "cloud_type_bias", m_CloudTypeBias);
		LoadMemberValue(wrapper, "cloud_detail_scale", m_CloudDetailScale);
		LoadMemberValue(wrapper, "cloud_erosion", m_CloudErosion);
		LoadMemberValue(wrapper, "cloud_powder", m_CloudPowder);
		LoadMemberValue(wrapper, "cloud_hg_g", m_CloudHgG);
		LoadMemberValue(wrapper, "cloud_hg_g_fwd", m_CloudHgGFwd);
		LoadMemberValue(wrapper, "cloud_hg_blend", m_CloudHgBlend);
		LoadMemberValue(wrapper, "cloud_ambient_scale", m_CloudAmbientScale);
		LoadMemberValue(wrapper, "cloud_quality", m_CloudQuality);
		LoadMemberValue(wrapper, "cloud_debug_mode", m_CloudDebugMode);
		LoadMemberValue(wrapper, "cloud_weather_bias", m_CloudWeatherBias);
		LoadMemberValue(wrapper, "cloud_weather_type_contrast", m_CloudWeatherTypeContrast);
		LoadMemberValue(wrapper, "cirrus_enabled", m_CirrusEnabled);
		LoadMemberValue(wrapper, "cirrus_coverage", m_CirrusCoverage);
		LoadMemberValue(wrapper, "cirrus_alt_km", m_CirrusAltKm);
		LoadMemberValue(wrapper, "cirrus_scale", m_CirrusScale);
		LoadMemberValue(wrapper, "cirrus_density", m_CirrusDensity);
		LoadMemberValue(wrapper, "time_hours", m_TimeHours);
		LoadMemberValue(wrapper, "day_length_minutes", m_DayLengthMinutes);
		LoadMemberValue(wrapper, "auto_advance", m_AutoAdvance);
		LoadMemberValue(wrapper, "sun_from_tod", m_SunFromTod);
		LoadMemberValue(wrapper, "sun_light_name", m_SunLightName);

		m_StaticDirty = true;
		return true;
	}

	void SkyAtmosphere::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		SaveKey(wrapper, "type");
		SaveValue(wrapper, "SkyAtmosphere");

		SaveKey(wrapper, "enabled");            SaveValue(wrapper, m_Enabled);
		SaveKey(wrapper, "bottom_radius");      SaveValue(wrapper, m_BottomRadiusKm);
		SaveKey(wrapper, "top_radius");         SaveValue(wrapper, m_TopRadiusKm);
		SaveKey(wrapper, "rayleigh_scat");      SaveValue(wrapper, m_RayleighScat);
		SaveKey(wrapper, "rayleigh_exp_scale"); SaveValue(wrapper, m_RayleighExpScale);
		SaveKey(wrapper, "mie_scat");           SaveValue(wrapper, m_MieScat);
		SaveKey(wrapper, "mie_ext");            SaveValue(wrapper, m_MieExt);
		SaveKey(wrapper, "mie_exp_scale");      SaveValue(wrapper, m_MieExpScale);
		SaveKey(wrapper, "mie_g");              SaveValue(wrapper, m_MieG);
		SaveKey(wrapper, "ozone_ext");          SaveValue(wrapper, m_OzoneExt);
		SaveKey(wrapper, "ozone_center");       SaveValue(wrapper, m_OzoneCenterKm);
		SaveKey(wrapper, "ozone_width");        SaveValue(wrapper, m_OzoneWidthKm);
		SaveKey(wrapper, "ground_albedo");      SaveValue(wrapper, m_GroundAlbedo);
		SaveKey(wrapper, "sun_angular_radius"); SaveValue(wrapper, m_SunAngularRadius);
		SaveKey(wrapper, "sun_disc_intensity"); SaveValue(wrapper, m_SunDiscIntensity);
		SaveKey(wrapper, "sun_intensity");      SaveValue(wrapper, m_SunIntensity);
		SaveKey(wrapper, "ap_range_km");        SaveValue(wrapper, m_ApRangeKm);
		SaveKey(wrapper, "moon_enabled");       SaveValue(wrapper, m_MoonEnabled);
		SaveKey(wrapper, "moon_angular_radius"); SaveValue(wrapper, m_MoonAngularRadius);
		SaveKey(wrapper, "moon_intensity");     SaveValue(wrapper, m_MoonIntensity);
		SaveKey(wrapper, "moon_texture");       SaveValue(wrapper, m_MoonTexturePath);
		SaveKey(wrapper, "clouds_enabled");     SaveValue(wrapper, m_CloudsEnabled);
		SaveKey(wrapper, "cloud_coverage");     SaveValue(wrapper, m_CloudCoverage);
		SaveKey(wrapper, "cloud_alt_km");       SaveValue(wrapper, m_CloudAltKm);
		SaveKey(wrapper, "cloud_thick_km");     SaveValue(wrapper, m_CloudThickKm);
		SaveKey(wrapper, "cloud_scale");        SaveValue(wrapper, m_CloudScale);
		SaveKey(wrapper, "cloud_density");      SaveValue(wrapper, m_CloudDensity);
		SaveKey(wrapper, "cloud_wind_speed");   SaveValue(wrapper, m_CloudWindSpeedCm);
		SaveKey(wrapper, "cloud_fade_km");      SaveValue(wrapper, m_CloudFadeKm);
		SaveKey(wrapper, "cloud_noise_texture"); SaveValue(wrapper, m_CloudNoisePath);
		SaveKey(wrapper, "cloud_type_bias");    SaveValue(wrapper, m_CloudTypeBias);
		SaveKey(wrapper, "cloud_detail_scale"); SaveValue(wrapper, m_CloudDetailScale);
		SaveKey(wrapper, "cloud_erosion");      SaveValue(wrapper, m_CloudErosion);
		SaveKey(wrapper, "cloud_powder");       SaveValue(wrapper, m_CloudPowder);
		SaveKey(wrapper, "cloud_hg_g");         SaveValue(wrapper, m_CloudHgG);
		SaveKey(wrapper, "cloud_hg_g_fwd");     SaveValue(wrapper, m_CloudHgGFwd);
		SaveKey(wrapper, "cloud_hg_blend");     SaveValue(wrapper, m_CloudHgBlend);
		SaveKey(wrapper, "cloud_ambient_scale"); SaveValue(wrapper, m_CloudAmbientScale);
		SaveKey(wrapper, "cloud_quality");      SaveValue(wrapper, m_CloudQuality);
		SaveKey(wrapper, "cloud_debug_mode");   SaveValue(wrapper, m_CloudDebugMode);
		SaveKey(wrapper, "cloud_weather_bias"); SaveValue(wrapper, m_CloudWeatherBias);
		SaveKey(wrapper, "cloud_weather_type_contrast"); SaveValue(wrapper, m_CloudWeatherTypeContrast);
		SaveKey(wrapper, "cirrus_enabled");     SaveValue(wrapper, m_CirrusEnabled);
		SaveKey(wrapper, "cirrus_coverage");    SaveValue(wrapper, m_CirrusCoverage);
		SaveKey(wrapper, "cirrus_alt_km");      SaveValue(wrapper, m_CirrusAltKm);
		SaveKey(wrapper, "cirrus_scale");       SaveValue(wrapper, m_CirrusScale);
		SaveKey(wrapper, "cirrus_density");     SaveValue(wrapper, m_CirrusDensity);
		SaveKey(wrapper, "time_hours");         SaveValue(wrapper, m_TimeHours);
		SaveKey(wrapper, "day_length_minutes"); SaveValue(wrapper, m_DayLengthMinutes);
		SaveKey(wrapper, "auto_advance");       SaveValue(wrapper, m_AutoAdvance);
		SaveKey(wrapper, "sun_from_tod");       SaveValue(wrapper, m_SunFromTod);
		SaveKey(wrapper, "sun_light_name");     SaveValue(wrapper, m_SunLightName);

		if (object)
			EndObject(wrapper);
	}

	void SkyAtmosphere::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		if (m_Enabled)
			s_ActiveSky = std::static_pointer_cast<SkyAtmosphere>(node->GetComponent(typeid(SkyAtmosphere)));
		if (m_AutoAdvance)
			Subscribe();
	}

	void SkyAtmosphere::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Unsubscribe();
		if (auto active = s_ActiveSky.lock(); active && active.get() == this)
			s_ActiveSky.reset();
		Component::OnDetaching(node);
	}

	void SkyAtmosphere::OnOwnerDestructing(SceneNode &node)
	{
		Unsubscribe();
		if (auto active = s_ActiveSky.lock(); active && active.get() == this)
			s_ActiveSky.reset();
		Component::OnOwnerDestructing(node);
	}

	void SkyAtmosphere::Subscribe()
	{
		if (m_UpdateKey != 0)
			return;
		auto node = m_Owner.lock();
		if (!node)
			return;
		auto self = std::static_pointer_cast<SkyAtmosphere>(node->GetComponent(typeid(SkyAtmosphere)));
		if (self)
			m_UpdateKey = Engine::OnUpdate->Connect(self, &SkyAtmosphere::TickUpdate);
	}

	void SkyAtmosphere::Unsubscribe()
	{
		if (m_UpdateKey != 0)
		{
			Engine::OnUpdate->Disconnect(m_UpdateKey);
			m_UpdateKey = 0;
		}
	}

	void SkyAtmosphere::SetTimeHours(float h)
	{
		m_TimeHours = h;
		while (m_TimeHours < 0.0f) m_TimeHours += 24.0f;
		while (m_TimeHours >= 24.0f) m_TimeHours -= 24.0f;
	}

	void SkyAtmosphere::SetAutoAdvance(bool v)
	{
		m_AutoAdvance = v;
		if (v) Subscribe();
		else Unsubscribe();
	}

	void SkyAtmosphere::TickUpdate(float dt)
	{
		if (m_AutoAdvance && m_DayLengthMinutes > 0.0f)
		{
			SetTimeHours(m_TimeHours + dt * 24.0f / (m_DayLengthMinutes * 60.0f));
		}
		m_WindOffsetKm.x += m_CloudWindSpeedCm * dt * 1e-5f;
	}

	void SkyAtmosphere::EvaluateSunAndLight()
	{
		// parametric sun path: 6h sunrise, 12h peak (70 deg), 18h sunset
		const float elevDeg = sinf((m_TimeHours - 6.0f) / 24.0f * 2.0f * MathUtil::PI) * 70.0f;
		const float azimRad = (m_TimeHours - 12.0f) * 15.0f * MathUtil::DegToRad;
		const float elevRad = elevDeg * MathUtil::DegToRad;
		m_SunDir = Vector4(cosf(elevRad) * sinf(azimRad), sinf(elevRad), -cosf(elevRad) * cosf(azimRad), 0.0f);
		m_SunDir.Normalize();

		// moon rides the opposite side of the day cycle
		const float moonElevRad = -elevRad;
		const float moonAzimRad = azimRad + MathUtil::PI;
		m_MoonDir = Vector4(cosf(moonElevRad) * sinf(moonAzimRad), sinf(moonElevRad),
			-cosf(moonElevRad) * cosf(moonAzimRad), 0.0f);
		m_MoonDir.Normalize();

		// daylight ramps through twilight
		m_Daylight = std::min(1.0f, std::max(0.0f, (elevDeg + 2.0f) / 8.0f));

		// warm at low sun, white at high sun
		const float warm = 1.0f - std::min(1.0f, std::max(0.0f, elevDeg / 30.0f));
		m_SunColor = Color(1.0f, 1.0f - warm * 0.45f, 1.0f - warm * 0.7f, 1.0f);
		m_SunIntensitySky = m_SunIntensity;
		m_SunIntensityCur = m_SunIntensity * m_Daylight;

		auto lightNode = ResolveSunLight();
		if (lightNode == nullptr)
			return;

		if (m_SunFromTod)
		{
			// drive the light: world rotation mapping (0,-1,0) onto -sunDir,
			// converted to local via parent inverse + Decompose (scaled
			// ancestors pollute the piecewise getters)
			Vector4 lightTravel = m_SunDir * -1.0f;
			Vector4 from(0.0f, -1.0f, 0.0f, 0.0f);
			Vector4 axis = from.CrossProduct(lightTravel);
			float axisLen = axis.Length();
			Quaternion worldQ;
			if (axisLen < 1e-5f)
			{
				// parallel or anti-parallel: identity, or 180 deg around X
				worldQ = ((from * lightTravel) > 0.0f)
					? Quaternion() : MathUtil::AxisRadToQuat(Vector4(1.0f, 0.0f, 0.0f, 0.0f), MathUtil::PI);
			}
			else
			{
				float angle = acosf(std::min(1.0f, std::max(-1.0f, from * lightTravel)));
				worldQ = MathUtil::AxisRadToQuat(axis * (1.0f / axisLen), angle);
			}

			Matrix4 worldM;
			worldM.Identity();
			worldM.AppendRotation(worldQ);

			Matrix4 parentWorld;
			parentWorld.Identity();
			if (auto parent = lightNode->GetParent())
				parentWorld = parent->GetWorldMatrix();

			Matrix4 local = parentWorld.Inverse() * worldM;
			Vector4 localPos, ignoredScale;
			Quaternion localRot;
			MathUtil::Decompose(local, localPos, localRot, ignoredScale);
			lightNode->SetLocalRoattion(localRot);
			lightNode->Recompose(false);

			if (auto light = lightNode->GetComponent<Light>())
			{
				light->SetColor(m_SunColor);
				light->SetIntensity(m_SunIntensityCur);
			}
		}
		else
		{
			// sky follows the light
			Matrix4 world = lightNode->GetWorldMatrix();
			Vector4 lightDir = world.Multiply(Vector4(0.0f, -1.0f, 0.0f, 0.0f));
			lightDir.Normalize();
			m_SunDir = lightDir * -1.0f;
			if (auto light = lightNode->GetComponent<Light>())
			{
				m_SunColor = light->GetColor();
				m_SunIntensityCur = light->GetIntensity();
				m_SunIntensitySky = light->GetIntensity();
			}
			m_Daylight = std::min(1.0f, std::max(0.0f, m_SunDir.y * 4.0f + 0.15f));
		}
	}

	bool SkyAtmosphere::AutoSelectSunLight()
	{
		// clear the name so ResolveSunLight takes the first-directional
		// fallback, then pin whatever it finds
		std::string prev = m_SunLightName;
		m_SunLightName.clear();
		auto node = ResolveSunLight();
		if (!node)
		{
			m_SunLightName = prev;
			return false;
		}
		m_SunLightName = node->GetName();
		return true;
	}

	std::shared_ptr<SceneNode> SkyAtmosphere::ResolveSunLight() const
	{
		if (Scene::Active == nullptr)
			return nullptr;
		auto root = Scene::Active->GetRootNode();
		if (root == nullptr)
			return nullptr;

		if (!m_SunLightName.empty())
		{
			if (auto node = root->FindChildRecursively(m_SunLightName))
			{
				if (node->GetComponent<Light>() != nullptr)
					return node;
				FURYW << "SkyAtmosphere: sun light node '" << m_SunLightName << "' has no Light";
			}
		}

		// fallback: first directional light in the scene
		std::shared_ptr<SceneNode> found;
		std::function<void(const std::shared_ptr<SceneNode>&)> walk =
			[&](const std::shared_ptr<SceneNode> &node)
		{
			if (found || node == nullptr)
				return;
			if (auto light = node->GetComponent<Light>())
			{
				if (light->GetType() == LightType::DIRECTIONAL)
					found = node;
				return;
			}
			for (unsigned int i = 0; i < node->GetChildCount(); i++)
				walk(node->GetChildAt(i));
		};
		walk(root);
		return found;
	}

	void SkyAtmosphere::BindAtmosphereUniforms(const std::shared_ptr<Shader> &shader, const SkyParams &params) const
	{
		shader->BindFloat("u_bottom_radius", params.bottomRadiusKm);
		shader->BindFloat("u_top_radius", params.topRadiusKm);
		shader->BindFloat("u_rayleigh_scat", params.rayleighScat.x, params.rayleighScat.y, params.rayleighScat.z);
		shader->BindFloat("u_rayleigh_density_exp_scale", params.rayleighExpScale);
		shader->BindFloat("u_mie_scat", params.mieScat);
		shader->BindFloat("u_mie_ext", params.mieExt);
		shader->BindFloat("u_mie_density_exp_scale", params.mieExpScale);
		shader->BindFloat("u_mie_g", params.mieG);
		shader->BindFloat("u_ozone_ext", params.ozoneExt.x, params.ozoneExt.y, params.ozoneExt.z);
		shader->BindFloat("u_ozone_center_km", params.ozoneCenterKm);
		shader->BindFloat("u_ozone_width_km", params.ozoneWidthKm);
		shader->BindFloat("u_ground_albedo", params.groundAlbedo.x, params.groundAlbedo.y, params.groundAlbedo.z);
		shader->BindFloat("u_sun_dir", params.sunDir.x, params.sunDir.y, params.sunDir.z);
		shader->BindFloat("u_sun_color", params.sunColor.r, params.sunColor.g, params.sunColor.b);
		shader->BindFloat("u_sun_intensity", params.sunIntensitySky);
		shader->BindFloat("u_view_height", params.viewHeightKm);
	}

	bool SkyAtmosphere::EnsureResources()
	{
		if (m_ResourcesCreated)
			return true;
		if (_ptrc_glGenTextures == nullptr)
			return false;   // headless (fury exec): no GL

		// Members are published under the mutex: the game thread copies
		// them into frame packets while this runs on the GL thread.
		std::lock_guard<std::mutex> resourceLock(s_SkyResourceMutex);

		auto makeLut = [](const char* name, int w, int h, int d, TextureType type)
		{
			auto tex = Texture::Create(name);
			tex->CreateEmpty(w, h, d, TextureFormat::RGBA16F, type, false);
			tex->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
			return tex;
		};

		m_TransmittanceLut = makeLut("sky_transmittance", 256, 64, 0, TextureType::TEXTURE_2D);
		m_MultiScatterLut = makeLut("sky_multiscatter", 32, 32, 0, TextureType::TEXTURE_2D);
		m_SkyViewLut = makeLut("sky_view", 192, 108, 0, TextureType::TEXTURE_2D);
		m_CameraVolume = makeLut("sky_camera_volume", 96, 54, 32, TextureType::TEXTURE_3D);
		// cloud target is RT-relative: created/resized in EnsureCloudTarget

		auto makePass = [](const std::string &name, const std::shared_ptr<Texture> &target)
		{
			auto pass = Pass::Create(name);
			pass->AddTexture(target, false);
			pass->SetClearMode(ClearMode::NONE);
			pass->SetCullMode(CullMode::NONE);
			pass->SetDepthWrite(false);
			return pass;
		};

		m_TransmittancePass = makePass("sky_transmittance_pass", m_TransmittanceLut);
		m_MultiScatterPass = makePass("sky_multiscatter_pass", m_MultiScatterLut);
		m_SkyViewPass = makePass("sky_view_pass", m_SkyViewLut);
		m_CameraVolumePass = makePass("sky_camera_volume_pass", m_CameraVolume);

		std::string base = FileUtil::GetAbsPath() + kShaderDir;
		auto loadShader = [&](const char* file)
		{
			auto shader = Shader::Create(file, ShaderType::OTHER);
			if (!shader->LoadAndCompile(base + file))
				FURYE << "SkyAtmosphere: failed to compile " << file;
			return shader;
		};

		m_TransmittanceShader = loadShader("TransmittanceLut.glsl");
		m_MultiScatterShader = loadShader("MultiScatterLut.glsl");
		m_SkyViewShader = loadShader("SkyViewLut.glsl");
		m_CameraVolumeShader = loadShader("CameraVolume.glsl");
		m_CloudShader = loadShader("VolumetricClouds.glsl");
		m_NoiseGenShader = loadShader("CloudNoiseGen.glsl");

		if (!m_MoonTexturePath.empty() && m_MoonTexture == nullptr)
			m_MoonTexture = LoadSkyTexture(m_MoonTexturePath, true);
		if (!m_CloudNoisePath.empty() && m_CloudNoise == nullptr)
		{
			m_CloudNoise = LoadSkyTexture(m_CloudNoisePath, false);
			// far-field clouds minify hard; without mip filtering they alias
			// into confetti
			if (m_CloudNoise)
				m_CloudNoise->SetFilterMode(FilterMode::LINEAR_MIPMAP_LINEAR);
		}

		m_ResourcesCreated = true;
		return true;
	}

	void SkyAtmosphere::DrawLutQuad(const std::shared_ptr<Pass> &pass, const std::shared_ptr<Shader> &shader)
	{
		// caller binds the shader + uniforms first: glUniform writes the
		// currently-bound program, so the FBO bind must not precede them
		auto mesh = MeshUtil::GetUnitQuad();
		pass->Bind();
		shader->BindMesh(mesh);
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
		pass->UnBind();
	}

	void SkyAtmosphere::RenderTransmittanceLut(const SkyParams &params)
	{
		m_TransmittanceShader->Bind();
		BindAtmosphereUniforms(m_TransmittanceShader, params);
		DrawLutQuad(m_TransmittancePass, m_TransmittanceShader);
		m_TransmittanceShader->UnBind();
	}

	void SkyAtmosphere::RenderMultiScatterLut(const SkyParams &params)
	{
		m_MultiScatterShader->Bind();
		BindAtmosphereUniforms(m_MultiScatterShader, params);
		m_MultiScatterShader->BindTexture("u_transmittance_lut", m_TransmittanceLut);
		DrawLutQuad(m_MultiScatterPass, m_MultiScatterShader);
		m_MultiScatterShader->UnBind();
	}

	void SkyAtmosphere::RenderSkyViewLut(const SkyParams &params)
	{
		m_SkyViewShader->Bind();
		BindAtmosphereUniforms(m_SkyViewShader, params);
		m_SkyViewShader->BindTexture("u_transmittance_lut", m_TransmittanceLut);
		m_SkyViewShader->BindTexture("u_multiscatter_lut", m_MultiScatterLut);
		DrawLutQuad(m_SkyViewPass, m_SkyViewShader);
		m_SkyViewShader->UnBind();
	}

	void SkyAtmosphere::RenderCameraVolume(const SkyParams &params, const PacketCamera &cam)
	{
		m_CameraVolumePass->Bind();
		m_CameraVolumeShader->Bind();
		m_CameraVolumeShader->BindCameraData(cam);
		BindAtmosphereUniforms(m_CameraVolumeShader, params);
		m_CameraVolumeShader->BindTexture("u_transmittance_lut", m_TransmittanceLut);
		m_CameraVolumeShader->BindTexture("u_multiscatter_lut", m_MultiScatterLut);

		auto mesh = MeshUtil::GetUnitQuad();
		m_CameraVolumeShader->BindMesh(mesh);
		for (int slice = 0; slice < 32; slice++)
		{
			m_CameraVolumePass->SetArrayTextureLayer(slice);
			m_CameraVolumeShader->BindFloat("u_slice_id", static_cast<float>(slice));
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
		}
		m_CameraVolumeShader->UnBind();
		m_CameraVolumePass->UnBind();
	}

	bool SkyAtmosphere::EnsureCloudNoise(const SkyParams &params)
	{
		if (m_CloudBaseNoise != nullptr && m_CloudDetailNoise != nullptr && m_WeatherMap != nullptr)
			return true;
		if (_ptrc_glGetTexImage == nullptr)
			return false;

		std::lock_guard<std::mutex> resourceLock(s_SkyResourceMutex);

		const std::uint64_t hash = CloudNoiseHash(params.cloudWeatherBias, params.cloudWeatherTypeContrast);
		const std::string cachePath = CloudNoiseCachePath(hash);

		if (std::filesystem::exists(cachePath))
		{
			std::ifstream in(cachePath, std::ios::binary);
			std::uint32_t magic = 0;
			std::uint64_t fileHash = 0;
			in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
			in.read(reinterpret_cast<char*>(&fileHash), sizeof(fileHash));
			if (in.good() && magic == kCloudNoiseMagic && fileHash == hash)
			{
				m_CloudBaseNoise = ReadCacheTex(in, "cloud_base_noise", TextureType::TEXTURE_3D);
				m_CloudDetailNoise = ReadCacheTex(in, "cloud_detail_noise", TextureType::TEXTURE_3D);
				m_WeatherMap = ReadCacheTex(in, "cloud_weather_map", TextureType::TEXTURE_2D);
			}
			if (m_CloudBaseNoise && m_CloudDetailNoise && m_WeatherMap)
				return true;
			FURYW << "SkyAtmosphere: cloud noise cache unreadable, regenerating";
			m_CloudBaseNoise = m_CloudDetailNoise = m_WeatherMap = nullptr;
		}

		// generate: one fullscreen draw per 3D slice, same slice-FBO pattern
		// as the camera volume
		auto genStart = std::chrono::steady_clock::now();

		m_CloudBaseNoise = Texture::Create("cloud_base_noise");
		m_CloudBaseNoise->CreateEmpty(128, 128, 128, TextureFormat::RGBA8, TextureType::TEXTURE_3D, true);
		m_CloudDetailNoise = Texture::Create("cloud_detail_noise");
		m_CloudDetailNoise->CreateEmpty(32, 32, 32, TextureFormat::RGB8, TextureType::TEXTURE_3D, true);
		m_WeatherMap = Texture::Create("cloud_weather_map");
		m_WeatherMap->CreateEmpty(256, 256, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D, true);
		m_CirrusNoise = Texture::Create("cloud_cirrus_noise");
		m_CirrusNoise->CreateEmpty(256, 256, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D, true);

		m_NoiseGenShader->Bind();
		m_NoiseGenShader->BindFloat("u_weather_bias", params.cloudWeatherBias);
		m_NoiseGenShader->BindFloat("u_weather_type_contrast", params.cloudWeatherTypeContrast);

		auto genVolume = [&](const std::shared_ptr<Texture> &tex, int slices, int mode)
		{
			auto pass = Pass::Create("cloud_noise_gen");
			pass->AddTexture(tex, false);
			pass->SetClearMode(ClearMode::NONE);
			pass->SetCullMode(CullMode::NONE);
			pass->SetDepthWrite(false);
			pass->Bind();
			m_NoiseGenShader->BindInt("u_gen_mode", mode);
			m_NoiseGenShader->BindFloat("u_slice_count", (float)slices);
			auto mesh = MeshUtil::GetUnitQuad();
			m_NoiseGenShader->BindMesh(mesh);
			for (int slice = 0; slice < slices; slice++)
			{
				pass->SetArrayTextureLayer(slice);
				m_NoiseGenShader->BindFloat("u_slice_id", (float)slice);
				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
			}
			pass->UnBind();
		};

		genVolume(m_CloudBaseNoise, 128, 0);
		genVolume(m_CloudDetailNoise, 32, 1);

		{
			auto pass = Pass::Create("cloud_weather_gen");
			pass->AddTexture(m_WeatherMap, false);
			pass->SetClearMode(ClearMode::NONE);
			pass->SetCullMode(CullMode::NONE);
			pass->SetDepthWrite(false);
			m_NoiseGenShader->BindInt("u_gen_mode", 2);
			DrawLutQuad(pass, m_NoiseGenShader);
		}

		{
			auto pass = Pass::Create("cloud_cirrus_gen");
			pass->AddTexture(m_CirrusNoise, false);
			pass->SetClearMode(ClearMode::NONE);
			pass->SetCullMode(CullMode::NONE);
			pass->SetDepthWrite(false);
			m_NoiseGenShader->BindInt("u_gen_mode", 3);
			DrawLutQuad(pass, m_NoiseGenShader);
		}
		m_NoiseGenShader->UnBind();

		// distance-mipped sampling in the marcher needs the chains
		m_CloudBaseNoise->GenerateMipMap();
		m_CloudDetailNoise->GenerateMipMap();
		m_WeatherMap->GenerateMipMap();
		m_CirrusNoise->GenerateMipMap();
		m_CloudBaseNoise->SetFilterMode(FilterMode::LINEAR_MIPMAP_LINEAR);
		m_CloudDetailNoise->SetFilterMode(FilterMode::LINEAR_MIPMAP_LINEAR);
		m_WeatherMap->SetFilterMode(FilterMode::LINEAR_MIPMAP_LINEAR);
		m_CirrusNoise->SetFilterMode(FilterMode::LINEAR_MIPMAP_LINEAR);

		FURYD << "SkyAtmosphere: cloud noise generated in "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - genStart).count() << " ms";

		std::filesystem::create_directories(std::filesystem::path(cachePath).parent_path());
		std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(&kCloudNoiseMagic), sizeof(kCloudNoiseMagic));
		out.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
		if (!WriteCacheTex(out, m_CloudBaseNoise) || !WriteCacheTex(out, m_CloudDetailNoise) || !WriteCacheTex(out, m_WeatherMap))
			FURYW << "SkyAtmosphere: cloud noise cache write failed (regenerates next run)";

		return true;
	}

	void SkyAtmosphere::EnsureCloudTarget(int rtW, int rtH, int quality)
	{
		const int shift = quality <= 0 ? 2 : 1;
		const int w = std::max(160, rtW >> shift);
		const int h = std::max(90, rtH >> shift);
		if (m_CloudTarget && w == m_CloudTargetW && h == m_CloudTargetH && quality == m_CloudTargetQuality)
			return;

		std::lock_guard<std::mutex> resourceLock(s_SkyResourceMutex);
		m_CloudTargetW = w;
		m_CloudTargetH = h;
		m_CloudTargetQuality = quality;

		m_CloudTarget = Texture::Create("sky_clouds");
		m_CloudTarget->CreateEmpty(w, h, 0, TextureFormat::RGBA16F, TextureType::TEXTURE_2D, false);
		m_CloudTarget->SetWrapMode(WrapMode::CLAMP_TO_EDGE);

		m_CloudPass = Pass::Create("sky_cloud_pass");
		m_CloudPass->AddTexture(m_CloudTarget, false);
		m_CloudPass->SetClearMode(ClearMode::NONE);
		m_CloudPass->SetCullMode(CullMode::NONE);
		m_CloudPass->SetDepthWrite(false);

		m_CloudParamsDirty = true;   // target holds garbage until re-rendered
	}

	void SkyAtmosphere::RenderCloudTarget(const SkyParams &params, const PacketCamera &cam, const std::shared_ptr<Texture> &depthTex)
	{
		FURY_ZONE_NAMED("Clouds");
		FURY_GPU_ZONE("Clouds");

		static const int envQuality = []()
		{
			const char* q = std::getenv("FURY_CLOUD_QUALITY");
			if (q == nullptr) return -1;
			if (strcmp(q, "low") == 0) return 0;
			if (strcmp(q, "med") == 0) return 1;
			if (strcmp(q, "high") == 0) return 2;
			return -1;
		}();
		const int quality = envQuality >= 0 ? envQuality : params.cloudQuality;
		const int maxSteps = quality <= 0 ? 32 : (quality == 1 ? 48 : 96);
		const int lightSamples = quality <= 0 ? 4 : 6;

		m_CloudShader->Bind();
		BindAtmosphereUniforms(m_CloudShader, params);
		m_CloudShader->BindCameraData(cam);
		m_CloudShader->BindTexture("u_base_noise", m_CloudBaseNoise);
		m_CloudShader->BindTexture("u_detail_noise", m_CloudDetailNoise);
		m_CloudShader->BindTexture("u_weather_map", m_WeatherMap);
		m_CloudShader->BindTexture("u_cirrus_noise", m_CirrusNoise ? m_CirrusNoise : GetDummyTexture2D());
		m_CloudShader->BindTexture("u_transmittance_lut", m_TransmittanceLut);
		m_CloudShader->BindTexture("gbuffer_depth", depthTex ? depthTex : GetDummyTexture2D());
		m_CloudShader->BindFloat("u_cloud_base_km", params.cloudAltKm);
		m_CloudShader->BindFloat("u_cloud_top_km", params.cloudAltKm + params.cloudThickKm);
		m_CloudShader->BindFloat("u_cloud_coverage", params.cloudCoverage);
		m_CloudShader->BindFloat("u_cloud_type_bias", params.cloudTypeBias);
		m_CloudShader->BindFloat("u_cloud_base_scale", params.cloudScale);
		m_CloudShader->BindFloat("u_cloud_detail_scale", params.cloudDetailScale);
		m_CloudShader->BindFloat("u_cloud_erosion", params.cloudErosion);
		m_CloudShader->BindFloat("u_cloud_density", params.cloudDensity);
		m_CloudShader->BindFloat("u_wind_offset_km", params.windOffsetKm.x, params.windOffsetKm.y);
		m_CloudShader->BindFloat("u_cloud_fade_km", params.cloudFadeKm);
		m_CloudShader->BindFloat("u_detail_fade_km", params.cloudFadeKm * 0.5f);
		m_CloudShader->BindFloat("u_daylight", params.daylight);
		m_CloudShader->BindFloat("u_powder_strength", params.cloudPowder);
		m_CloudShader->BindFloat("u_hg_g", params.cloudHgG);
		m_CloudShader->BindFloat("u_hg_g_fwd", params.cloudHgGFwd);
		m_CloudShader->BindFloat("u_hg_blend", params.cloudHgBlend);
		m_CloudShader->BindFloat("u_ambient_scale", params.cloudAmbientScale);
		m_CloudShader->BindInt("u_max_steps", maxSteps);
		m_CloudShader->BindInt("u_light_samples", lightSamples);
		m_CloudShader->BindInt("u_debug_mode", params.cloudDebugMode);
		m_CloudShader->BindInt("u_cirrus_enabled", params.cirrusEnabled ? 1 : 0);
		m_CloudShader->BindFloat("u_cirrus_coverage", params.cirrusCoverage);
		m_CloudShader->BindFloat("u_cirrus_alt_km", params.cirrusAltKm);
		m_CloudShader->BindFloat("u_cirrus_scale", params.cirrusScale);
		m_CloudShader->BindFloat("u_cirrus_density", params.cirrusDensity);
		DrawLutQuad(m_CloudPass, m_CloudShader);
		m_CloudShader->UnBind();
	}

	void SkyAtmosphere::EnsureLutsRender(const SkyParams &params, const PacketCamera &cam,
		int rtW, int rtH, const std::shared_ptr<Texture> &depthTex, std::uint64_t frameIndex)
	{
		if (!EnsureResources())
			return;

		const bool skyDebug = std::getenv("FURY_SKY_DEBUG") != nullptr;

		if (params.staticDirty)
		{
			RenderTransmittanceLut(params);
			RenderMultiScatterLut(params);
			m_LastSunDir = Vector4(0, 0, 0, 0);   // force view lut refresh
			m_CloudParamsDirty = true;
			if (skyDebug)
				FURYD << "SkyAtmosphere: static LUTs rendered";
		}

		Vector4 sunDelta = params.sunDir - m_LastSunDir;
		if (sunDelta.SquareLength() > 1e-8f || std::fabs(params.viewHeightKm - m_LastViewHeightKm) > 1e-5f)
		{
			RenderSkyViewLut(params);
			m_LastSunDir = params.sunDir;
			m_LastViewHeightKm = params.viewHeightKm;
			if (skyDebug)
				FURYD << "SkyAtmosphere: sky-view LUT rendered (sun " << params.sunDir.y << ")";
		}

		if (!cam.valid)
			return;

		// The camera volume and cloud target are camera-dependent; re-render
		// only when the camera or an input changed (static frames cost zero).
		// frameIndex is part of the key guard contract: callers pass the
		// packet frame so duplicate same-camera calls within a frame skip.
		CloudRenderKey key;
		key.camPos[0] = cam.worldPos.x; key.camPos[1] = cam.worldPos.y; key.camPos[2] = cam.worldPos.z;
		key.camFwd[0] = cam.worldMatrix.Raw[2]; key.camFwd[1] = cam.worldMatrix.Raw[5];
		key.camFwd[2] = cam.worldMatrix.Raw[8]; key.camFwd[3] = cam.worldMatrix.Raw[10];
		key.fov = cam.fov;
		key.wind[0] = params.windOffsetKm.x; key.wind[1] = params.windOffsetKm.y;
		key.sunY = params.sunDir.y;
		key.params[0] = params.cloudCoverage; key.params[1] = params.cloudAltKm;
		key.params[2] = params.cloudThickKm; key.params[3] = params.cloudScale;
		key.params[4] = params.cloudDensity; key.params[5] = params.cloudFadeKm;
		key.params[6] = params.cloudTypeBias; key.params[7] = params.cloudDetailScale;
		key.params[8] = params.cloudErosion; key.params[9] = params.cloudPowder;
		key.params[10] = params.cloudHgG; key.params[11] = params.cloudHgGFwd;
		key.params[12] = params.cloudHgBlend; key.params[13] = params.cloudAmbientScale;
		key.params[14] = params.daylight; key.params[15] = params.viewHeightKm;
		key.params[16] = params.cloudWeatherBias; key.params[17] = params.cloudWeatherTypeContrast;
		key.params[18] = params.cirrusCoverage; key.params[19] = params.cirrusAltKm;
		key.params[20] = params.cirrusScale; key.params[21] = params.cirrusDensity;
		static const int envQuality = []()
		{
			const char* q = std::getenv("FURY_CLOUD_QUALITY");
			if (q == nullptr) return -1;
			if (strcmp(q, "low") == 0) return 0;
			if (strcmp(q, "med") == 0) return 1;
			if (strcmp(q, "high") == 0) return 2;
			return -1;
		}();
		key.quality = envQuality >= 0 ? envQuality : params.cloudQuality;
		key.debugMode = params.cloudDebugMode;
		key.cirrusEnabled = params.cirrusEnabled ? 1 : 0;

		const bool volumeDirty = key.camPos[0] != m_LastCloudCamPos.x || key.camPos[1] != m_LastCloudCamPos.y
			|| key.camPos[2] != m_LastCloudCamPos.z
			|| key.camFwd[0] != m_LastCloudFwd[0] || key.camFwd[1] != m_LastCloudFwd[1]
			|| key.camFwd[2] != m_LastCloudFwd[2] || key.camFwd[3] != m_LastCloudFwd[3]
			|| key.fov != m_LastCloudFov || key.sunY != m_LastCloudSunY || params.staticDirty;
		(void)frameIndex;

		if (volumeDirty)
		{
			RenderCameraVolume(params, cam);
			m_LastCloudCamPos = cam.worldPos;
			m_LastCloudFwd[0] = key.camFwd[0]; m_LastCloudFwd[1] = key.camFwd[1];
			m_LastCloudFwd[2] = key.camFwd[2]; m_LastCloudFwd[3] = key.camFwd[3];
			m_LastCloudFov = key.fov;
			m_LastCloudSunY = key.sunY;
		}

		if (!params.cloudsEnabled)
			return;

		EnsureCloudTarget(rtW, rtH, key.quality);
		key.w = m_CloudTargetW;
		key.h = m_CloudTargetH;

		if (key != m_LastCloudKey || m_CloudParamsDirty)
		{
			if (EnsureCloudNoise(params))
				RenderCloudTarget(params, cam, depthTex);
			m_LastCloudKey = key;
			m_CloudParamsDirty = false;
		}
	}

	SkyParams SkyAtmosphere::SnapshotParams() const
	{
		SkyParams p;
		p.enabled = m_Enabled;
		p.bottomRadiusKm = m_BottomRadiusKm;
		p.topRadiusKm = m_TopRadiusKm;
		p.rayleighScat = m_RayleighScat;
		p.rayleighExpScale = m_RayleighExpScale;
		p.mieScat = m_MieScat;
		p.mieExt = m_MieExt;
		p.mieExpScale = m_MieExpScale;
		p.mieG = m_MieG;
		p.ozoneExt = m_OzoneExt;
		p.ozoneCenterKm = m_OzoneCenterKm;
		p.ozoneWidthKm = m_OzoneWidthKm;
		p.groundAlbedo = m_GroundAlbedo;
		p.sunAngularRadius = m_SunAngularRadius;
		p.sunDiscIntensity = m_SunDiscIntensity;
		p.sunIntensity = m_SunIntensity;
		p.apRangeKm = m_ApRangeKm;
		p.moonEnabled = m_MoonEnabled;
		p.moonAngularRadius = m_MoonAngularRadius;
		p.moonIntensity = m_MoonIntensity;
		p.cloudsEnabled = m_CloudsEnabled;
		p.cloudCoverage = m_CloudCoverage;
		p.cloudAltKm = m_CloudAltKm;
		p.cloudThickKm = m_CloudThickKm;
		p.cloudScale = m_CloudScale;
		p.cloudDensity = m_CloudDensity;
		p.cloudWindSpeedCm = m_CloudWindSpeedCm;
		p.cloudFadeKm = m_CloudFadeKm;
		p.cloudTypeBias = m_CloudTypeBias;
		p.cloudDetailScale = m_CloudDetailScale;
		p.cloudErosion = m_CloudErosion;
		p.cloudPowder = m_CloudPowder;
		p.cloudHgG = m_CloudHgG;
		p.cloudHgGFwd = m_CloudHgGFwd;
		p.cloudHgBlend = m_CloudHgBlend;
		p.cloudAmbientScale = m_CloudAmbientScale;
		p.cloudQuality = m_CloudQuality;
		p.cloudDebugMode = m_CloudDebugMode;
		p.cloudWeatherBias = m_CloudWeatherBias;
		p.cloudWeatherTypeContrast = m_CloudWeatherTypeContrast;
		p.cirrusEnabled = m_CirrusEnabled;
		p.cirrusCoverage = m_CirrusCoverage;
		p.cirrusAltKm = m_CirrusAltKm;
		p.cirrusScale = m_CirrusScale;
		p.cirrusDensity = m_CirrusDensity;
		p.sunDir = m_SunDir;
		p.moonDir = m_MoonDir;
		p.sunColor = m_SunColor;
		p.sunIntensitySky = m_SunIntensitySky;
		p.sunIntensityCur = m_SunIntensityCur;
		p.daylight = m_Daylight;
		p.windOffsetKm = m_WindOffsetKm;
		// The game thread claims the dirty flag into the snapshot; an edit
		// landing mid-flight re-arms it for the next frame.
		p.staticDirty = m_StaticDirty;
		m_StaticDirty = false;
		return p;
	}

	SkyParams SkyAtmosphere::GatherSkyParams(float cameraWorldY)
	{
		EvaluateSunAndLight();
		m_ViewHeightKm = std::max(0.005f, cameraWorldY * 1e-5f);
		SkyParams p = SnapshotParams();
		p.viewHeightKm = m_ViewHeightKm;
		// deterministic screenshots: freeze the noise scroll
		static const bool freezeWind = std::getenv("FURY_CLOUD_FREEZE") != nullptr;
		if (freezeWind)
			p.windOffsetKm = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		return p;
	}

	void SkyAtmosphere::SnapshotRenderTextures(PacketSky &out) const
	{
		std::lock_guard<std::mutex> lock(s_SkyResourceMutex);
		out.transmittanceLut = m_TransmittanceLut;
		out.multiScatterLut = m_MultiScatterLut;
		out.skyViewLut = m_SkyViewLut;
		out.cameraVolume = m_CameraVolume;
		out.cloudTarget = m_CloudTarget;
		out.moonTexture = m_MoonTexture;
	}
}
