#ifndef _FURY_RENDER_SETTINGS_H_
#define _FURY_RENDER_SETTINGS_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Fury/Serializable.h"

namespace fury
{
	class UniformBase;

	// One entry in the per-scene postprocess chain. Each entry
	// references an effect by NAME (resolved against the global
	// PostProcessRegistry at load time) and carries its own enabled
	// flag so individual effects can be toggled without removing
	// the entry from the chain.
	//
	// uniformOverrides: optional per-instance values replacing the
	// effect descriptor's declared defaults for matching names
	// (serialized as "uniforms": [{ "name", "value": [f, ...] }] --
	// the same shape as PostProcessEffect's descriptor defaults).
	struct RenderChainEntry
	{
		std::string effectName;
		bool enabled = true;
		std::unordered_map<std::string, std::shared_ptr<UniformBase>> uniformOverrides;
	};

	// Per-scene render configuration. Owned by Scene; serialized as
	// the `renderSettings` block in scene JSON. Holds:
	//   * pipelinePath - which pipeline JSON to load (the LDR
	//     DefferedLightingLambert.json or the HDR PBR variant).
	//   * hdr          - true -> PBR + float targets + mandatory ACES
	//     tonemap; false -> legacy LDR Lambert.
	//   * cascadedShadowMap - per-scene CSM override (drives the
	//     Pipeline::CASCADED_SHADOW_MAP switch on scene load).
	//   * chain        - ordered list of postprocess entries.
	//
	// The scene owns one of these; legacy scenes without the block
	// get a default (empty pipeline path + LDR + CSM on + empty
	// chain) at load time.
	class FURY_API RenderSettings : public Serializable
	{
	public:

		typedef std::shared_ptr<RenderSettings> Ptr;

		// Defaults match the legacy "no renderSettings block" path:
		// no pipeline, LDR, CSM on, empty chain. So when Scene::Load
		// constructs a fresh RenderSettings for an old scene file it
		// already behaves correctly.
		RenderSettings();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		// pipeline path (relative to working dir, same convention as
		// Shader::m_FilePath). Empty = use the active pipeline (legacy
		// behaviour: the editor / Lua script picks it).
		const std::string &GetPipelinePath() const;
		void SetPipelinePath(const std::string &path);

		bool IsHDR() const;
		void SetHDR(bool value);

		bool IsCascadedShadowMap() const;
		void SetCascadedShadowMap(bool value);

		// CSM depth map resolution per cascade (default 1024).
		int GetCsmMapSize() const;
		void SetCsmMapSize(int size);

		// Cascade range in cm; 0 = cover the camera's full far plane.
		float GetShadowFar() const;
		void SetShadowFar(float far);

		// Split blend: 0 = linear, 1 = logarithmic, in-between blends.
		float GetCsmSplitBlend() const;
		void SetCsmSplitBlend(float blend);

		// The 4 cascade far distances (cm view depth) for a camera with
		// this near/far. THE split source: the shadow-map render and the
		// light shader's cascade picker must both use these.
		void ComputeCsmSplits(float nearPlane, float cameraFar, float *outSplits4) const;

		// Chain accessors. The chain is owned by RenderSettings; UI
		// code mutates it in place via these methods. Resolution
		// against the registry happens on scene load (see
		// Scene::Load).
		const std::vector<RenderChainEntry> &GetChain() const;
		std::vector<RenderChainEntry> &GetChainMutable();

		void ClearChain();

		void AddEffect(const std::string &effectName, bool enabled = true);

		// Replace the chain with a deep copy of `other`'s (preserves
		// per-entry uniform overrides; used by the editor's File ->
		// Open path, which previously dropped them via AddEffect).
		void CopyChainFrom(const RenderSettings &other);

		// Remove by name; first match wins.
		void RemoveEffect(const std::string &effectName);

		// Move an existing chain entry from srcIndex to dstIndex. No-op
		// when either index is out of range.
		void MoveEffect(unsigned int srcIndex, unsigned int dstIndex);

		void SetEffectEnabled(unsigned int index, bool enabled);

	private:

		std::string m_PipelinePath;
		bool m_HDR = false;
		bool m_CascadedShadowMap = true;
		int m_CsmMapSize = 2048;
		float m_ShadowFar = 20000.0f;    // cm; 0 = camera far
		float m_CsmSplitBlend = 0.7f;    // 0 = linear, 1 = logarithmic
		std::vector<RenderChainEntry> m_Chain;
	};
}

#endif // _FURY_RENDER_SETTINGS_H_