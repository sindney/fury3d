#include "Fury/VegetationUtil.h"

#include <cmath>
#include <iostream>
#include <limits>

#if __has_include("ThirdParty/Kraut/Tools/KrautCLI/FoliageShading.h")
#include "ThirdParty/Kraut/Tools/KrautCLI/FoliageShading.h"
#define TEST_KRAUT_SHADING
#endif

int main()
{
	int failures = 0;
	auto check = [&](bool condition, const char* message) {
		if (!condition) { std::cerr << message << '\n'; ++failures; }
	};
	check(fury::BillboardBiasFromMeanUp(-1.0f) == 0.0f, "negative canopy up clamps to zero");
	check(fury::BillboardBiasFromMeanUp(2.0f) == 1.0f, "canopy up above one clamps to one");
	check(fury::BillboardBiasFromMeanUp(std::numeric_limits<float>::quiet_NaN()) == 0.0f, "NaN has a finite fallback");
	check(std::abs(fury::BillboardBiasFromMeanUp(std::sqrt(0.5f)) - 0.5f) < 1e-6f,
		"45 degree canopy normal uses midpoint bias");
	float previous = -1.0f;
	for (int i = 0; i <= 10000; ++i)
	{
		const float up = i / 10000.0f;
		const float bias = fury::BillboardBiasFromMeanUp(up);
		const float reconstructed = bias / std::sqrt((1.0f - bias) * (1.0f - bias) + bias * bias);
		check(std::isfinite(bias) && bias >= previous, "bias is finite and monotonic");
		check(std::abs(reconstructed - up) < 1e-6f, "bias preserves the requested normal elevation");
		previous = bias;
	}
#ifdef TEST_KRAUT_SHADING
	float position[3] = {2.0f, 4.0f, 0.0f}, normal[3] = {}, tangent[3] = {};
	KrautCLI::ExpandLeafCard(position, normal, tangent, 0.0f, 0.0f, 0.5f);
	check(position[0] == 2.0f && position[1] == 3.5f && position[2] == 0.5f,
		"encoded leaves expand into radial cards");
	check(normal[0] == 1.0f && tangent[2] == -1.0f, "leaf basis follows its radial direction");
	const float minimum[3] = {-3.0f, 0.0f, -3.0f}, maximum[3] = {3.0f, 6.0f, 3.0f};
	KrautCLI::CrownNormal(position, minimum, maximum, normal);
	check(normal[1] > 0.0f, "canopy normals point upward");
	check(std::abs(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2] - 1.0f) < 1e-6f,
		"canopy normals have unit length");
#endif
	return failures == 0 ? 0 : 1;
}
