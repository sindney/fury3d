#ifndef _FURY_WAVE_SAMPLER_H_
#define _FURY_WAVE_SAMPLER_H_

#include "Fury/Vector4.h"

namespace fury
{
	class OceanWaves;

	// CPU wave queries over an OceanWaves asset (baked or GPU-generated -
	// both fill the same float32 CPU copy). Sampling wraps in space (per
	// band tile) and time (loop), mirrors the GPU surface shader: texel-
	// center bilinear + frame lerp, all bands summed. Invalid assets sample
	// as flat water (zero displacement); the owning OceanComponent adds its
	// water level on top of these wave-local values.
	namespace WaveSampler
	{
		// wave height at world (x, z) and time t (seconds)
		float Height(const OceanWaves &waves, float x, float z, float t);

		// full choppy displacement (x=dx, y=dy, z=dz)
		Vector4 Displacement(const OceanWaves &waves, float x, float z, float t);

		// surface normal via per-band central differences of Height
		Vector4 Normal(const OceanWaves &waves, float x, float z, float t);

		// Height compensated for the choppy horizontal pinch: iterates
		// p = (x,z) - D(p) a fixed number of times before sampling.
		float HeightChoppyCorrected(const OceanWaves &waves, float x, float z, float t, int iterations = 2);
	}
}

#endif // _FURY_WAVE_SAMPLER_H_
