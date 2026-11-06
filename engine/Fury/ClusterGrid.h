#ifndef _FURY_CLUSTER_GRID_H_
#define _FURY_CLUSTER_GRID_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Fury/Vector4.h"

namespace fury
{
	namespace ClusterGrid
	{
		constexpr int TILES_X = 16;
		constexpr int TILES_Y = 8;
		constexpr int SLICES_Z = 24;
		constexpr int COUNT = TILES_X * TILES_Y * SLICES_Z;
		constexpr int MAX_LIGHTS = 128;
		constexpr int MAX_POINT_SHADOWS = 6;
		constexpr int MAX_SPOT_SHADOWS = 6;

		inline int DepthSlice(float depth, float nearPlane, float farPlane)
		{
			if (depth <= nearPlane) return 0;
			if (depth >= farPlane) return SLICES_Z - 1;
			const float scale = static_cast<float>(SLICES_Z) / std::log(farPlane / nearPlane);
			return std::clamp(static_cast<int>(std::floor(std::log(depth / nearPlane) * scale)), 0, SLICES_Z - 1);
		}

		inline float SliceBoundary(int slice, float nearPlane, float farPlane)
		{
			return nearPlane * std::pow(farPlane / nearPlane, static_cast<float>(slice) / SLICES_Z);
		}

		inline int Index(int x, int y, int z)
		{
			return (z * TILES_Y + y) * TILES_X + x;
		}

		inline void Coordinate(int pixelX, int pixelY, float viewDepth, int viewportW, int viewportH,
			float nearPlane, float farPlane, int &x, int &y, int &z)
		{
			x = std::clamp(pixelX * TILES_X / viewportW, 0, TILES_X - 1);
			y = std::clamp(pixelY * TILES_Y / viewportH, 0, TILES_Y - 1);
			z = DepthSlice(viewDepth, nearPlane, farPlane);
		}

		inline void Bounds(int x, int y, int z, float nearPlane, float farPlane,
			float tanHalfFovX, float tanHalfFovY, Vector4 &boxMin, Vector4 &boxMax)
		{
			const float z0 = SliceBoundary(z, nearPlane, farPlane);
			const float z1 = SliceBoundary(z + 1, nearPlane, farPlane);
			const float ndcX0 = static_cast<float>(x) / TILES_X * 2.0f - 1.0f;
			const float ndcX1 = static_cast<float>(x + 1) / TILES_X * 2.0f - 1.0f;
			const float ndcY0 = static_cast<float>(y) / TILES_Y * 2.0f - 1.0f;
			const float ndcY1 = static_cast<float>(y + 1) / TILES_Y * 2.0f - 1.0f;
			const float x0 = std::min(ndcX0 * tanHalfFovX * z0, ndcX0 * tanHalfFovX * z1);
			const float x1 = std::max(ndcX1 * tanHalfFovX * z0, ndcX1 * tanHalfFovX * z1);
			const float y0 = std::min(ndcY0 * tanHalfFovY * z0, ndcY0 * tanHalfFovY * z1);
			const float y1 = std::max(ndcY1 * tanHalfFovY * z0, ndcY1 * tanHalfFovY * z1);
			boxMin = Vector4(std::min(x0, x1), std::min(y0, y1), -z1, 1.0f);
			boxMax = Vector4(std::max(x0, x1), std::max(y0, y1), -z0, 1.0f);
		}

		inline bool SphereIntersects(const Vector4 &center, float radius, const Vector4 &boxMin, const Vector4 &boxMax)
		{
			const float x = std::clamp(center.x, boxMin.x, boxMax.x);
			const float y = std::clamp(center.y, boxMin.y, boxMax.y);
			const float z = std::clamp(center.z, boxMin.z, boxMax.z);
			const float dx = center.x - x;
			const float dy = center.y - y;
			const float dz = center.z - z;
			return dx * dx + dy * dy + dz * dz <= radius * radius;
		}

		inline bool SpotIntersects(const Vector4 &apex, const Vector4 &direction, float range, float cosOuter,
			const Vector4 &boxMin, const Vector4 &boxMax)
		{
			const float safeCos = std::max(cosOuter, 0.0001f);
			const float sinOuter = std::sqrt(std::max(0.0f, 1.0f - safeCos * safeCos));
			const Vector4 center = apex + direction * (range * 0.5f);
			const float radius = std::sqrt(range * range * 0.25f + range * range * sinOuter * sinOuter / (safeCos * safeCos));
			if (!SphereIntersects(center, radius, boxMin, boxMax)) return false;

			float minAxis = 0.0f;
			float maxAxis = 0.0f;
			for (int i = 0; i < 8; ++i)
			{
				const Vector4 corner(
					(i & 1) ? boxMax.x : boxMin.x,
					(i & 2) ? boxMax.y : boxMin.y,
					(i & 4) ? boxMax.z : boxMin.z, 1.0f);
				const float axial = (corner - apex) * direction;
				if (i == 0) minAxis = maxAxis = axial;
				else
				{
					minAxis = std::min(minAxis, axial);
					maxAxis = std::max(maxAxis, axial);
				}
			}
			return maxAxis >= 0.0f && minAxis <= range;
		}

		struct ReferenceLight
		{
			Vector4 position;
			Vector4 direction = Vector4(0.0f, 0.0f, -1.0f, 0.0f);
			float range = 1.0f;
			float cosOuter = 1.0f;
			bool spot = false;
		};

		inline int AssignLights(const std::vector<ReferenceLight> &lights, const Vector4 &boxMin, const Vector4 &boxMax,
			std::uint32_t *outIndices, int &overflow)
		{
			int count = 0;
			overflow = 0;
			for (size_t i = 0; i < lights.size(); ++i)
			{
				const auto &light = lights[i];
				const bool intersects = light.spot
					? SpotIntersects(light.position, light.direction, light.range, light.cosOuter, boxMin, boxMax)
					: SphereIntersects(light.position, light.range, boxMin, boxMax);
				if (!intersects) continue;
				if (count < MAX_LIGHTS)
				{
					if (outIndices != nullptr) outIndices[count] = static_cast<std::uint32_t>(i);
					++count;
				}
				else
					++overflow;
			}
			return count;
		}
	}
}

#endif // _FURY_CLUSTER_GRID_H_
