#ifndef _FURY_SKY_ATMOSPHERE_H_
#define _FURY_SKY_ATMOSPHERE_H_

#include <cstdint>
#include <cstring>
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

	struct PacketCamera;

	struct PacketSky;

	// Cloud target / camera-volume re-render key: camera, wind, sun and the
	// cloud params. Unchanged key = the per-frame renders are still valid.
	struct CloudRenderKey
	{
		float camPos[3] = { 0.0f };
		float camFwd[4] = { 0.0f };
		float fov = -1.0f;
		float wind[2] = { 0.0f };
		float sunY = -999.0f;
		float params[22] = { 0.0f };
		int quality = -1;
		int debugMode = 0;
		int cirrusEnabled = 0;
		int w = 0;
		int h = 0;
		bool operator==(const CloudRenderKey &o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
		bool operator!=(const CloudRenderKey &o) const { return !(*this == o); }
	};

	// Value snapshot of every field the LUT renders and pass bindings read.
	// Gathered on the game thread (SnapshotParams); consumed on the render
	// thread so the render path never races TickUpdate/editor writes.
	struct SkyParams
	{
		bool enabled = true;

		float bottomRadiusKm = 6360.0f;

		float topRadiusKm = 6460.0f;

		Vector4 rayleighScat = Vector4(5.802e-3f, 13.558e-3f, 33.1e-3f, 0.0f);

		float rayleighExpScale = -0.125f;

		float mieScat = 3.996e-3f;

		float mieExt = 4.40e-3f;

		float mieExpScale = -0.8333f;

		float mieG = 0.8f;

		Vector4 ozoneExt = Vector4(0.650e-3f, 1.881e-3f, 0.085e-3f, 0.0f);

		float ozoneCenterKm = 25.0f;

		float ozoneWidthKm = 15.0f;

		Vector4 groundAlbedo = Vector4(0.3f, 0.3f, 0.3f, 0.0f);

		float sunAngularRadius = 0.004675f;

		float sunDiscIntensity = 20.0f;

		float sunIntensity = 3.0f;

		float apRangeKm = 5.0f;

		bool moonEnabled = true;

		float moonAngularRadius = 0.0047f;

		float moonIntensity = 0.12f;

		bool cloudsEnabled = false;

		float cloudCoverage = 0.45f;

		float cloudAltKm = 0.15f;

		float cloudThickKm = 0.12f;

		float cloudScale = 0.35f;

		float cloudDensity = 18.0f;

		float cloudWindSpeedCm = 200.0f;

		float cloudFadeKm = 2.5f;

		// volumetric cloud block
		float cloudTypeBias = 0.0f;

		float cloudDetailScale = 2.4f;

		float cloudErosion = 0.5f;

		float cloudPowder = 1.0f;

		float cloudHgG = 0.2f;

		float cloudHgGFwd = 0.7f;

		float cloudHgBlend = 0.5f;

		float cloudAmbientScale = 1.0f;

		// 0 low (quarter res), 1 med (half res), 2 high (half res, deep march)
		int cloudQuality = 1;

		int cloudDebugMode = 0;

		float cloudWeatherBias = 0.0f;

		float cloudWeatherTypeContrast = 1.6f;

		bool cirrusEnabled = true;

		float cirrusCoverage = 0.35f;

		float cirrusAltKm = 8.0f;

		float cirrusScale = 0.02f;

		float cirrusDensity = 1.5f;

		// runtime state (TickUpdate output)
		Vector4 sunDir = Vector4(0.0f, 1.0f, 0.0f, 0.0f);

		Vector4 moonDir = Vector4(0.0f, -1.0f, 0.0f, 0.0f);

		Color sunColor = Color::White;

		float sunIntensitySky = 3.0f;

		float sunIntensityCur = 3.0f;

		float daylight = 1.0f;

		float viewHeightKm = 0.0f;

		Vector4 windOffsetKm;

		// Set by the game thread on param edits; consumed (cleared) by the
		// render thread's LUT refresh.
		bool staticDirty = false;
	};

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

		// --- volumetric cloud block ---
		float GetCloudTypeBias() const { return m_CloudTypeBias; }
		void SetCloudTypeBias(float v) { m_CloudTypeBias = v; }
		float GetCloudDetailScale() const { return m_CloudDetailScale; }
		void SetCloudDetailScale(float v) { m_CloudDetailScale = v; }
		float GetCloudErosion() const { return m_CloudErosion; }
		void SetCloudErosion(float v) { m_CloudErosion = v; }
		float GetCloudPowder() const { return m_CloudPowder; }
		void SetCloudPowder(float v) { m_CloudPowder = v; }
		float GetCloudHgG() const { return m_CloudHgG; }
		void SetCloudHgG(float v) { m_CloudHgG = v; }
		float GetCloudHgGFwd() const { return m_CloudHgGFwd; }
		void SetCloudHgGFwd(float v) { m_CloudHgGFwd = v; }
		float GetCloudHgBlend() const { return m_CloudHgBlend; }
		void SetCloudHgBlend(float v) { m_CloudHgBlend = v; }
		float GetCloudAmbientScale() const { return m_CloudAmbientScale; }
		void SetCloudAmbientScale(float v) { m_CloudAmbientScale = v; }
		int GetCloudQuality() const { return m_CloudQuality; }
		void SetCloudQuality(int v) { m_CloudQuality = v; }
		int GetCloudDebugMode() const { return m_CloudDebugMode; }
		void SetCloudDebugMode(int v) { m_CloudDebugMode = v; }
		float GetCloudWeatherBias() const { return m_CloudWeatherBias; }
		void SetCloudWeatherBias(float v) { m_CloudWeatherBias = v; }
		float GetCloudWeatherTypeContrast() const { return m_CloudWeatherTypeContrast; }
		void SetCloudWeatherTypeContrast(float v) { m_CloudWeatherTypeContrast = v; }
		bool GetCirrusEnabled() const { return m_CirrusEnabled; }
		void SetCirrusEnabled(bool v) { m_CirrusEnabled = v; }
		float GetCirrusCoverage() const { return m_CirrusCoverage; }
		void SetCirrusCoverage(float v) { m_CirrusCoverage = v; }
		float GetCirrusAltKm() const { return m_CirrusAltKm; }
		void SetCirrusAltKm(float v) { m_CirrusAltKm = v; }
		float GetCirrusScale() const { return m_CirrusScale; }
		void SetCirrusScale(float v) { m_CirrusScale = v; }
		float GetCirrusDensity() const { return m_CirrusDensity; }
		void SetCirrusDensity(float v) { m_CirrusDensity = v; }

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
		// Render-thread entry: LUT refresh from a params/camera snapshot (no
		// scene reads, no EvaluateSunAndLight -- the game thread owns sun
		// state via GatherSkyFrame). rtW/rtH size the cloud target; depthTex
		// clips the cloud march against opaque geometry; frameIndex guards
		// per-frame work when several passes call in one frame.
		void EnsureLutsRender(const SkyParams &params, const PacketCamera &cam,
			int rtW, int rtH, const std::shared_ptr<Texture> &depthTex, std::uint64_t frameIndex);

		// Game-thread snapshot of the render-relevant fields.
		SkyParams SnapshotParams() const;

		// Game thread, once per frame at packet gather: evaluates ToD sun
		// state (drives the bound light), computes view height from the
		// camera, and returns the param snapshot.
		SkyParams GatherSkyParams(float cameraWorldY);

		// Copies the LUT/moon/cloud texture ptrs into the packet sky
		// (mutex-guarded: EnsureResources publishes them from the GL
		// thread on first use).
		void SnapshotRenderTextures(PacketSky &out) const;

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
		void BindAtmosphereUniforms(const std::shared_ptr<Shader> &shader, const SkyParams &params) const;

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

		void RenderTransmittanceLut(const SkyParams &params);

		void RenderMultiScatterLut(const SkyParams &params);

		void RenderSkyViewLut(const SkyParams &params);

		void RenderCameraVolume(const SkyParams &params, const PacketCamera &cam);

		void RenderCloudTarget(const SkyParams &params, const PacketCamera &cam, const std::shared_ptr<Texture> &depthTex);

		// Volumetric-cloud noise set: load from disk cache or generate via
		// fragment passes (one slice per draw), then cache to disk.
		bool EnsureCloudNoise(const SkyParams &params);

		// (Re)creates the RT-relative cloud target + pass when size/quality change.
		void EnsureCloudTarget(int rtW, int rtH, int quality);

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
		float m_CloudAltKm = 1.5f;              // slab base above surface
		float m_CloudThickKm = 2.5f;            // slab thickness
		float m_CloudScale = 0.35f;             // base noise uvw per km
		float m_CloudDensity = 18.0f;           // extinction per km
		float m_CloudWindSpeedCm = 200.0f;      // cm/s
		float m_CloudFadeKm = 20.0f;
		std::string m_CloudNoisePath = "Engine/Texture/Sky/cloud_noise.png";

		float m_CloudTypeBias = 0.0f;           // weather type offset: stratus..cumulonimbus
		float m_CloudDetailScale = 2.4f;        // detail noise uvw per km
		float m_CloudErosion = 0.5f;            // detail erosion strength
		float m_CloudPowder = 1.0f;             // powder sugar strength 0..1
		float m_CloudHgG = 0.2f;                // phase base lobe
		float m_CloudHgGFwd = 0.7f;             // phase forward lobe
		float m_CloudHgBlend = 0.5f;
		float m_CloudAmbientScale = 1.0f;
		int m_CloudQuality = 1;                 // 0 low, 1 med, 2 high
		int m_CloudDebugMode = 0;               // 1 steps heatmap, 2 transmittance
		float m_CloudWeatherBias = 0.0f;        // coverage field offset (regenerates noise)
		float m_CloudWeatherTypeContrast = 1.6f;

		bool m_CirrusEnabled = true;
		float m_CirrusCoverage = 0.35f;
		float m_CirrusAltKm = 8.0f;
		float m_CirrusScale = 0.02f;            // 2D noise uv per km
		float m_CirrusDensity = 1.5f;

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

		// Game thread sets on param edits; render thread clears after LUT
		// refresh. Atomic: the two threads never touch it simultaneously
		// in a defined order otherwise.
		// Game-thread only: setters/editor mark it, SnapshotParams consumes.
		mutable bool m_StaticDirty = true;
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

		// volumetric-cloud noise set (generated once, disk-cached)
		std::shared_ptr<Texture> m_CloudBaseNoise;    // 128^3 rgba8
		std::shared_ptr<Texture> m_CloudDetailNoise;  // 32^3 rgb8
		std::shared_ptr<Texture> m_WeatherMap;        // 256x256 rgba8
		std::shared_ptr<Texture> m_CirrusNoise;       // 256x256 rgba8 (procedural wispy streaks)

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
		std::shared_ptr<Shader> m_NoiseGenShader;

		// cloud target sizing / per-frame render guards (render thread)
		int m_CloudTargetW = 0;
		int m_CloudTargetH = 0;
		int m_CloudTargetQuality = -1;
		Vector4 m_LastCloudCamPos;
		float m_LastCloudFwd[3] = { 0.0f, 0.0f, 0.0f };
		float m_LastCloudFov = -1.0f;
		float m_LastCloudSunY = -999.0f;
		bool m_CloudParamsDirty = true;
		CloudRenderKey m_LastCloudKey;
	};
}

#endif // _FURY_SKY_ATMOSPHERE_H_
