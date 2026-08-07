#include "Fury/MeshSimplifier.h"

#include "Fury/Log.h"
#include "Fury/Mesh.h"

#include <algorithm>
#include <cstring>
#include <functional>

// meshoptimizer v0.19's header has MESHOPTIMIZER_API commented out
// in this vendored copy; define it as a no-op macro so the declarations
// compile. (Newer upstream versions define it unconditionally.)
#ifndef MESHOPTIMIZER_API
#define MESHOPTIMIZER_API
#endif

#include "meshoptimizer.h"

namespace fury
{
	namespace
	{
		// Helper: build a packed Mesh::Ptr with the same vertex-layout
		// as `source` (positions, normals, tangents, uvs) but with the
		// given per-submesh index lists. SubMesh count is preserved.
		// Vertex buffers are empty (the simplifier wires them in
		// separately so we can stream attributes one at a time).
		std::shared_ptr<Mesh> MakeLodShell(const std::shared_ptr<Mesh> &source,
										   const std::string &name)
		{
			auto out = Mesh::Create(name);
			out->SetCastShadows(source->GetCastShadows());
			return out;
		}

		// Pack a single attribute stream. `dest` is the output flat
		// array (already sized to new_vertex_count * components * sizeof(T)).
		// `remap` is the v0.19 meshopt_generateVertexRemap output: a
		// table indexed by source-vertex-id (size = vertex_count),
		// where remap[i] = new output index for source vertex i, or
		// ~0u if source i isn't referenced.
		// `source_data` is the source flat array (size = vertex_count * components).
		template <typename T>
		void PackAttribute(T *dest,
						   const unsigned int *remap,
						   const T *source_data, size_t vertex_count,
						   size_t components)
		{
			for (size_t i = 0; i < vertex_count; ++i)
			{
				unsigned int dst = remap[i];
				if (dst == ~0u) continue; // unreferenced
				std::memcpy(dest + dst * components,
							source_data + i * components,
							components * sizeof(T));
			}
		}

		// Per-submesh simplification: takes the source's positions and
		// the submesh's index buffer, runs meshopt_simplify, then
		// gathers the referenced vertices into fresh attribute buffers
		// via meshopt_generateVertexRemap + meshopt_remapVertexBuffer.
		// Returns the new submesh's index buffer via `out_indices`, the
		// packed attribute buffers via the out-params, and the new
		// vertex count via `out_vertex_count`. Returns false on any
		// error (logged FURYE).
		bool SimplifySubMesh(const std::shared_ptr<Mesh> &source,
							 unsigned int sub_index,
							 size_t target_indices,
							 float target_error,
							 unsigned int options,
							 MeshSimplifyOptions::Method method,
							 // outputs
							 std::vector<unsigned int> &out_indices,
							 std::vector<float> &out_positions,
							 std::vector<float> &out_normals,
							 std::vector<float> &out_tangents,
							 std::vector<float> &out_uvs,
							 size_t &out_vertex_count)
		{
			auto sub = source->GetSubMeshAt(sub_index);
			if (!sub)
			{
				FURYE << "MeshSimplifier: source submesh " << sub_index << " is null";
				return false;
			}
			const auto &src_indices = sub->Indices.Data;
			const auto &src_positions = source->Positions.Data;
			const auto &src_normals = source->Normals.Data;
			const auto &src_tangents = source->Tangents.Data;
			const auto &src_uvs = source->UVs.Data;
			const size_t vertex_count = src_positions.size() / 3;
			const size_t index_count = src_indices.size();
			if (vertex_count == 0 || index_count < 3 || (index_count % 3) != 0)
			{
				FURYE << "MeshSimplifier: source submesh " << sub_index
					  << " is degenerate (verts=" << vertex_count
					  << ", indices=" << index_count << ")";
				return false;
			}
			if (index_count % 3 != 0)
			{
				FURYE << "MeshSimplifier: source submesh " << sub_index
					  << " has non-triangle topology (indices=" << index_count << ")";
				return false;
			}

			// 1. Run the simplifier. The output is a new index buffer
			// referencing the original vertices.
			std::vector<unsigned int> simplified_indices(index_count);
			size_t simplified_count;
			if (method == MeshSimplifyOptions::Method::Sloppy)
			{
				// meshopt_simplifySloppy: grid-based, aggressive. Doesn't
				// respect border vertices but produces visible
				// reduction for typical meshes that meshopt_simplify
				// can't reduce (e.g. meshes with many UV seams).
				simplified_count = meshopt_simplifySloppy(
					simplified_indices.data(),
					src_indices.data(), index_count,
					src_positions.data(), vertex_count, sizeof(float) * 3,
					target_indices, target_error, nullptr);
			}
			else
			{
				// Quadric / QuadricLegacy share meshopt_simplify but
				// differ in the options mask: Quadric preserves the
				// lock_borders flag (typically
				// meshopt_SimplifyLockBorder), QuadricLegacy forces
				// it off to match the old default.
				const unsigned int method_options =
					(method == MeshSimplifyOptions::Method::Quadric) ? options : 0u;
				simplified_count = meshopt_simplify(
					simplified_indices.data(),
					src_indices.data(), index_count,
					src_positions.data(), vertex_count, sizeof(float) * 3,
					target_indices, // actual target (already shared-out)
					target_error,
					method_options, nullptr);
			}
			if (simplified_count < 3 || (simplified_count % 3) != 0)
			{
				FURYE << "MeshSimplifier: meshopt_simplify returned " << simplified_count
					  << " indices for submesh " << sub_index;
				return false;
			}
			simplified_indices.resize(simplified_count);

			// 2. Generate a remap table: for each output vertex, the
			// source-vertex index it should pull attributes from. The
			// returned remap is sized to the new vertex count.
			std::vector<unsigned int> remap(vertex_count);
			const size_t new_vertex_count = meshopt_generateVertexRemap(
				remap.data(),
				simplified_indices.data(), simplified_count,
				source->Positions.Data.data(), vertex_count, sizeof(float) * 3);
			if (new_vertex_count == 0)
			{
				FURYE << "MeshSimplifier: meshopt_generateVertexRemap returned 0";
				return false;
			}
			remap.resize(new_vertex_count);

			// 3. Pack each attribute. meshopt_remapVertexBuffer can do
			// this in one call per attribute; the manual PackAttribute
			// path is equivalent but lets us skip attributes the source
			// doesn't carry.
			auto pack = [&](const std::vector<float> &src, size_t components,
							std::vector<float> &dst, const char *name)
			{
				(void)name;
				if (src.size() < vertex_count * components)
				{
					// Source is missing this attribute; emit a default
					// (zeros) buffer so the LOD mesh's attribute layout
					// stays aligned with the source's.
					dst.assign(new_vertex_count * components, 0.0f);
					return;
				}
				dst.resize(new_vertex_count * components);
				PackAttribute(dst.data(), remap.data(),
							  src.data(), vertex_count, components);
			};
			pack(src_positions, 3, out_positions, "positions");
			pack(src_normals, 3, out_normals, "normals");
			// Tangents are vec4 in glTF but the engine stores 3
			// components (drops the w/handedness). The simplifier
			// doesn't need tangents; we just pass them through.
			pack(src_tangents, 3, out_tangents, "tangents");
			pack(src_uvs, 2, out_uvs, "uvs");

			// 4. Reorder the simplified indices through the remap so
			// they point into the new packed buffer. Pre-size the
			// output buffer -- meshopt_remapIndexBuffer writes into
			// the destination without growing it.
			out_indices.assign(simplified_count, 0u);
			meshopt_remapIndexBuffer(
				out_indices.data(),
				simplified_indices.data(), simplified_count,
				remap.data());
			out_indices.resize(simplified_count);

			out_vertex_count = new_vertex_count;
			return true;
		}
	} // namespace

	MeshSimplifyResult SimplifyMesh(const std::shared_ptr<Mesh> &source,
									const MeshSimplifyOptions &opts)
	{
		MeshSimplifyResult result;
		if (!source)
		{
			FURYE << "MeshSimplifier: source mesh is null";
			return result;
		}
		if (source->Positions.Data.empty())
		{
			FURYE << "MeshSimplifier: source mesh '" << source->GetName()
				  << "' has no position data";
			return result;
		}
		// Index data lives on the submeshes (multi-submesh meshes) or on
		// the parent mesh's flat `Indices` buffer (single-submesh case).
		// Accept either.
		bool has_indices = !source->Indices.Data.empty();
		if (!has_indices) {
			for (unsigned int s = 0; s < source->GetSubMeshCount(); ++s) {
				if (auto sm = source->GetSubMeshAt(s)) {
					if (!sm->Indices.Data.empty()) { has_indices = true; break; }
				}
			}
		}
		if (!has_indices)
		{
			FURYE << "MeshSimplifier: source mesh '" << source->GetName()
				  << "' has no indices data";
			return result;
		}

		const int lod_count = std::clamp(opts.lod_count, 1, 5);
		const float ratio = std::clamp(opts.reduction_ratio, 0.05f, 0.95f);
		const float target_error = std::max(0.0f, opts.target_error);
		const unsigned int options = opts.lock_borders
										 ? meshopt_SimplifyLockBorder : 0u;
		// Dispatch on the requested simplification method -- see
		// MeshSimplifyOptions::Method. Quadric is the default;
		// Sloppy uses the grid-based variant; QuadricLegacy skips
		// the border-lock flag.
		const MeshSimplifyOptions::Method method = opts.method;

		// Compute per-level target triangle count. Level 0 is the
		// source (highest detail). Each subsequent level is ratio of
		// the previous. Total index count is the sum across all
		// submeshes when the source has submeshes; otherwise it's
		// the parent mesh's flat index buffer.
		size_t total_indices = source->Indices.Data.size();
		if (source->GetSubMeshCount() > 0) {
			for (unsigned int s = 0; s < source->GetSubMeshCount(); ++s) {
				if (auto sm = source->GetSubMeshAt(s))
					total_indices += sm->Indices.Data.size();
			}
		}
		std::vector<size_t> target_indices_per_level(lod_count);
		float current = static_cast<float>(total_indices);
		for (int i = 0; i < lod_count; ++i)
		{
			current *= ratio;
			// Ensure at least one triangle (3 indices) per level so
			// the simplifier has something to emit.
			size_t target = static_cast<size_t>(current);
			if (target < 3) target = 3;
			if (target > total_indices) target = total_indices;
			// Round down to a multiple of 3.
			target -= (target % 3);
			target_indices_per_level[i] = target;
		}

		// Build threshold ramp. Threshold[i] is the screen-coverage at
		// which the i-th LOD becomes active. The list is non-increasing
		// (LOD 0 -> 1.0, LOD 1 -> 1 - ratio, ..., LOD N -> 0).
		result.thresholds.resize(lod_count);
		for (int i = 0; i < lod_count; ++i)
		{
			float t = 1.0f;
			for (int j = 0; j <= i; ++j) t *= (1.0f - ratio);
			if (i == lod_count - 1) t = 0.0f; // deepest LOD snaps to 0
			result.thresholds[i] = t;
		}

		// For each LOD level, simplify each submesh independently and
		// build a fresh Mesh with the same submesh count.
		const std::string base_name = source->GetName();
		for (int level = 0; level < lod_count; ++level)
		{
			const size_t target_indices = target_indices_per_level[level];
			const std::string lod_name = base_name + "_LOD" + std::to_string(level + 1);

			auto lod = MakeLodShell(source, lod_name);
			const unsigned int sub_count = source->GetSubMeshCount();
			for (unsigned int s = 0; s < std::max(1u, sub_count); ++s)
			{
				if (sub_count == 0 && s > 0) break;
				std::vector<unsigned int> new_indices;
				std::vector<float> new_positions, new_normals, new_tangents, new_uvs;
				size_t new_vertex_count = 0;
				if (sub_count == 0)
				{
					// Mesh has no submeshes -- simplify the parent
					// index buffer directly. Build a temporary
					// "submesh" view that uses source->Indices.
					std::vector<unsigned int> simplified_indices(source->Indices.Data.size());
					size_t simplified_count;
					if (method == MeshSimplifyOptions::Method::Sloppy)
					{
						simplified_count = meshopt_simplifySloppy(
							simplified_indices.data(),
							source->Indices.Data.data(), source->Indices.Data.size(),
							source->Positions.Data.data(), source->Positions.Data.size() / 3,
							sizeof(float) * 3,
							target_indices, target_error, nullptr);
					}
					else
					{
						const unsigned int method_options =
							(method == MeshSimplifyOptions::Method::Quadric) ? options : 0u;
						simplified_count = meshopt_simplify(
							simplified_indices.data(),
							source->Indices.Data.data(), source->Indices.Data.size(),
							source->Positions.Data.data(), source->Positions.Data.size() / 3,
							sizeof(float) * 3,
							target_indices, target_error, method_options, nullptr);
					}
					if (simplified_count < 3 || (simplified_count % 3) != 0)
					{
						FURYE << "MeshSimplifier: parent-mesh simplification returned "
							  << simplified_count << " indices";
						return result;
					}
					simplified_indices.resize(simplified_count);

					std::vector<unsigned int> remap(source->Positions.Data.size() / 3);
					new_vertex_count = meshopt_generateVertexRemap(
						remap.data(),
						simplified_indices.data(), simplified_count,
						source->Positions.Data.data(), source->Positions.Data.size() / 3,
						sizeof(float) * 3);
					if (new_vertex_count == 0) return result;
					remap.resize(new_vertex_count);

					auto pack = [&](const std::vector<float> &src, size_t components,
									std::vector<float> &dst)
					{
						const size_t parent_vc = source->Positions.Data.size() / 3;
						if (src.size() < parent_vc * components)
						{
							dst.assign(new_vertex_count * components, 0.0f);
							return;
						}
						dst.resize(new_vertex_count * components);
						PackAttribute(dst.data(), remap.data(),
									  src.data(), parent_vc, components);
					};
					pack(source->Positions.Data, 3, new_positions);
					pack(source->Normals.Data, 3, new_normals);
					pack(source->Tangents.Data, 3, new_tangents);
					pack(source->UVs.Data, 2, new_uvs);

					new_indices.assign(simplified_count, 0u);
					meshopt_remapIndexBuffer(
						new_indices.data(),
						simplified_indices.data(), simplified_count,
						remap.data());
					new_indices.resize(simplified_count);
				}
				else
				{
					// Per-submesh simplification. We need a level-
					// distributed target: each submesh contributes its
					// share of the original total.
					const size_t this_sub_indices = source->GetSubMeshAt(s)->Indices.Data.size();
					const size_t sub_share = sub_count > 0
						? (this_sub_indices * target_indices) / std::max<size_t>(1, total_indices)
						: target_indices;
					if (!SimplifySubMesh(source, s, sub_share, target_error, options,
										 method,
										 new_indices, new_positions, new_normals,
										 new_tangents, new_uvs, new_vertex_count))
					{
						return result;
					}
				}

				// Write the new attribute buffers + submesh to the
				// LOD mesh. The LOD mesh's vertex data is the union of
				// all its submeshes' positions (the engine expects
				// shared per-vertex buffers across submeshes), so we
				// append each submesh's vertices to the LOD's
				// Positions/Normals/Tangents/UVs and renumber the
				// submesh's indices by the running vertex offset.
				const size_t v_offset = lod->Positions.Data.size() / 3;
				lod->Positions.Data.insert(lod->Positions.Data.end(),
										   new_positions.begin(), new_positions.end());
				lod->Normals.Data.insert(lod->Normals.Data.end(),
										 new_normals.begin(), new_normals.end());
				lod->Tangents.Data.insert(lod->Tangents.Data.end(),
										  new_tangents.begin(), new_tangents.end());
				lod->UVs.Data.insert(lod->UVs.Data.end(),
									 new_uvs.begin(), new_uvs.end());

				auto sub_out = SubMesh::Create();
				sub_out->Indices.Data.reserve(new_indices.size());
				for (auto idx : new_indices)
					sub_out->Indices.Data.push_back(static_cast<unsigned int>(v_offset + idx));
				lod->AddSubMesh(sub_out);
			}

			lod->CalculateAABB();
			result.lod_meshes.push_back(lod);
		}

		FURYI << "MeshSimplifier: generated " << result.lod_meshes.size()
			  << " LOD(s) for '" << base_name << "' (target tri per level:";
		for (size_t i = 0; i < result.lod_meshes.size(); ++i)
		{
			const auto &m = result.lod_meshes[i];
			// Sum triangles across all submeshes -- the parent
			// mesh's flat Indices buffer is empty for our LOD meshes.
			size_t tri = 0;
			for (unsigned int s = 0; s < m->GetSubMeshCount(); ++s)
				if (auto sm = m->GetSubMeshAt(s))
					tri += sm->Indices.Data.size() / 3;
			FURYI << "  " << m->GetName() << " -- verts=" << (m->Positions.Data.size() / 3)
				  << ", tris=" << tri << ", threshold=" << result.thresholds[i]
				  << ", target_tri=" << (target_indices_per_level[i] / 3);
		}
		FURYI << ")";

		return result;
	}
}
