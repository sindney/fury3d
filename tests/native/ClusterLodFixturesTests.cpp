#include "Fury/ClusterLodBuilder.h"
#include "Fury/ClusterLodSelection.h"
#include "Fury/ClusterLodValidation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace
{
	using namespace fury;

	int failures = 0;

	void Check(bool condition, const char *expression)
	{
		if (condition)
			return;
		std::cerr << "FAILED: " << expression << std::endl;
		++failures;
	}

#define CHECK(expression) Check((expression), #expression)

	using Edge = std::pair<std::uint32_t, std::uint32_t>;
	using Boundary = std::set<Edge>;
	using MaterialBoundaries = std::map<std::uint32_t, Boundary>;

	struct MeshData
	{
		std::vector<float> positions;
		std::vector<std::vector<unsigned int>> indices;
		std::vector<std::uint32_t> materials;
	};

	MeshData MakeCube()
	{
		MeshData mesh;
		mesh.positions = {
			-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1,
			-1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1
		};
		mesh.indices.push_back({
			0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
			0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
			0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5
		});
		mesh.materials = { 0 };
		return mesh;
	}

	MeshData MakeOpenGrid(bool multi_material)
	{
		MeshData mesh;
		const std::size_t side = 4;
		const std::size_t stride = side + 1;
		for (std::size_t y = 0; y < stride; ++y)
		{
			for (std::size_t x = 0; x < stride; ++x)
			{
				mesh.positions.insert(mesh.positions.end(), {
					static_cast<float>(x), static_cast<float>(y), 0.0f });
			}
		}
		mesh.indices.resize(multi_material ? 2 : 1);
		mesh.materials = multi_material ? std::vector<std::uint32_t>{ 2, 5 }
			: std::vector<std::uint32_t>{ 0 };
		for (std::size_t y = 0; y < side; ++y)
		{
			for (std::size_t x = 0; x < side; ++x)
			{
				const unsigned int a = static_cast<unsigned int>(y * stride + x);
				const unsigned int b = a + 1;
				const unsigned int c = a + static_cast<unsigned int>(stride);
				const unsigned int d = c + 1;
				const std::size_t material = multi_material && x >= side / 2 ? 1 : 0;
				mesh.indices[material].insert(mesh.indices[material].end(), { a, b, c, b, d, c });
			}
		}
		return mesh;
	}

	MeshData MakeDisconnected()
	{
		MeshData mesh = MakeCube();
		const MeshData second = MakeCube();
		const unsigned int offset = static_cast<unsigned int>(mesh.positions.size() / 3);
		for (std::size_t i = 0; i < second.positions.size(); i += 3)
		{
			mesh.positions.push_back(second.positions[i] + 10.0f);
			mesh.positions.push_back(second.positions[i + 1]);
			mesh.positions.push_back(second.positions[i + 2]);
		}
		for (unsigned int index : second.indices[0])
			mesh.indices[0].push_back(index + offset);
		return mesh;
	}

	MeshData MakeAttributeSeam()
	{
		MeshData mesh;
		mesh.positions = {
			0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0,
			1, 0, 0, 2, 0, 0, 2, 1, 0, 1, 1, 0
		};
		mesh.indices.push_back({ 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 });
		mesh.materials = { 0 };
		return mesh;
	}

	ClusterLodMeshView MakeView(const MeshData &mesh, std::vector<ClusterLodSubmeshView> &views)
	{
		views.clear();
		for (std::size_t i = 0; i < mesh.indices.size(); ++i)
			views.push_back({ mesh.indices[i].data(), mesh.indices[i].size(), mesh.materials[i] });
		ClusterLodMeshView view;
		view.positions = mesh.positions.data();
		view.vertex_count = mesh.positions.size() / 3;
		view.submeshes = views.data();
		view.submesh_count = views.size();
		return view;
	}

	void AddBoundaryEdges(Boundary &boundary, const unsigned int *indices, std::size_t index_count)
	{
		std::map<Edge, std::size_t> counts;
		for (std::size_t i = 0; i < index_count; i += 3)
		{
			for (std::size_t corner = 0; corner < 3; ++corner)
			{
				std::uint32_t a = indices[i + corner];
				std::uint32_t b = indices[i + (corner + 1) % 3];
				if (a > b)
					std::swap(a, b);
				++counts[Edge(a, b)];
			}
		}
		for (const auto &entry : counts)
		{
			if (entry.second == 1)
				boundary.insert(entry.first);
		}
	}

	void SourceBoundaries(const MeshData &mesh, Boundary &global, MaterialBoundaries &materials)
	{
		std::vector<unsigned int> all_indices;
		for (std::size_t i = 0; i < mesh.indices.size(); ++i)
		{
			all_indices.insert(all_indices.end(), mesh.indices[i].begin(), mesh.indices[i].end());
			AddBoundaryEdges(materials[mesh.materials[i]], mesh.indices[i].data(), mesh.indices[i].size());
		}
		AddBoundaryEdges(global, all_indices.data(), all_indices.size());
	}

	void SelectedBoundaries(const ClusterLodAsset &asset, const std::vector<std::uint32_t> &selected,
		Boundary &global, MaterialBoundaries &materials)
	{
		std::vector<unsigned int> all_indices;
		std::map<std::uint32_t, std::vector<unsigned int>> material_indices;
		for (std::uint32_t cluster_index : selected)
		{
			const ClusterLodCluster &cluster = asset.clusters[cluster_index];
			const auto begin = asset.merged_indices->begin() + cluster.first_index;
			all_indices.insert(all_indices.end(), begin, begin + cluster.index_count);
			auto &indices = material_indices[cluster.material_index];
			indices.insert(indices.end(), begin, begin + cluster.index_count);
		}
		AddBoundaryEdges(global, all_indices.data(), all_indices.size());
		for (const auto &entry : material_indices)
			AddBoundaryEdges(materials[entry.first], entry.second.data(), entry.second.size());
	}

	void TestFixture(const MeshData &mesh, const char *name)
	{
		std::vector<ClusterLodSubmeshView> submeshes;
		const ClusterLodMeshView view = MakeView(mesh, submeshes);
		ClusterLodBuildOptions options;
		options.cluster_triangle_limit = 16;
		options.cluster_vertex_limit = 32;
		options.group_cluster_limit = 4;
		options.target_error = 1.0f;
		const ClusterLodBuildResult result = BuildClusterLod(view, options);
		CHECK(result.asset != nullptr);
		if (!result)
		{
			std::cerr << name << ": " << result.error << std::endl;
			return;
		}

		Boundary source_global;
		MaterialBoundaries source_materials;
		SourceBoundaries(mesh, source_global, source_materials);
		ClusterLodSelectionParams params;
		params.camera_position[0] = 0.5f;
		params.camera_position[1] = 0.5f;
		params.camera_position[2] = 8.0f;
		params.projection_scale = 100.0f;
		std::size_t previous_triangles = std::numeric_limits<std::size_t>::max();
		for (float threshold : { 0.0f, 0.001f, 0.01f, 0.1f, 1.0f, 100.0f, 1e6f })
		{
			params.error_threshold = threshold;
			const auto selected = SelectClusterLodCut(*result.asset, params);
			std::string error;
			CHECK(ValidateClusterLodCut(*result.asset, selected, error));
			if (!error.empty())
				std::cerr << name << ": " << error << std::endl;
			Boundary selected_global;
			MaterialBoundaries selected_materials;
			SelectedBoundaries(*result.asset, selected, selected_global, selected_materials);
			CHECK(selected_global == source_global);
			CHECK(selected_materials == source_materials);
			std::size_t triangles = 0;
			for (std::uint32_t cluster_index : selected)
				triangles += result.asset->clusters[cluster_index].index_count / 3;
			CHECK(triangles <= previous_triangles);
			previous_triangles = triangles;
		}

		ClusterLodAsset invalid = *result.asset;
		invalid.clusters[0].parent_error = invalid.clusters[0].error - 1.0f;
		std::string error;
		CHECK(!ValidateClusterLodAsset(invalid, view.vertex_count, error));
	}

	void TestCutRejection()
	{
		const MeshData mesh = MakeOpenGrid(false);
		std::vector<ClusterLodSubmeshView> submeshes;
		ClusterLodBuildOptions options;
		options.target_error = 1.0f;
		const ClusterLodBuildResult result = BuildClusterLod(MakeView(mesh, submeshes), options);
		CHECK(result.asset != nullptr);
		if (!result)
			return;
		std::string error = "stale";
		CHECK(!ValidateClusterLodCut(*result.asset, {}, error));
		ClusterLodSelectionParams params;
		params.error_threshold = 1e6f;
		params.projection_scale = 100.0f;
		auto selected = SelectClusterLodCut(*result.asset, params);
		if (selected.size() > 1)
		{
			selected.pop_back();
			CHECK(!ValidateClusterLodCut(*result.asset, selected, error));
		}
	}
}

int main()
{
	const MeshData cube = MakeCube();
	const MeshData open_grid = MakeOpenGrid(false);
	const MeshData disconnected = MakeDisconnected();
	const MeshData seam = MakeAttributeSeam();
	const MeshData multi_material = MakeOpenGrid(true);
	TestFixture(cube, "closed");
	TestFixture(open_grid, "open");
	TestFixture(disconnected, "disconnected");
	TestFixture(seam, "attribute seam");
	TestFixture(multi_material, "multi material");
	TestCutRejection();
	if (failures != 0)
	{
		std::cerr << failures << " cluster LOD fixture checks failed" << std::endl;
		return 1;
	}
	std::cout << "Cluster LOD fixture tests passed" << std::endl;
	return 0;
}
