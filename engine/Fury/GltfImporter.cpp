// GltfImporter.cpp — tinygltf::Model -> engine Scene translator.
//
// This file grows across implementation groups 2-9 (skeleton, rejection,
// material, texture, mesh, skin, scene-node, animation). The first cut just
// handles top-level Load and rejection of unsupported features — enough for
// `fury convert gltf` to refuse bad inputs cleanly while the asset-translation
// passes are written.

#include "Fury/GltfImporter.h"

#include "Fury/Log.h"
#include "Fury/OcTree.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"

// tinygltf pulls in its own JSON header; we suppress its stb_image to avoid
// ODR collision with the engine's STB (same as in engine/CMakeLists.txt).
#include <tiny_gltf.h>

#include <algorithm>

namespace fury
{
	namespace
	{
		std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool HasSuffix(const std::string &path, const std::string &suffix)
		{
			if (path.size() < suffix.size()) return false;
			return ToLower(path.substr(path.size() - suffix.size())) == suffix;
		}

		// Returns true if the model has any feature we don't support in v1.
		// Logs the first rejection reason and short-circuits.
		bool HasUnsupportedFeatures(const tinygltf::Model &model, const std::string &input_path)
		{
			if (!model.extensionsRequired.empty())
			{
				std::string names;
				for (const auto &n : model.extensionsRequired) names += " " + n;
				FURYE << "gltf-importer: rejected — extensionsRequired (" << names
					<< ") not supported in v1 (input: " << input_path
					<< "; see docs/CLI.md §Limitations)";
				return true;
			}

			for (size_t bv_i = 0; bv_i < model.bufferViews.size(); ++bv_i)
			{
				if (model.bufferViews[bv_i].byteStride != 0)
				{
					FURYE << "gltf-importer: rejected — bufferViews[" << bv_i
						<< "].byteStride=" << model.bufferViews[bv_i].byteStride
						<< " not supported in v1 (input: " << input_path
						<< "; see docs/CLI.md §Limitations)";
					return true;
				}
			}

			for (size_t a_i = 0; a_i < model.accessors.size(); ++a_i)
			{
				if (model.accessors[a_i].sparse.isSparse)
				{
					FURYE << "gltf-importer: rejected — accessors[" << a_i
						<< "] is sparse, not supported in v1 (input: " << input_path
						<< "; see docs/CLI.md §Limitations)";
					return true;
				}
			}

			for (size_t m_i = 0; m_i < model.meshes.size(); ++m_i)
			{
				const auto &mesh = model.meshes[m_i];
				for (size_t p_i = 0; p_i < mesh.primitives.size(); ++p_i)
				{
					const auto &prim = mesh.primitives[p_i];
					if (!prim.targets.empty())
					{
						FURYE << "gltf-importer: rejected — meshes[" << m_i
							<< "].primitives[" << p_i << "] has " << prim.targets.size()
							<< " morph target(s), not supported in v1 (input: " << input_path
							<< "; see docs/CLI.md §Limitations)";
						return true;
					}
					if (prim.mode != TINYGLTF_MODE_TRIANGLES)
					{
						FURYE << "gltf-importer: rejected — meshes[" << m_i
							<< "].primitives[" << p_i << "].mode=" << prim.mode
							<< " (only TINYGLTF_MODE_TRIANGLES=4 supported in v1; input: "
							<< input_path << ")";
						return true;
					}
				}
			}

			return false;
		}
	}

	std::shared_ptr<Scene> GltfImporter::Import(
		const std::string &input_path,
		const std::string &scene_name,
		const std::string &working_dir,
		const Options &opts)
	{
		(void)opts;

		tinygltf::TinyGLTF loader;
		tinygltf::Model model;
		std::string err, warn;

		bool ok = false;
		if (HasSuffix(input_path, ".glb"))
			ok = loader.LoadBinaryFromFile(&model, &err, &warn, input_path);
		else if (HasSuffix(input_path, ".gltf"))
			ok = loader.LoadASCIIFromFile(&model, &err, &warn, input_path);
		else
		{
			FURYE << "gltf-importer: input '" << input_path
				<< "' has neither .gltf nor .glb extension";
			return nullptr;
		}

		if (!warn.empty()) FURYW << "gltf-importer: tinygltf warn: " << warn;
		if (!ok || !err.empty())
		{
			FURYE << "gltf-importer: failed to load '" << input_path
				<< "': " << (err.empty() ? "(no error message)" : err);
			return nullptr;
		}

		if (HasUnsupportedFeatures(model, input_path))
			return nullptr;

		// Per-pass translation lands across groups 4-9 below. For now we
		// return an empty scene that proves the Load + rejection paths work
		// end-to-end. The subsequent commits will fill in materials, meshes,
		// skins, the node tree, and animations.
		FURYI << "gltf-importer: loaded '" << input_path << "' — model has "
			<< model.meshes.size() << " mesh(es), " << model.materials.size()
			<< " material(s), " << model.skins.size() << " skin(s), "
			<< model.animations.size() << " animation(s); translation passes "
			<< "are stubs in this build";

		auto tree = OcTree::Create(Vector4(-1000), Vector4(1000), 2);
		return Scene::Create(scene_name, working_dir, tree);
	}
}
