const int CLUSTER_TILES_X = 16;
const int CLUSTER_TILES_Y = 8;
const int CLUSTER_SLICES_Z = 24;
const int CLUSTER_COUNT = CLUSTER_TILES_X * CLUSTER_TILES_Y * CLUSTER_SLICES_Z;
const int CLUSTER_MAX_LIGHTS = 128;

int ClusterDepthSlice(float viewDepth, vec2 nearFar)
{
	if (viewDepth <= nearFar.x) return 0;
	if (viewDepth >= nearFar.y) return CLUSTER_SLICES_Z - 1;
	float scale = float(CLUSTER_SLICES_Z) / log(nearFar.y / nearFar.x);
	return clamp(int(floor(log(viewDepth / nearFar.x) * scale)), 0, CLUSTER_SLICES_Z - 1);
}

float ClusterSliceBoundary(int slice, vec2 nearFar)
{
	return nearFar.x * pow(nearFar.y / nearFar.x, float(slice) / float(CLUSTER_SLICES_Z));
}

uvec3 ClusterCoord(vec2 pixel, float viewDepth, vec2 viewport, vec2 nearFar)
{
	uvec2 xy = uvec2(clamp(ivec2(floor(pixel / viewport * vec2(CLUSTER_TILES_X, CLUSTER_TILES_Y))),
		ivec2(0), ivec2(CLUSTER_TILES_X - 1, CLUSTER_TILES_Y - 1)));
	return uvec3(xy, ClusterDepthSlice(viewDepth, nearFar));
}

uvec3 ClusterCoordFromLinearDepth(vec2 pixel, float normalizedDepth, vec2 viewport, vec2 nearFar)
{
	return ClusterCoord(pixel, normalizedDepth * nearFar.y, viewport, nearFar);
}

int ClusterIndex(uvec3 coord)
{
	return (int(coord.z) * CLUSTER_TILES_Y + int(coord.y)) * CLUSTER_TILES_X + int(coord.x);
}

int ClusterIndexFromLinearDepth(vec2 pixel, float normalizedDepth, vec2 viewport, vec2 nearFar)
{
	return ClusterIndex(ClusterCoordFromLinearDepth(pixel, normalizedDepth, viewport, nearFar));
}

void ClusterBounds(uvec3 coord, vec2 nearFar, vec2 tanHalfFov,
	out vec3 boxMin, out vec3 boxMax)
{
	float z0 = ClusterSliceBoundary(int(coord.z), nearFar);
	float z1 = ClusterSliceBoundary(int(coord.z) + 1, nearFar);
	vec2 ndc0 = vec2(coord.xy) / vec2(CLUSTER_TILES_X, CLUSTER_TILES_Y) * 2.0 - 1.0;
	vec2 ndc1 = vec2(coord.xy + uvec2(1)) / vec2(CLUSTER_TILES_X, CLUSTER_TILES_Y) * 2.0 - 1.0;
	vec2 x0 = ndc0.x * tanHalfFov.x * vec2(z0, z1);
	vec2 x1 = ndc1.x * tanHalfFov.x * vec2(z0, z1);
	vec2 y0 = ndc0.y * tanHalfFov.y * vec2(z0, z1);
	vec2 y1 = ndc1.y * tanHalfFov.y * vec2(z0, z1);
	boxMin = vec3(min(min(x0.x, x0.y), min(x1.x, x1.y)),
		min(min(y0.x, y0.y), min(y1.x, y1.y)), -z1);
	boxMax = vec3(max(max(x0.x, x0.y), max(x1.x, x1.y)),
		max(max(y0.x, y0.y), max(y1.x, y1.y)), -z0);
}

bool ClusterSphereIntersects(vec3 center, float radius, vec3 boxMin, vec3 boxMax)
{
	vec3 delta = center - clamp(center, boxMin, boxMax);
	return dot(delta, delta) <= radius * radius;
}

bool ClusterSpotIntersects(vec3 apex, vec3 direction, float range, float cosOuter,
	vec3 boxMin, vec3 boxMax)
{
	float safeCos = max(cosOuter, 0.0001);
	float sinOuter = sqrt(max(0.0, 1.0 - safeCos * safeCos));
	vec3 center = apex + direction * (range * 0.5);
	float radius = sqrt(range * range * 0.25 + range * range * sinOuter * sinOuter / (safeCos * safeCos));
	if (!ClusterSphereIntersects(center, radius, boxMin, boxMax)) return false;

	float minAxis = 0.0;
	float maxAxis = 0.0;
	for (int i = 0; i < 8; ++i)
	{
		vec3 corner = vec3(
			(i & 1) != 0 ? boxMax.x : boxMin.x,
			(i & 2) != 0 ? boxMax.y : boxMin.y,
			(i & 4) != 0 ? boxMax.z : boxMin.z);
		float axial = dot(corner - apex, direction);
		if (i == 0) minAxis = maxAxis = axial;
		else
		{
			minAxis = min(minAxis, axial);
			maxAxis = max(maxAxis, axial);
		}
	}
	return maxAxis >= 0.0 && minAxis <= range;
}
