#pragma once

#include <algorithm>
#include <cmath>

namespace fury
{
	inline float BillboardBiasFromMeanUp(float meanUp)
	{
		const float m = std::isfinite(meanUp) ? std::clamp(meanUp, 0.0f, 1.0f) : 0.0f;
		return m / (m + std::sqrt(std::max(0.0f, 1.0f - m * m)));
	}
}
