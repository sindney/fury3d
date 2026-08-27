#ifndef _FURY_OCEAN_WAVES_H_
#define _FURY_OCEAN_WAVES_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/Entity.h"
#include "Fury/Texture.h"

namespace fury
{
	// Ocean wave asset: a set of looping FFT bands described by an
	// ocean.json sidecar (see tools/gen_ocean_assets.py). Per band: a
	// displacement field (dx,dy,dz,foam per texel per frame) held as a
	// float32 CPU copy for WaveSampler/buoyancy, plus frame-layered 2D
	// array textures for the GPU surface shader when a GL context is live.
	// The scene file stores only the ocean.json path; payloads re-decode
	// (or regenerate via the compute path) on load.
	class FURY_API OceanWaves : public Entity
	{
	public:

		struct Band
		{
			int Resolution = 0;

			float TileCm = 0.0f;

			// Peak of the raw Jacobian foam channel over all frames
			// (ocean.json maxFoam, or measured post-readback on the GPU
			// path). Informational - the surface shader thresholds the
			// ABSOLUTE bake value so calm seas stay foam-free. The CPU copy
			// stays raw (bit-faithful to the payload for spec tests).
			float MaxFoam = 0.0f;

			// frames*N*N*4 floats: dx,dy,dz,foam. Frame-major, [z*N+x] rows.
			std::vector<float> Disp;

			Texture::Ptr DispTexture; // RGBA16F 2D array, frames as layers
			Texture::Ptr NrmTexture;  // RGBA8 2D array, frames as layers
		};

		typedef std::shared_ptr<OceanWaves> Ptr;

		static Ptr Create(const std::string &name);

		// EntityManager get-or-create cache keyed by path, so two oceans
		// referencing the same ocean.json share one loaded instance.
		static Ptr Resolve(const std::string &path);

		OceanWaves(const std::string &name);

		virtual ~OceanWaves();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		const std::string &GetFilePath() const { return m_FilePath; }
		void SetFilePath(const std::string &path) { m_FilePath = path; }

		// Reads ocean.json + band payloads (scene-working-dir relative).
		// Idempotent. CPU data always loads; GL textures only when a
		// context is live. Returns false (one logged warning) on
		// missing/malformed files.
		bool LoadWaves();

		bool IsValid() const { return m_Frames > 0 && !m_Bands.empty(); }

		int GetFrameCount() const { return m_Frames; }

		float GetLoopSeconds() const { return m_LoopSeconds; }

		int GetBandCount() const { return (int)m_Bands.size(); }

		const Band &GetBand(int index) const { return m_Bands[index]; }
		Band &GetBand(int index) { return m_Bands[index]; }

		// GPU-generation path: set loop info, then install fully populated
		// bands (CPU copy + textures) by move.
		void SetLoopInfo(int frames, float loopSeconds);
		void SetBand(int index, Band &&band);

		// Test access: one channel of one texel of the CPU copy.
		float GetDispValue(int band, int frame, int i, int j, int channel) const;

	protected:

		std::string m_FilePath;

		int m_Frames = 0;

		float m_LoopSeconds = 0.0f;

		std::vector<Band> m_Bands;
	};
}

#endif // _FURY_OCEAN_WAVES_H_
