#include "Fury/WaveSampler.h"

#include <cmath>

#include "Fury/OceanWaves.h"

namespace fury
{
	namespace
	{
		float Wrap01(float v)
		{
			return v - std::floor(v);
		}

		// bilinear texel-center sampling of one channel of one frame, wrapped
		float SampleTexel(const OceanWaves::Band &band, int frames, int frame,
			int channel, float u, float v)
		{
			int n = band.Resolution;
			float gx = Wrap01(u) * n - 0.5f;
			float gz = Wrap01(v) * n - 0.5f;
			float fx = gx - std::floor(gx);
			float fz = gz - std::floor(gz);
			int x0 = (int)std::floor(gx);
			int z0 = (int)std::floor(gz);
			int x1 = x0 + 1;
			int z1 = z0 + 1;
			x0 = ((x0 % n) + n) % n;
			x1 = ((x1 % n) + n) % n;
			z0 = ((z0 % n) + n) % n;
			z1 = ((z1 % n) + n) % n;

			const size_t stride = (size_t)n * n * 4;
			const float *base = band.Disp.data() + (size_t)frame * stride;
			float v00 = base[(z0 * n + x0) * 4 + channel];
			float v10 = base[(z0 * n + x1) * 4 + channel];
			float v01 = base[(z1 * n + x0) * 4 + channel];
			float v11 = base[(z1 * n + x1) * 4 + channel];
			float a = v00 + (v10 - v00) * fx;
			float b = v01 + (v11 - v01) * fx;
			return a + (b - a) * fz;
		}

		// one band, one channel, frame-lerped
		float SampleChannel(const OceanWaves::Band &band, int frames,
			float loopSeconds, int channel, float x, float z, float t)
		{
			float ft = Wrap01(t / loopSeconds) * frames;
			int f0 = (int)std::floor(ft);
			float tl = ft - (float)f0;
			f0 %= frames;
			int f1 = (f0 + 1) % frames;

			float u = x / band.TileCm;
			float v = z / band.TileCm;
			float a = SampleTexel(band, frames, f0, channel, u, v);
			float b = SampleTexel(band, frames, f1, channel, u, v);
			return a + (b - a) * tl;
		}
	}

	float WaveSampler::Height(const OceanWaves &waves, float x, float z, float t)
	{
		return Displacement(waves, x, z, t).y;
	}

	Vector4 WaveSampler::Displacement(const OceanWaves &waves, float x, float z, float t)
	{
		if (!waves.IsValid())
			return Vector4(0.0f, 0.0f, 0.0f, 0.0f);

		float dx = 0.0f, dy = 0.0f, dz = 0.0f;
		int bands = waves.GetBandCount();
		for (int b = 0; b < bands; b++)
		{
			const OceanWaves::Band &band = waves.GetBand(b);
			dx += SampleChannel(band, waves.GetFrameCount(), waves.GetLoopSeconds(), 0, x, z, t);
			dy += SampleChannel(band, waves.GetFrameCount(), waves.GetLoopSeconds(), 1, x, z, t);
			dz += SampleChannel(band, waves.GetFrameCount(), waves.GetLoopSeconds(), 2, x, z, t);
		}
		return Vector4(dx, dy, dz, 0.0f);
	}

	Vector4 WaveSampler::Normal(const OceanWaves &waves, float x, float z, float t)
	{
		if (!waves.IsValid())
			return Vector4(0.0f, 1.0f, 0.0f, 0.0f);

		float sx = 0.0f, sz = 0.0f;
		int bands = waves.GetBandCount();
		for (int b = 0; b < bands; b++)
		{
			const OceanWaves::Band &band = waves.GetBand(b);
			float eps = band.TileCm / band.Resolution;
			float hx1 = SampleChannel(band, waves.GetFrameCount(), waves.GetLoopSeconds(), 1, x + eps, z, t);
			float hx0 = SampleChannel(band, waves.GetFrameCount(), waves.GetLoopSeconds(), 1, x - eps, z, t);
			float hz1 = SampleChannel(band, waves.GetFrameCount(), waves.GetLoopSeconds(), 1, x, z + eps, t);
			float hz0 = SampleChannel(band, waves.GetFrameCount(), waves.GetLoopSeconds(), 1, x, z - eps, t);
			sx += (hx1 - hx0) / (2.0f * eps);
			sz += (hz1 - hz0) / (2.0f * eps);
		}

		Vector4 n(-sx, 1.0f, -sz, 0.0f);
		n.Normalize();
		return n;
	}

	float WaveSampler::HeightChoppyCorrected(const OceanWaves &waves, float x, float z, float t, int iterations)
	{
		float px = x;
		float pz = z;
		for (int it = 0; it < iterations; it++)
		{
			Vector4 d = Displacement(waves, px, pz, t);
			px = x - d.x;
			pz = z - d.z;
		}
		return Height(waves, px, pz, t);
	}
}
