#ifndef _FURY_SKY_ATMOSPHERE_H_
#define _FURY_SKY_ATMOSPHERE_H_

#include <memory>
#include <string>

#include "Fury/Color.h"
#include "Fury/Component.h"
#include "Fury/EnumUtil.h"
#include "Fury/Vector4.h"

namespace fury
{
	class Pass;

	class SceneNode;

	class Shader;

	class Texture;

	// Precomputed-LUT sky atmosphere (GL 3.3 fragment-pass port of the UE
	// SkyAtmosphere technique, EGSR 2020). Owns the transmittance /
	// multi-scatter / sky-view LUTs, the aerial-perspective camera volume and
	// the half-res cloud target; evaluates time-of-day and optionally drives
	// the scene's dominant directional light as the sun (design D5).
	//
	// Units: engine world is cm; the atmosphere math works in km
	// (1 km = 1e5 cm). All radii/altitudes below are km.
	class FURY_API SkyAtmosphere : public Component
	{
	public:

		typedef std::shared_ptr<SkyAtmosphere> Ptr;

		static Ptr Create();

		// The scene's active sky (most recently attached enabled one).
		static Ptr GetActive();

		SkyAtmosphere();

		virtual ~SkyAtmosphere();

		virtual Component::Ptr Clone() const override;

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		bool GetEnabled() const { return m_Enabled; }
		void SetEnabled(bool value) { m_Enabled = value; }

		// --- time-of-day ---
		float GetTimeHours() const { return m_TimeHours; }
		void SetTimeHours(float h);

		float GetDayLengthMinutes() const { return m_DayLengthMinutes; }
		void SetDayLengthMinutes(float v) { m_DayLengthMinutes = v; }

		bool GetAutoAdvance() const { return m_AutoAdvance; }
		void SetAutoAdvance(bool v);

		// on: TOD drives the bound light; off: sky follows the light.
		bool GetSunFromTod() const { return m_SunFromTod; }
		void SetSunFromTod(bool v) { m_SunFromTod = v; }

		const std::string &GetSunLightName() const { return m_SunLightName; }
		void SetSunLightName(const std::string &name) { m_SunLightName = name; }

		// Sets the sun light to the scene's first directional light.
		// Returns false when the scene has none.
		bool AutoSelectSunLight();

		// --- clouds ---
		bool GetCloudsEnabled() const { return m_CloudsEnabled; }
		void SetCloudsEnabled(bool v) { m_CloudsEnabled = v; }

		float GetCloudCoverage() const { return m_CloudCoverage; }
		void SetCloudCoverage(float v) { m_CloudCoverage = v; }

		float GetCloudAltitudeKm() const { return m_CloudAltKm; }
		void SetCloudAltitudeKm(float v) { m_CloudAltKm = v; }

		float GetCloudScale() const { return m_CloudScale; }
		void SetCloudScale(float v) { m_CloudScale = v; }

		float GetCloudWindSpeed() const { return m_CloudWindSpeedCm; }
		void SetCloudWindSpeed(float v) { m_CloudWindSpeedCm = v; }

		// extinction per km; higher = thicker-looking clouds
		float GetCloudDensity() const { return m_CloudDensity; }
		void SetCloudDensity(float v) { m_CloudDensity = v; }

		// distance at which the cloud deck dissolves into haze, km
		float GetCloudFadeKm() const { return m_CloudFadeKm; }
		void SetCloudFadeKm(float v) { m_CloudFadeKm = v; }

		// --- atmosphere coefficients (static LUTs re-render on change) ---
		void SetRayleighScattering(Vector4 v) { m_RayleighScat = v; m_StaticDirty = true; }
		void SetMieScattering(float v) { m_MieScat = v; m_StaticDirty = true; }
		void SetMieG(float v) { m_MieG = v; m_StaticDirty = true; }
		void SetGroundAlbedo(Vector4 v) { m_GroundAlbedo = v; m_StaticDirty = true; }
		void SetSunIntensity(float v) { m_SunIntensity = v; }
		void SetMoonTexturePath(const std::string &p) { m_MoonTexturePath = p; m_MoonTexture = nullptr; }
		void SetCloudNoisePath(const std::string &p) { m_CloudNoisePath = p; m_CloudNoise = nullptr; }

		// inspector surface
		Vector4 GetRayleighScattering() const { return m_RayleighScat; }
		float GetMieScattering() const { return m_MieScat; }
		float GetMieG() const { return m_MieG; }
		Vector4 GetGroundAlbedo() const { return m_GroundAlbedo; }
		float GetSunIntensityParam() const { return m_SunIntensity; }
		void SetSunDiscIntensity(float v) { m_SunDiscIntensity = v; }
		void SetMoonEnabled(bool v) { m_MoonEnabled = v; }
		void SetMoonIntensity(float v) { m_MoonIntensity = v; }
		void SetMoonAngularRadius(float v) { m_MoonAngularRadius = v; }
		void SetApRangeKm(float v) { m_ApRangeKm = v; }
		const std::string &GetMoonTexturePath() const { return m_MoonTexturePath; }
		const std::string &GetCloudNoisePath() const { return m_CloudNoisePath; }
		float GetCloudThicknessKm() const { return m_CloudThickKm; }
		void SetCloudThicknessKm(float v) { m_CloudThickKm = v; }

		// --- render hooks (PrelightPipeline) ---
		// Renders any stale LUTs/volume/cloud target for this frame.
		void EnsureLuts(const std::shared_ptr<SceneNode> &camNode);

		// Resolved sun state for this frame (world, toward the sun).
		Vector4 GetSunDirection() const { return m_SunDir; }
		Color GetSunColor() const { return m_SunColor; }
		float GetSunIntensity() const { return m_SunIntensityCur; }
		Vector4 GetMoonDirection() const { return m_MoonDir; }
		float GetDaylight() const { return m_Daylight; }
		float GetViewHeightKm() const { return m_ViewHeightKm; }
		float GetBottomRadiusKm() const { return m_BottomRadiusKm; }

		float GetSunAngularRadius() const { return m_SunAngularRadius; }
		void SetSunAngularRadius(float v) { m_SunAngularRadius = v; }
		float GetSunDiscIntensity() const { return m_SunDiscIntensity; }
		bool GetMoonEnabled() const { return m_MoonEnabled; }
		float GetMoonAngularRadius() const { return m_MoonAngularRadius; }
		float GetMoonIntensity() const { return m_MoonIntensity; }
		std::shared_ptr<Texture> GetMoonTexture() const { return m_MoonTexture; }
		std::shared_ptr<Texture> GetCloudNoiseTexture() const { return m_CloudNoise; }

		std::shared_ptr<Texture> GetTransmittanceLut() const { return m_TransmittanceLut; }
		std::shared_ptr<Texture> GetMultiScatterLut() const { return m_MultiScatterLut; }
		std::shared_ptr<Texture> GetSkyViewLut() const { return m_SkyViewLut; }
		std::shared_ptr<Texture> GetCameraVolume() const { return m_CameraVolume; }
		std::shared_ptr<Texture> GetCloudTarget() const { return m_CloudTarget; }
		float GetApRangeKm() const { return m_ApRangeKm; }

		// Binds every u_* atmosphere uniform on the shader (sampler targets
		// are the caller's job).
		void BindAtmosphereUniforms(const std::shared_ptr<Shader> &shader) const;

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnOwnerDestructing(SceneNode &node) override;

		void Subscribe();

		void Unsubscribe();

		void TickUpdate(float dt);

		// TOD -> sun state; drives the bound light when sun-from-TOD is set.
		void EvaluateSunAndLight();

		std::shared_ptr<SceneNode> ResolveSunLight() const;

		void MarkStaticDirty() { m_StaticDirty = true; }

		bool EnsureResources();

		void RenderTransmittanceLut();

		void RenderMultiScatterLut();

		void RenderSkyViewLut();

		void RenderCameraVolume(const std::shared_ptr<SceneNode> &camNode);

		void RenderCloudTarget(const std::shared_ptr<SceneNode> &camNode);

		void DrawLutQuad(const std::shared_ptr<Pass> &pass, const std::shared_ptr<Shader> &shader);

		bool m_Enabled = true;

		// --- serialized atmosphere parameters (earth-like defaults) ---
		float m_BottomRadiusKm = 6360.0f;
		float m_TopRadiusKm = 6460.0f;
		Vector4 m_RayleighScat = Vector4(5.802e-3f, 13.558e-3f, 33.1e-3f, 0.0f);
		float m_RayleighExpScale = -0.125f;
		float m_MieScat = 3.996e-3f;
		float m_MieExt = 4.40e-3f;
		float m_MieExpScale = -0.8333f;
		float m_MieG = 0.8f;
		Vector4 m_OzoneExt = Vector4(0.650e-3f, 1.881e-3f, 0.085e-3f, 0.0f);
		float m_OzoneCenterKm = 25.0f;
		float m_OzoneWidthKm = 15.0f;
		Vector4 m_GroundAlbedo = Vector4(0.3f, 0.3f, 0.3f, 0.0f);
		float m_SunAngularRadius = 0.004675f;   // 0.535 deg diameter
		float m_SunDiscIntensity = 20.0f;
		float m_SunIntensity = 3.0f;
		float m_ApRangeKm = 5.0f;               // aerial-perspective volume range

		bool m_MoonEnabled = true;
		float m_MoonAngularRadius = 0.0047f;
		float m_MoonIntensity = 0.12f;
		std::string m_MoonTexturePath = "Engine/Texture/Sky/moon.png";

		bool m_CloudsEnabled = false;
		float m_CloudCoverage = 0.45f;
		float m_CloudAltKm = 0.15f;
		float m_CloudThickKm = 0.12f;
		float m_CloudScale = 0.35f;             // noise uv per km
		float m_CloudDensity = 18.0f;           // extinction per km
		float m_CloudWindSpeedCm = 200.0f;      // cm/s
		float m_CloudFadeKm = 2.5f;
		std::string m_CloudNoisePath = "Engine/Texture/Sky/cloud_noise.png";

		float m_TimeHours = 12.0f;
		float m_DayLengthMinutes = 10.0f;
		bool m_AutoAdvance = false;
		bool m_SunFromTod = true;
		std::string m_SunLightName;

		// --- runtime state ---
		Vector4 m_SunDir = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
		Vector4 m_MoonDir = Vector4(0.0f, -1.0f, 0.0f, 0.0f);
		Color m_SunColor = Color::White;
		// sky shader sun drive: NOT daylight-ramped (the atmosphere self-dims
		// via transmittance; ramping it kills twilight glow)
		float m_SunIntensitySky = 3.0f;
		// light-drive intensity: daylight-ramped for gameplay
		float m_SunIntensityCur = 3.0f;
		float m_Daylight = 1.0f;
		float m_ViewHeightKm = 0.0f;
		Vector4 m_WindOffsetKm = Vector4(0.0f, 0.0f, 0.0f, 0.0f);

		bool m_ResourcesCreated = false;
		bool m_StaticDirty = true;
		Vector4 m_LastSunDir = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		float m_LastViewHeightKm = -1.0f;

		size_t m_UpdateKey = 0;

		std::shared_ptr<Texture> m_TransmittanceLut;
		std::shared_ptr<Texture> m_MultiScatterLut;
		std::shared_ptr<Texture> m_SkyViewLut;
		std::shared_ptr<Texture> m_CameraVolume;
		std::shared_ptr<Texture> m_CloudTarget;
		std::shared_ptr<Texture> m_MoonTexture;
		std::shared_ptr<Texture> m_CloudNoise;

		std::shared_ptr<Pass> m_TransmittancePass;
		std::shared_ptr<Pass> m_MultiScatterPass;
		std::shared_ptr<Pass> m_SkyViewPass;
		std::shared_ptr<Pass> m_CameraVolumePass;
		std::shared_ptr<Pass> m_CloudPass;

		std::shared_ptr<Shader> m_TransmittanceShader;
		std::shared_ptr<Shader> m_MultiScatterShader;
		std::shared_ptr<Shader> m_SkyViewShader;
		std::shared_ptr<Shader> m_CameraVolumeShader;
		std::shared_ptr<Shader> m_CloudShader;
	};
}

#endif // _FURY_SKY_ATMOSPHERE_H_
