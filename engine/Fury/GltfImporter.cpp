// GltfImporter.cpp — tinygltf::Model -> engine Scene translator.
//
// This file grows across implementation groups 2-9 (skeleton, rejection,
// material, texture, mesh, skin, scene-node, animation). The first cut just
// handles top-level Load and rejection of unsupported features — enough for
// `fury convert gltf` to refuse bad inputs cleanly while the asset-translation
// passes are written.

#include "Fury/GltfImporter.h"

#include "Fury/EntityManager.h"
#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/OcTree.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"

// tinygltf pulls in its own JSON header; we suppress its stb_image to avoid
// ODR collision with the engine's STB (same as in engine/CMakeLists.txt).
#include <tiny_gltf.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <string>

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

		// Derive an on-disk URI for a glTF image. For external-file images
		// (image.uri non-empty, no embedded buffer view) we pass the URI
		// through verbatim — Texture::Load will resolve it against
		// Scene::Path() at runtime. For .glb-embedded images (image.uri
		// empty, bufferView set) we synthesize a path next to the output:
		// "<output_stem>_image<i>.<ext>". The actual bytes are extracted
		// in ExtractEmbeddedImage() when the converter writes the scene.
		std::string DeriveImageUri(
			const tinygltf::Model &model,
			int image_index,
			const std::string &output_basename_no_ext)
		{
			if (image_index < 0 || image_index >= static_cast<int>(model.images.size()))
				return "";
			const auto &image = model.images[image_index];
			if (!image.uri.empty())
				return image.uri;
			// Embedded image — Group 5 will extract bytes to this file. We
			// derive the extension from mimeType.
			std::string ext = ".png";
			if (image.mimeType == "image/jpeg") ext = ".jpg";
			else if (image.mimeType == "image/bmp") ext = ".bmp";
			return output_basename_no_ext + "_image" + std::to_string(image_index) + ext;
		}

		// Map a glTF texture index to the engine Texture::Ptr we cached for it.
		Texture::Ptr CreateEngineTexture(
			const tinygltf::Model &model,
			int texture_index,
			bool srgb,
			const std::string &output_basename_no_ext)
		{
			if (texture_index < 0 || texture_index >= static_cast<int>(model.textures.size()))
				return nullptr;
			const auto &gtex = model.textures[texture_index];
			const std::string uri = DeriveImageUri(model, gtex.source, output_basename_no_ext);
			if (uri.empty()) return nullptr;

			// Name: prefer the image's name or the URI basename; the engine
			// looks up textures by name in some paths.
			std::string name = uri;
			auto slash = uri.find_last_of("/\\");
			if (slash != std::string::npos) name = uri.substr(slash + 1);

			auto tex = Texture::Create(name);
			tex->SetFilePathAndSRGB(uri, srgb);

			// Sampler-derived filter / wrap. tinygltf::Texture has a sampler
			// index; we copy the basics. For v1 use the engine's defaults
			// when no sampler is present.
			if (gtex.sampler >= 0 && gtex.sampler < static_cast<int>(model.samplers.size()))
			{
				const auto &sampler = model.samplers[gtex.sampler];
				// glTF wrap: 33071=CLAMP_TO_EDGE, 33648=MIRRORED_REPEAT, 10497=REPEAT
				if (sampler.wrapS == 33071) tex->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
				else if (sampler.wrapS == 33648) tex->SetWrapMode(WrapMode::MIRRORED_REPEAT);
				else tex->SetWrapMode(WrapMode::REPEAT);
				// glTF mag filter: 9728=NEAREST, 9729=LINEAR
				if (sampler.magFilter == 9728) tex->SetFilterMode(FilterMode::NEAREST);
				else tex->SetFilterMode(FilterMode::LINEAR);
			}
			return tex;
		}

		// Translate one glTF material -> one engine Material. Lossy: PBR
		// metallic-roughness becomes Lambert. baseColorFactor -> diffuse_color,
		// baseColorTexture -> diffuse_texture slot, emissiveFactor ->
		// emissive_color, alphaMode -> opaque flag. Other PBR fields are read
		// but discarded with a one-shot warning per material.
		//
		// already_warned: signatures of discarded-field sets we've already
		// reported, so identical materials don't spam the log.
		Material::Ptr TranslateMaterial(
			const tinygltf::Model &model,
			int material_index,
			const std::string &output_basename_no_ext,
			std::set<std::string> &already_warned)
		{
			const auto &gm = model.materials[material_index];
			const std::string name = gm.name.empty()
				? "Material_" + std::to_string(material_index)
				: gm.name;
			auto material = Material::Create(name);

			// alphaMode -> opaque
			material->SetOpaque(gm.alphaMode != "BLEND" && gm.alphaMode != "MASK");

			// baseColorFactor -> diffuse_color (rgb) + transparency (1 - a)
			const auto &bcf = gm.pbrMetallicRoughness.baseColorFactor;
			float r = bcf.size() > 0 ? static_cast<float>(bcf[0]) : 1.0f;
			float g = bcf.size() > 1 ? static_cast<float>(bcf[1]) : 1.0f;
			float b = bcf.size() > 2 ? static_cast<float>(bcf[2]) : 1.0f;
			float a = bcf.size() > 3 ? static_cast<float>(bcf[3]) : 1.0f;
			material->SetUniform(Material::DIFFUSE_COLOR, Uniform3f::Create({ r, g, b }));
			material->SetUniform(Material::TRANSPARENCY, Uniform1f::Create({ 1.0f - a }));

			// baseColorTexture -> diffuse_texture slot
			if (gm.pbrMetallicRoughness.baseColorTexture.index >= 0)
			{
				auto tex = CreateEngineTexture(model,
					gm.pbrMetallicRoughness.baseColorTexture.index,
					/*srgb=*/true,                  // base color is colorspace data
					output_basename_no_ext);
				if (tex) material->SetTexture(Material::DIFFUSE_TEXTURE, tex);
			}

			// emissiveFactor -> emissive_color
			const auto &ef = gm.emissiveFactor;
			float er = ef.size() > 0 ? static_cast<float>(ef[0]) : 0.0f;
			float eg = ef.size() > 1 ? static_cast<float>(ef[1]) : 0.0f;
			float eb = ef.size() > 2 ? static_cast<float>(ef[2]) : 0.0f;
			material->SetUniform(Material::EMISSIVE_COLOR, Uniform3f::Create({ er, eg, eb }));

			// Engine-Lambert defaults that the existing pipeline shaders expect.
			// Cross-referenced against examples/bin/Resource/Scene/scene.json L26-111.
			material->SetUniform(Material::SHININESS,       Uniform1f::Create({ 32.0f }));
			material->SetUniform(Material::AMBIENT_COLOR,   Uniform3f::Create({ 0.0f, 0.0f, 0.0f }));
			material->SetUniform(Material::AMBIENT_FACTOR,  Uniform1f::Create({ 1.0f }));
			material->SetUniform(Material::DIFFUSE_FACTOR,  Uniform1f::Create({ 1.0f }));
			material->SetUniform(Material::SPECULAR_FACTOR, Uniform1f::Create({ 0.25f }));
			material->SetUniform(Material::EMISSIVE_FACTOR, Uniform1f::Create({ 0.0f }));
			material->SetUniform(Material::SPECULAR_COLOR,  Uniform3f::Create({ 0.2f, 0.2f, 0.2f }));

			// Build a discarded-fields signature so we don't log the same
			// warning twice when many materials share the same shape.
			std::string sig;
			if (gm.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) sig += "metallicRoughnessTexture,";
			if (gm.normalTexture.index >= 0)                                 sig += "normalTexture,";
			if (gm.occlusionTexture.index >= 0)                              sig += "occlusionTexture,";
			if (gm.emissiveTexture.index >= 0)                               sig += "emissiveTexture,";
			if (gm.pbrMetallicRoughness.metallicFactor != 1.0)               sig += "metallicFactor,";
			if (gm.pbrMetallicRoughness.roughnessFactor != 1.0)              sig += "roughnessFactor,";
			if (!sig.empty() && already_warned.insert(sig).second)
			{
				FURYW << "gltf-importer: material '" << name
					<< "' — discarded PBR fields: " << sig
					<< " (engine pipeline is Lambert in v1; HDR/PBR pipeline deferred)";
			}

			return material;
		}

		// Extract bytes for embedded images (image.uri empty, bufferView set)
		// into files alongside the converter's output. Returns true on success,
		// false on any IO error. For .gltf inputs with external image URIs this
		// is a no-op.
		bool ExtractEmbeddedImages(
			const tinygltf::Model &model,
			const std::string &output_basename_no_ext)
		{
			if (output_basename_no_ext.empty()) return true;  // no extraction target
			for (size_t i = 0; i < model.images.size(); ++i)
			{
				const auto &image = model.images[i];
				if (!image.uri.empty()) continue;  // external — nothing to write
				if (image.bufferView < 0) continue;
				if (image.image.empty())
				{
					FURYW << "gltf-importer: image[" << i
						<< "] is embedded but tinygltf produced no decoded bytes; skipping";
					continue;
				}
				const std::string uri = DeriveImageUri(model, static_cast<int>(i),
					output_basename_no_ext);
				// tinygltf decoded the image to raw RGBA pixels in image.image.
				// We can't re-encode without an image-write library; writing the
				// raw .bin would mismatch the engine's stb_image-driven load path.
				// Workaround: dump the *encoded* bytes from the buffer view if
				// they're available there.
				if (image.bufferView < static_cast<int>(model.bufferViews.size()))
				{
					const auto &bv = model.bufferViews[image.bufferView];
					if (bv.buffer >= 0 && bv.buffer < static_cast<int>(model.buffers.size()))
					{
						const auto &buf = model.buffers[bv.buffer];
						if (bv.byteOffset + bv.byteLength <= buf.data.size())
						{
							std::ofstream out(uri, std::ios::binary);
							if (!out)
							{
								FURYE << "gltf-importer: failed to open '" << uri
									<< "' for embedded image extraction";
								return false;
							}
							out.write(reinterpret_cast<const char*>(buf.data.data() + bv.byteOffset),
								bv.byteLength);
							out.close();
							FURYD << "gltf-importer: extracted image[" << i
								<< "] (" << bv.byteLength << " bytes) -> " << uri;
							continue;
						}
					}
				}
				FURYW << "gltf-importer: image[" << i
					<< "] had no usable bufferView bytes; texture path '" << uri
					<< "' will be a dangling reference";
			}
			return true;
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

		// Extract embedded image bytes (no-op for .gltf with external URIs).
		// Done before material translation so a failure aborts the import
		// before any Scene is created.
		if (!ExtractEmbeddedImages(model, opts.output_basename_no_ext))
			return nullptr;

		auto tree = OcTree::Create(Vector4(-1000), Vector4(1000), 2);
		auto scene = Scene::Create(scene_name, working_dir, tree);
		auto entities = scene->GetEntityManager();

		// Materials. We translate them all up-front so meshes (next groups)
		// can reference engine Material::Ptrs by glTF material index.
		std::set<std::string> warned_signatures;
		std::vector<std::shared_ptr<Material>> materials;
		materials.reserve(model.materials.size());
		for (size_t mi = 0; mi < model.materials.size(); ++mi)
		{
			auto mat = TranslateMaterial(model, static_cast<int>(mi),
				opts.output_basename_no_ext, warned_signatures);
			entities->Add(mat);
			materials.push_back(mat);
		}

		// Mesh / skin / node-tree / animation translation lands across
		// implementation groups 6-9; this commit only covers groups 2-5.
		FURYI << "gltf-importer: loaded '" << input_path << "' — translated "
			<< materials.size() << " material(s); mesh/skin/node/anim passes "
			<< "still stubbed in this build";

		return scene;
	}
}
