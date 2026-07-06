#ifndef _FURY_MESHSIMPLIFIER_H_
#define _FURY_MESHSIMPLIFIER_H_

#include <memory>
#include <vector>

namespace fury
{
	class Mesh;

	// Options for meshopt-driven LOD generation. The wrapper builds
	// `lod_count` Mesh objects below the source: each level targets
	// `previous * reduction_ratio` of the previous triangle count.
	// Thresholds are emitted as a 1.0 -> 0.0 linear ramp with `lod_count`
	// interior points (LOD 0 keeps the source mesh; threshold 1.0 is
	// implied for the source itself).
	struct MeshSimplifyOptions
	{
		// Number of LODs to generate (excluding the source = LOD 0).
		// Clamped to [1, 5].
		int lod_count = 3;

		// Per-level reduction factor in (0, 1). 0.5 means each level
		// targets 50% of the previous level's triangle count.
		float reduction_ratio = 0.5f;

		// meshopt_simplify target error in mesh units. Larger = more
		// aggressive simplification at the cost of geometric drift.
		// The default of 0.5 is tuned for meshopt_simplifySloppy
		// (the algorithm we use by default): it maps to a coarse grid
		// that produces visible reduction on typical meshes
		// (including meshes with many locked border vertices, which
		// would otherwise prevent meshopt_simplify from reducing).
		// For meshopt_simplify (lock_borders=true path) try 0.001
		// for near-lossless reduction.
		// 0 = exact target_index_count (no error allowed).
		float target_error = 0.5f;

		// Append a default border-lock mask to the source vertex_lock
		// buffer so seam vertices (referenced by exactly one submesh)
		// are preserved across simplification. When false, the
		// simplifier is free to collapse them.
		bool lock_borders = true;
	};

	struct MeshSimplifyResult
	{
		// Newly-generated LOD meshes, ordered LOD 1 .. LOD N.
		// LOD 0 is the source mesh itself (not in this vector).
		std::vector<std::shared_ptr<Mesh>> lod_meshes;

		// screen-coverage thresholds for the new LODs. Length equals
		// lod_meshes.size(). Generated as 1 - ratio, 1 - ratio^2, ...
		// clamped so the last entry is 0.
		std::vector<float> thresholds;
	};

	// Generate `lod_count` simplified LODs from `source` using
	// meshoptimizer's meshopt_simplify. Each output Mesh preserves
	// the source's submesh count (per-submesh simplification), the
	// source's shadow flag, and the source's position/normal/uv/tangent
	// data (gathered into a fresh packed buffer via
	// meshopt_generateVertexRemap + meshopt_remapVertexBuffer).
	//
	// On failure (empty source, zero-index submesh, simplifier
	// returns 0 indices, etc.) returns an empty result and logs
	// FURYE. The source Mesh is never modified.
	MeshSimplifyResult SimplifyMesh(const std::shared_ptr<Mesh> &source,
									const MeshSimplifyOptions &opts);
}

#endif // _FURY_MESHSIMPLIFIER_H_
