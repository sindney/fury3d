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
#include "Fury/Joint.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
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

			return material;
		}

		// Accessor reading -- copy raw bytes from a tinygltf accessor into a
		// contiguous typed vector. Returns true on success, false if the
		// accessor's component type doesn't match T (we only handle the
		// straightforward cases used by glTF 2.0: float for positions/normals/
		// tangents/UVs/weights/inverse-bind-matrices, uint8/16/32 for indices
		// and joint indices).

		// Read a vec3 (or vec2) of floats into a flat float vector.
		// num_components is the per-vertex element count (2 for UVs, 3 for
		// positions/normals/tangents). For TANGENT (vec4 in glTF) we drop the
		// handedness w component by passing 3 here and stride-skipping the 4th.
		bool ReadFloatAccessor(
			const tinygltf::Model &model,
			int accessor_index,
			int num_components_to_copy,
			std::vector<float> &out)
		{
			if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
			const auto &accessor = model.accessors[accessor_index];
			if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;
			const int actual_components = tinygltf::GetNumComponentsInType(accessor.type);
			if (actual_components <= 0) return false;
			if (accessor.bufferView < 0) return false;
			const auto &bv = model.bufferViews[accessor.bufferView];
			const auto &buf = model.buffers[bv.buffer];
			const uint8_t *base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
			const size_t element_stride = sizeof(float) * actual_components;
			for (size_t i = 0; i < accessor.count; ++i)
			{
				const float *fp = reinterpret_cast<const float*>(base + i * element_stride);
				const int copy = std::min(num_components_to_copy, actual_components);
				for (int c = 0; c < copy; ++c) out.push_back(fp[c]);
			}
			return true;
		}

		// Read indices into uint32 (glTF allows UNSIGNED_BYTE / UNSIGNED_SHORT /
		// UNSIGNED_INT). offset_to_add is applied to each value — used when we
		// renumber primitive-local indices into a combined per-mesh vertex
		// buffer.
		bool ReadIndexAccessor(
			const tinygltf::Model &model,
			int accessor_index,
			unsigned int offset_to_add,
			std::vector<unsigned int> &out)
		{
			if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
			const auto &accessor = model.accessors[accessor_index];
			if (accessor.bufferView < 0) return false;
			const auto &bv = model.bufferViews[accessor.bufferView];
			const auto &buf = model.buffers[bv.buffer];
			const uint8_t *base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
			switch (accessor.componentType)
			{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					for (size_t i = 0; i < accessor.count; ++i)
						out.push_back(offset_to_add + static_cast<unsigned int>(base[i]));
					return true;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					for (size_t i = 0; i < accessor.count; ++i)
						out.push_back(offset_to_add + static_cast<unsigned int>(reinterpret_cast<const uint16_t*>(base)[i]));
					return true;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					for (size_t i = 0; i < accessor.count; ++i)
						out.push_back(offset_to_add + reinterpret_cast<const uint32_t*>(base)[i]);
					return true;
				default:
					return false;
			}
		}

		// Read JOINTS_0 (vec4 of uint8/uint16) -> 4 uint32 per vertex.
		bool ReadJointsAccessor(
			const tinygltf::Model &model,
			int accessor_index,
			std::vector<unsigned int> &out)
		{
			if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
			const auto &accessor = model.accessors[accessor_index];
			if (accessor.type != TINYGLTF_TYPE_VEC4) return false;
			if (accessor.bufferView < 0) return false;
			const auto &bv = model.bufferViews[accessor.bufferView];
			const auto &buf = model.buffers[bv.buffer];
			const uint8_t *base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
			switch (accessor.componentType)
			{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					for (size_t i = 0; i < accessor.count; ++i)
					{
						const uint8_t *bp = base + i * 4;
						for (int c = 0; c < 4; ++c) out.push_back(bp[c]);
					}
					return true;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					for (size_t i = 0; i < accessor.count; ++i)
					{
						const uint16_t *bp = reinterpret_cast<const uint16_t*>(base + i * sizeof(uint16_t) * 4);
						for (int c = 0; c < 4; ++c) out.push_back(bp[c]);
					}
					return true;
				default:
					return false;
			}
		}

		// Read a vec4 of floats and emit the first 3 components per vertex
		// (the engine stores 3 explicit weights; the 4th is implicit).
		bool ReadWeights3Accessor(
			const tinygltf::Model &model,
			int accessor_index,
			std::vector<float> &out)
		{
			if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
			const auto &accessor = model.accessors[accessor_index];
			if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;
			if (accessor.type != TINYGLTF_TYPE_VEC4) return false;
			if (accessor.bufferView < 0) return false;
			const auto &bv = model.bufferViews[accessor.bufferView];
			const auto &buf = model.buffers[bv.buffer];
			const uint8_t *base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
			for (size_t i = 0; i < accessor.count; ++i)
			{
				const float *fp = reinterpret_cast<const float*>(base + i * sizeof(float) * 4);
				out.push_back(fp[0]);
				out.push_back(fp[1]);
				out.push_back(fp[2]);
				// fp[3] is recoverable as 1 - sum and is intentionally dropped
			}
			return true;
		}

		// Decompose a glTF node matrix or compose its TRS into a Matrix4.
		// Falls through to identity if neither matrix nor TRS is present.
		Matrix4 NodeLocalMatrix(const tinygltf::Node &node)
		{
			if (node.matrix.size() == 16)
			{
				float raw[16];
				for (int i = 0; i < 16; ++i) raw[i] = static_cast<float>(node.matrix[i]);
				return Matrix4(raw);
			}
			// glTF: T * R * S (column-major). We construct via the engine's
			// Matrix4 Append* family.
			Matrix4 m;
			m.Identity();
			if (node.scale.size() == 3)
			{
				m.AppendScale(Vector4(static_cast<float>(node.scale[0]),
					static_cast<float>(node.scale[1]),
					static_cast<float>(node.scale[2]), 1.0f));
			}
			if (node.rotation.size() == 4)
			{
				Quaternion q(
					static_cast<float>(node.rotation[0]),
					static_cast<float>(node.rotation[1]),
					static_cast<float>(node.rotation[2]),
					static_cast<float>(node.rotation[3]));
				m.AppendRotation(q);
			}
			if (node.translation.size() == 3)
			{
				m.AppendTranslation(Vector4(static_cast<float>(node.translation[0]),
					static_cast<float>(node.translation[1]),
					static_cast<float>(node.translation[2]), 1.0f));
			}
			return m;
		}

		// Translate one glTF mesh -> one engine Mesh. Each glTF primitive
		// becomes one engine SubMesh; their vertex streams are concatenated
		// into the engine Mesh's flat ArrayBuffers with index renumbering.
		//
		// The submesh_materials out-parameter is parallel to engine
		// Mesh::m_SubMeshes — each entry is the glTF material index for that
		// submesh, or -1 if none. The node-walking pass uses this to populate
		// the corresponding MeshRender's material list.
		std::shared_ptr<Mesh> TranslateMesh(
			const tinygltf::Model &model,
			int mesh_index,
			std::vector<int> &submesh_materials)
		{
			const auto &gm = model.meshes[mesh_index];
			const std::string name = gm.name.empty()
				? "Mesh_" + std::to_string(mesh_index)
				: gm.name;
			auto mesh = Mesh::Create(name);

			// Track scene-space AABB; use POSITION.minValues / maxValues when
			// present (glTF 2.0 mandates them on POSITION accessors).
			float aabb_min[3] = { std::numeric_limits<float>::max(),
				std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
			float aabb_max[3] = { -std::numeric_limits<float>::max(),
				-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
			bool aabb_valid = false;

			unsigned int vertex_base = 0;
			for (const auto &prim : gm.primitives)
			{
				const size_t pos_before = mesh->Positions.Data.size() / 3;

				auto pos_it = prim.attributes.find("POSITION");
				if (pos_it == prim.attributes.end())
				{
					FURYE << "gltf-importer: mesh '" << name
						<< "' primitive missing POSITION; skipping";
					continue;
				}

				if (!ReadFloatAccessor(model, pos_it->second, 3, mesh->Positions.Data))
				{
					FURYE << "gltf-importer: failed to read POSITION on mesh '" << name << "'";
					return nullptr;
				}

				const size_t verts_added = mesh->Positions.Data.size() / 3 - pos_before;

				// Accumulate AABB from accessor min/max when present.
				const auto &pos_acc = model.accessors[pos_it->second];
				if (pos_acc.minValues.size() >= 3 && pos_acc.maxValues.size() >= 3)
				{
					for (int c = 0; c < 3; ++c)
					{
						aabb_min[c] = std::min(aabb_min[c], static_cast<float>(pos_acc.minValues[c]));
						aabb_max[c] = std::max(aabb_max[c], static_cast<float>(pos_acc.maxValues[c]));
					}
					aabb_valid = true;
				}

				auto nrm_it = prim.attributes.find("NORMAL");
				if (nrm_it != prim.attributes.end())
					ReadFloatAccessor(model, nrm_it->second, 3, mesh->Normals.Data);

				auto tan_it = prim.attributes.find("TANGENT");
				if (tan_it != prim.attributes.end())
					ReadFloatAccessor(model, tan_it->second, 3, mesh->Tangents.Data);  // drop w (handedness)

				auto uv_it = prim.attributes.find("TEXCOORD_0");
				if (uv_it != prim.attributes.end())
					ReadFloatAccessor(model, uv_it->second, 2, mesh->UVs.Data);

				auto joints_it = prim.attributes.find("JOINTS_0");
				auto weights_it = prim.attributes.find("WEIGHTS_0");
				if (joints_it != prim.attributes.end() && weights_it != prim.attributes.end())
				{
					if (!ReadJointsAccessor(model, joints_it->second, mesh->IDs.Data))
						FURYW << "gltf-importer: mesh '" << name << "' JOINTS_0 in unsupported type";
					if (!ReadWeights3Accessor(model, weights_it->second, mesh->Weights.Data))
						FURYW << "gltf-importer: mesh '" << name << "' WEIGHTS_0 in unsupported type";
				}

				// SubMesh: indices for just this primitive, into the
				// per-mesh combined vertex buffer.
				auto sub = SubMesh::Create();
				if (prim.indices >= 0)
				{
					ReadIndexAccessor(model, prim.indices, vertex_base, sub->Indices.Data);
				}
				else
				{
					// Non-indexed primitive — synthesize a linear index range.
					for (unsigned int v = 0; v < verts_added; ++v)
						sub->Indices.Data.push_back(vertex_base + v);
				}
				mesh->AddSubMesh(sub);
				submesh_materials.push_back(prim.material);

				// Also accumulate into the combined Indices buffer so that the
				// engine's whole-mesh index queries (CalculateAABB, etc.) see
				// the full topology. SubMesh::Indices are what the renderer
				// actually draws against; Mesh::Indices is a back-up.
				for (auto idx : sub->Indices.Data) mesh->Indices.Data.push_back(idx);

				vertex_base += static_cast<unsigned int>(verts_added);
			}

			if (aabb_valid)
				mesh->CalculateAABB(Vector4(aabb_min[0], aabb_min[1], aabb_min[2], 1.0f),
					Vector4(aabb_max[0], aabb_max[1], aabb_max[2], 1.0f));
			else
				mesh->CalculateAABB();

			mesh->SetCastShadows(true);
			return mesh;
		}

		// Build the engine Joint tree for one skin and attach it to the given
		// engine Mesh. Returns true on success.
		//
		// Engine model: joints are owned by a Mesh; a Joint's m_Mesh weak_ptr
		// points back. We use the glTF skin's `joints` array (indices into
		// model.nodes) and `inverseBindMatrices` (one mat4 per joint).
		bool TranslateSkin(
			const tinygltf::Model &model,
			int skin_index,
			const std::shared_ptr<Mesh> &mesh)
		{
			const auto &skin = model.skins[skin_index];

			// Read inverse-bind matrices (vec16 floats per joint).
			std::vector<float> ibms;
			if (skin.inverseBindMatrices >= 0)
				ReadFloatAccessor(model, skin.inverseBindMatrices, 16, ibms);

			std::vector<Joint::Ptr> joints;
			joints.reserve(skin.joints.size());

			// First pass: create joints, set local + offset matrices.
			for (size_t j = 0; j < skin.joints.size(); ++j)
			{
				int node_index = skin.joints[j];
				if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size())) continue;
				const auto &node = model.nodes[node_index];
				const std::string jname = node.name.empty()
					? "Joint_" + std::to_string(node_index)
					: node.name;
				auto joint = Joint::Create(jname, mesh);
				joint->SetLocalMatrix(NodeLocalMatrix(node));
				if (ibms.size() >= (j + 1) * 16)
				{
					float raw[16];
					for (int i = 0; i < 16; ++i) raw[i] = ibms[j * 16 + i];
					joint->SetOffsetMatrix(Matrix4(raw));
				}
				joints.push_back(joint);
			}

			// Second pass: parent/child links. For each glTF joint-node we
			// look at its children; any child that's also in skin.joints
			// becomes a child of the corresponding engine Joint.
			//
			// Engine joints use first-child + sibling linked lists; we
			// insert each child at the head of its parent's child list.
			std::unordered_map<int, size_t> node_to_jointidx;
			for (size_t j = 0; j < skin.joints.size(); ++j)
				node_to_jointidx[skin.joints[j]] = j;

			for (size_t j = 0; j < skin.joints.size(); ++j)
			{
				int node_index = skin.joints[j];
				const auto &node = model.nodes[node_index];
				for (int child_node : node.children)
				{
					auto child_it = node_to_jointidx.find(child_node);
					if (child_it == node_to_jointidx.end()) continue;  // non-joint child
					auto parent = joints[j];
					auto child = joints[child_it->second];
					child->SetParent(parent);
					auto existing = parent->GetFirstChild();
					child->SetSibling(existing);
					parent->SetFirstChild(child);
				}
			}

			// Determine root: prefer skin.skeleton (a node index) if it's in
			// the joint set; else use the first joint.
			Joint::Ptr root;
			if (skin.skeleton >= 0)
			{
				auto it = node_to_jointidx.find(skin.skeleton);
				if (it != node_to_jointidx.end()) root = joints[it->second];
			}
			if (!root && !joints.empty()) root = joints[0];

			// Attach to mesh: the m_Joints / m_JointMap / m_RootJoint fields
			// are protected — we use the Mesh's friend-class trick? No,
			// they're not accessible from a helper. We'll need to add a
			// public setter or befriend GltfImporter. The cleanest path is
			// a small public setter pair on Mesh.
			//
			// (See companion Mesh.h change.)
			mesh->SetJointTree(joints, root);
			return true;
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

		// Meshes. Each glTF mesh becomes one engine Mesh; each glTF primitive
		// becomes one SubMesh. The submesh_to_gltf_material map below carries
		// each submesh's glTF material index forward so the node-walking pass
		// can wire MeshRender's material list.
		std::vector<std::shared_ptr<Mesh>> meshes;
		std::vector<std::vector<int>> submesh_to_gltf_material;  // [mesh_i][sub_j] = gltf_material_index or -1
		meshes.reserve(model.meshes.size());
		submesh_to_gltf_material.reserve(model.meshes.size());
		for (size_t mi = 0; mi < model.meshes.size(); ++mi)
		{
			std::vector<int> per_submesh_mat;
			auto m = TranslateMesh(model, static_cast<int>(mi), per_submesh_mat);
			if (!m) return nullptr;  // unrecoverable translation error
			entities->Add(m);
			meshes.push_back(m);
			submesh_to_gltf_material.push_back(std::move(per_submesh_mat));
		}

		// Skins. A glTF node references both a mesh and a skin; the engine
		// stores joints on the Mesh itself, so we attach the skin to the mesh
		// the first referencing node names. v1 rejects multi-skin-per-mesh.
		std::vector<int> mesh_to_skin(meshes.size(), -1);  // -1 = no skin
		for (const auto &node : model.nodes)
		{
			if (node.mesh < 0 || node.skin < 0) continue;
			if (node.mesh >= static_cast<int>(meshes.size())) continue;
			int existing = mesh_to_skin[node.mesh];
			if (existing >= 0 && existing != node.skin)
			{
				FURYE << "gltf-importer: mesh '" << meshes[node.mesh]->GetName()
					<< "' is referenced by nodes using different skins ("
					<< existing << " and " << node.skin
					<< ") — v1 supports one skin per mesh, rejecting";
				return nullptr;
			}
			mesh_to_skin[node.mesh] = node.skin;
		}
		for (size_t mi = 0; mi < meshes.size(); ++mi)
		{
			if (mesh_to_skin[mi] < 0) continue;
			if (!TranslateSkin(model, mesh_to_skin[mi], meshes[mi])) return nullptr;
		}

		// Node-tree walk and animation translation land in groups 8-9 below.
		// We surface what we have so far so the smoke path produces useful logs.
		FURYI << "gltf-importer: '" << input_path << "' translated "
			<< materials.size() << " material(s), "
			<< meshes.size() << " mesh(es); node tree + animations pending";

		// Silence unused-variable warnings until groups 8-9 wire them in.
		(void)submesh_to_gltf_material;

		return scene;
	}
}
