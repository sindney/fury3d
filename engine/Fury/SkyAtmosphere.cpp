#include "Fury/SkyAtmosphere.h"

#include <cmath>
#include <cstdlib>
#include <functional>

#include "Fury/Engine.h"
#include "Fury/EntityManager.h"
#include "Fury/FileUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshUtil.h"
#include "Fury/Pass.h"
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
			if (tex->GetID() == 0)
				return nullptr;
			if (em)
				em->Add(tex);
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
		ptr->m_TransmittancePass = ptr->m_MultiScatterPass = ptr->m_SkyViewPass = nullptr;
		ptr->m_CameraVolumePass = ptr->m_CloudPass = nullptr;
		ptr->m_TransmittanceShader = ptr->m_MultiScatterShader = ptr->m_SkyViewShader = nullptr;
		ptr->m_CameraVolumeShader = ptr->m_CloudShader = nullptr;
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

	void SkyAtmosphere::BindAtmosphereUniforms(const std::shared_ptr<Shader> &shader) const
	{
		shader->BindFloat("u_bottom_radius", m_BottomRadiusKm);
		shader->BindFloat("u_top_radius", m_TopRadiusKm);
		shader->BindFloat("u_rayleigh_scat", m_RayleighScat.x, m_RayleighScat.y, m_RayleighScat.z);
		shader->BindFloat("u_rayleigh_density_exp_scale", m_RayleighExpScale);
		shader->BindFloat("u_mie_scat", m_MieScat);
		shader->BindFloat("u_mie_ext", m_MieExt);
		shader->BindFloat("u_mie_density_exp_scale", m_MieExpScale);
		shader->BindFloat("u_mie_g", m_MieG);
		shader->BindFloat("u_ozone_ext", m_OzoneExt.x, m_OzoneExt.y, m_OzoneExt.z);
		shader->BindFloat("u_ozone_center_km", m_OzoneCenterKm);
		shader->BindFloat("u_ozone_width_km", m_OzoneWidthKm);
		shader->BindFloat("u_ground_albedo", m_GroundAlbedo.x, m_GroundAlbedo.y, m_GroundAlbedo.z);
		shader->BindFloat("u_sun_dir", m_SunDir.x, m_SunDir.y, m_SunDir.z);
		shader->BindFloat("u_sun_color", m_SunColor.r, m_SunColor.g, m_SunColor.b);
		shader->BindFloat("u_sun_intensity", m_SunIntensitySky);
		shader->BindFloat("u_view_height", m_ViewHeightKm);
	}

	bool SkyAtmosphere::EnsureResources()
	{
		if (m_ResourcesCreated)
			return true;
		if (_ptrc_glGenTextures == nullptr)
			return false;   // headless (fury exec): no GL

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
		m_CloudTarget = makeLut("sky_clouds", 640, 360, 0, TextureType::TEXTURE_2D);

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
		m_CloudPass = makePass("sky_cloud_pass", m_CloudTarget);

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
		m_CloudShader = loadShader("CloudLayer.glsl");

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

	void SkyAtmosphere::RenderTransmittanceLut()
	{
		m_TransmittanceShader->Bind();
		BindAtmosphereUniforms(m_TransmittanceShader);
		DrawLutQuad(m_TransmittancePass, m_TransmittanceShader);
		m_TransmittanceShader->UnBind();
	}

	void SkyAtmosphere::RenderMultiScatterLut()
	{
		m_MultiScatterShader->Bind();
		BindAtmosphereUniforms(m_MultiScatterShader);
		m_MultiScatterShader->BindTexture("u_transmittance_lut", m_TransmittanceLut);
		DrawLutQuad(m_MultiScatterPass, m_MultiScatterShader);
		m_MultiScatterShader->UnBind();
	}

	void SkyAtmosphere::RenderSkyViewLut()
	{
		m_SkyViewShader->Bind();
		BindAtmosphereUniforms(m_SkyViewShader);
		m_SkyViewShader->BindTexture("u_transmittance_lut", m_TransmittanceLut);
		m_SkyViewShader->BindTexture("u_multiscatter_lut", m_MultiScatterLut);
		DrawLutQuad(m_SkyViewPass, m_SkyViewShader);
		m_SkyViewShader->UnBind();
	}

	void SkyAtmosphere::RenderCameraVolume(const std::shared_ptr<SceneNode> &camNode)
	{
		m_CameraVolumePass->Bind();
		m_CameraVolumeShader->Bind();
		m_CameraVolumeShader->BindCamera(camNode);
		BindAtmosphereUniforms(m_CameraVolumeShader);
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

	void SkyAtmosphere::RenderCloudTarget(const std::shared_ptr<SceneNode> &camNode)
	{
		m_CloudShader->Bind();
		BindAtmosphereUniforms(m_CloudShader);
		m_CloudShader->BindCamera(camNode);
		m_CloudShader->BindTexture("u_transmittance_lut", m_TransmittanceLut);
		m_CloudShader->BindTexture("u_cloud_noise",
			m_CloudNoise ? m_CloudNoise : GetDummyTexture2D());
		m_CloudShader->BindFloat("u_cloud_coverage", m_CloudCoverage);
		m_CloudShader->BindFloat("u_cloud_alt_km", m_CloudAltKm);
		m_CloudShader->BindFloat("u_cloud_thick_km", m_CloudThickKm);
		m_CloudShader->BindFloat("u_cloud_scale", m_CloudScale);
		m_CloudShader->BindFloat("u_cloud_density", m_CloudDensity);
		m_CloudShader->BindFloat("u_wind_offset_km", m_WindOffsetKm.x, m_WindOffsetKm.y);
		m_CloudShader->BindFloat("u_daylight", m_Daylight);
		m_CloudShader->BindFloat("u_cloud_fade_km", m_CloudFadeKm);
		DrawLutQuad(m_CloudPass, m_CloudShader);
		m_CloudShader->UnBind();
	}

	void SkyAtmosphere::EnsureLuts(const std::shared_ptr<SceneNode> &camNode)
	{
		if (!EnsureResources())
			return;

		EvaluateSunAndLight();

		m_ViewHeightKm = 0.06f;
		if (camNode)
			m_ViewHeightKm = std::max(0.005f, camNode->GetWorldPosition().y * 1e-5f);

		const bool skyDebug = std::getenv("FURY_SKY_DEBUG") != nullptr;

		if (m_StaticDirty)
		{
			RenderTransmittanceLut();
			RenderMultiScatterLut();
			m_StaticDirty = false;
			m_LastSunDir = Vector4(0, 0, 0, 0);   // force view lut refresh
			if (skyDebug)
				FURYD << "SkyAtmosphere: static LUTs rendered";
		}

		Vector4 sunDelta = m_SunDir - m_LastSunDir;
		if (sunDelta.SquareLength() > 1e-8f || std::fabs(m_ViewHeightKm - m_LastViewHeightKm) > 1e-5f)
		{
			RenderSkyViewLut();
			m_LastSunDir = m_SunDir;
			m_LastViewHeightKm = m_ViewHeightKm;
			if (skyDebug)
				FURYD << "SkyAtmosphere: sky-view LUT rendered (sun " << m_SunDir.y << ")";
		}

		if (camNode)
			RenderCameraVolume(camNode);

		if (m_CloudsEnabled && camNode)
			RenderCloudTarget(camNode);
	}
}
