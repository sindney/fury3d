// GltfImporter.cpp -- tinygltf::Model -> engine Scene translator.
//
// This file grows across implementation groups 2-9 (skeleton, rejection,
// material, texture, mesh, skin, scene-node, animation). The first cut just
// handles top-level Load and rejection of unsupported features -- enough for
// `fury convert gltf` to refuse bad inputs cleanly while the asset-translation
// passes are written.

#include "Fury/GltfImporter.h"

// Vector4/Matrix4 must be complete before Joint.h since Joint declares
// std::pair<Vector4, Vector4> members.
#include "Fury/AnimationClip.h"
#include "Fury/Component.h"
#include "Fury/EntityManager.h"
#include "Fury/FileUtil.h"
#include "Fury/Joint.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/MathUtil.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/OcTree.h"
#include "Fury/Quaternion.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Texture.h"
#include "Fury/Transform.h"
#include "Fury/Uniform.h"
#include "Fury/Vector4.h"

// tinygltf pulls in its own JSON header; we suppress its stb_image to avoid
// ODR collision with the engine's STB. These defines MUST be in effect at
// every tiny_gltf.h include site -- they affect inline default initializers
// for the TinyGLTF class. Without them this TU references undefined
// tinygltf::LoadImageData / WriteImageData at link time.
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <tiny_gltf.h>
#include <unordered_map>

namespace fury {
namespace {
std::string ToLower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool HasSuffix(const std::string& path, const std::string& suffix) {
	if (path.size() < suffix.size()) return false;
	return ToLower(path.substr(path.size() - suffix.size())) == suffix;
}

// Returns true if the model has any feature we don't support in v1.
// Logs the first rejection reason and short-circuits.
bool HasUnsupportedFeatures(const tinygltf::Model& model, const std::string& input_path) {
	if (!model.extensionsRequired.empty()) {
		std::string names;
		for (const auto& n : model.extensionsRequired) names += " " + n;
		FURYE << "gltf-importer: rejected -- extensionsRequired (" << names
			  << ") not supported in v1 (input: " << input_path
			  << "; see docs/CLI.md sec Limitations)";
		return true;
	}

	for (size_t bv_i = 0; bv_i < model.bufferViews.size(); ++bv_i) {
		// byteStride != 0 (interleaved vertex buffers, e.g. the Khronos
		// Fox sample) IS supported -- the accessor readers below use
		// bv.byteStride as the element pitch when non-zero. No rejection.
		(void)model.bufferViews[bv_i].byteStride;
	}

	for (size_t a_i = 0; a_i < model.accessors.size(); ++a_i) {
		if (model.accessors[a_i].sparse.isSparse) {
			FURYE << "gltf-importer: rejected -- accessors[" << a_i
				  << "] is sparse, not supported in v1 (input: " << input_path
				  << "; see docs/CLI.md sec Limitations)";
			return true;
		}
	}

	for (size_t m_i = 0; m_i < model.meshes.size(); ++m_i) {
		const auto& mesh = model.meshes[m_i];
		for (size_t p_i = 0; p_i < mesh.primitives.size(); ++p_i) {
			const auto& prim = mesh.primitives[p_i];
			if (!prim.targets.empty()) {
				FURYE << "gltf-importer: rejected -- meshes[" << m_i
					  << "].primitives[" << p_i << "] has " << prim.targets.size()
					  << " morph target(s), not supported in v1 (input: " << input_path
					  << "; see docs/CLI.md sec Limitations)";
				return true;
			}
			if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
				FURYE << "gltf-importer: rejected -- meshes[" << m_i
					  << "].primitives[" << p_i << "].mode=" << prim.mode
					  << " (only TINYGLTF_MODE_TRIANGLES=4 supported in v1; input: "
					  << input_path << ")";
				return true;
			}
		}
	}

	return false;
}

// Derive a hint at the original filename for an embedded glTF
// image. Used by FileUtil::SaveFile when it extracts memory-backed
// textures to sibling files: prefer image.name (FBX2glTF preserves
// the original FBX texture filename here, e.g. "body.jpg"); else
// fall back to "<input_stem>_image<i>.<ext>" with the extension
// sniffed from mimeType. The returned string is purely a hint; it
// does NOT participate in URI resolution at import time.
std::string DeriveOriginalFilename(
	const tinygltf::Model& model,
	int image_index,
	const std::string& input_stem) {
	if (image_index < 0 || image_index >= static_cast<int>(model.images.size()))
		return "";
	const auto& image = model.images[image_index];
	if (!image.name.empty())
		return image.name;
	std::string ext = ".png";
	if (image.mimeType == "image/jpeg")
		ext = ".jpg";
	else if (image.mimeType == "image/bmp")
		ext = ".bmp";
	return input_stem + "_image" + std::to_string(image_index) + ext;
}

// Map a glTF texture index to the engine Texture::Ptr we cached for it.
// External-URI images go through SetFilePathAndSRGB + CreateFromImage
// (existing path; runtime resolves via Scene::Path). Embedded images
// (image.uri empty + non-negative bufferView) go through the new
// CreateFromMemory path: bytes flow straight to the GPU and stay
// attached to the Texture for save-time extraction.
Texture::Ptr CreateEngineTexture(
	const tinygltf::Model& model,
	int texture_index,
	bool srgb,
	const std::string& input_stem,
	const std::string& input_dir) {
	if (texture_index < 0 || texture_index >= static_cast<int>(model.textures.size()))
		return nullptr;
	const auto& gtex = model.textures[texture_index];
	const int image_index = gtex.source;
	if (image_index < 0 || image_index >= static_cast<int>(model.images.size()))
		return nullptr;
	const auto& image = model.images[image_index];

	// Sampler -> filter / wrap mode (applied to the engine Texture
	// before any GPU upload runs).
	const std::string original_filename = DeriveOriginalFilename(model, image_index, input_stem);

	// Texture name: prefer the image name or original-filename hint;
	// the engine looks up textures by name in some paths.
	std::string name = original_filename;
	if (name.empty())
		name = "tex_" + std::to_string(texture_index);
	else {
		auto slash = name.find_last_of("/\\");
		if (slash != std::string::npos) name = name.substr(slash + 1);
	}
	auto tex = Texture::Create(name);

	bool mipmap = true;
	if (gtex.sampler >= 0 && gtex.sampler < static_cast<int>(model.samplers.size())) {
		const auto& sampler = model.samplers[gtex.sampler];
		// glTF wrap: 33071=CLAMP_TO_EDGE, 33648=MIRRORED_REPEAT, 10497=REPEAT
		if (sampler.wrapS == 33071)
			tex->SetWrapMode(WrapMode::CLAMP_TO_EDGE);
		else if (sampler.wrapS == 33648)
			tex->SetWrapMode(WrapMode::MIRRORED_REPEAT);
		else
			tex->SetWrapMode(WrapMode::REPEAT);
		// glTF mag filter: 9728=NEAREST, 9729=LINEAR
		if (sampler.magFilter == 9728)
			tex->SetFilterMode(FilterMode::NEAREST);
		else
			tex->SetFilterMode(FilterMode::LINEAR);
	}

	if (!image.uri.empty()) {
		// External-URI case. Resolve relative URIs against the
		// .gltf/.glb's directory (the glTF spec requires this; tinygltf
		// hands us the raw uri). Absolute paths and data: URIs pass
		// through unchanged.
		std::string path = image.uri;
		if (!path.empty() && path[0] != '/' && path.find("data:") != 0
			&& path.find("://") == std::string::npos
			&& !(path.size() >= 2 && path[1] == ':') // Windows drive
			&& !input_dir.empty()) {
			path = input_dir + path;
		}
		tex->SetFilePathAndSRGB(path, srgb);
		tex->CreateFromImage(path, srgb, mipmap);
		return tex;
	}

	// Embedded case: image.uri empty, bytes live in a bufferView.
	if (image.bufferView < 0 || image.bufferView >= static_cast<int>(model.bufferViews.size())) {
		FURYW << "gltf-importer: image[" << image_index
			  << "] is neither external nor bufferView-backed; texture will be missing";
		return nullptr;
	}
	const auto& bv = model.bufferViews[image.bufferView];
	if (bv.buffer < 0 || bv.buffer >= static_cast<int>(model.buffers.size())) {
		FURYW << "gltf-importer: image[" << image_index
			  << "] bufferView references invalid buffer; texture will be missing";
		return nullptr;
	}
	const auto& buf = model.buffers[bv.buffer];
	if (bv.byteOffset + bv.byteLength > buf.data.size()) {
		FURYW << "gltf-importer: image[" << image_index
			  << "] bufferView slice out of buffer bounds; texture will be missing";
		return nullptr;
	}
	tex->CreateFromMemory(buf.data.data() + bv.byteOffset, bv.byteLength, srgb, mipmap);
	tex->SetOriginalFilename(original_filename);
	return tex;
}

// Translate one glTF material -> one engine Material. Lossy: PBR
// metallic-roughness becomes Lambert. baseColorFactor -> diffuse_color,
// baseColorTexture -> diffuse_texture slot, emissiveFactor ->
// emissive_color, alphaMode -> opaque flag. Other PBR fields are read
// but discarded with a one-shot warning per material.
//
// HDR mode: when hdr is true, PBR fields are NOT discarded -- they are
// mapped onto the engine's PBR material slots (METALLIC_FACTOR,
// ROUGHNESS_FACTOR, METALLIC_ROUGHNESS_TEXTURE, OCCLUSION_TEXTURE,
// normal slot reused for normalTexture). The Lambert defaults are
// still populated so a legacy LDR pass that reads them doesn't crash,
// but the importer doesn't emit the discard warning because no PBR
// field was thrown away.
//
// already_warned: signatures of discarded-field sets we've already
// reported, so identical materials don't spam the log.
Material::Ptr TranslateMaterial(
	const tinygltf::Model& model,
	int material_index,
	const std::string& input_stem,
	const std::string& input_dir,
	bool hdr,
	std::set<std::string>& already_warned) {
	const auto& gm = model.materials[material_index];
	const std::string name = gm.name.empty()
								 ? "Material_" + std::to_string(material_index)
								 : gm.name;
	auto material = Material::Create(name);

	// alphaMode/alphaCutoff -> material alpha mode (MASK stays
	// opaque-bucketed and alpha-tests in the gbuffer shader)
	material->SetAlphaMode(EnumUtil::AlphaModeFromString(gm.alphaMode));
	material->SetAlphaCutoff(static_cast<float>(gm.alphaCutoff));

	// baseColorFactor -> diffuse_color (rgb) + transparency (1 - a)
	const auto& bcf = gm.pbrMetallicRoughness.baseColorFactor;
	float r = bcf.size() > 0 ? static_cast<float>(bcf[0]) : 1.0f;
	float g = bcf.size() > 1 ? static_cast<float>(bcf[1]) : 1.0f;
	float b = bcf.size() > 2 ? static_cast<float>(bcf[2]) : 1.0f;
	float a = bcf.size() > 3 ? static_cast<float>(bcf[3]) : 1.0f;
	material->SetUniform(Material::DIFFUSE_COLOR, Uniform3f::Create({r, g, b}));
	material->SetUniform(Material::TRANSPARENCY, Uniform1f::Create({1.0f - a}));

	// KHR_materials_transmission fallback: no refraction support --
	// approximate as BLEND glass with alpha = 1 - transmissionFactor.
	// Explicit alphaMode=BLEND wins over the extension.
	if (gm.alphaMode != "BLEND") {
		auto extIt = gm.extensions.find("KHR_materials_transmission");
		if (extIt != gm.extensions.end() && extIt->second.IsObject()) {
			double t = 1.0;
			auto f = extIt->second.Get("transmissionFactor");
			if (f.IsNumber()) t = f.GetNumberAsDouble();
			if (t > 0.0) {
				material->SetAlphaMode(AlphaMode::BLEND);
				material->SetUniform(Material::TRANSPARENCY,
					Uniform1f::Create({static_cast<float>(t)}));
			}
		}
	}

	// baseColorTexture -> diffuse_texture slot
	if (gm.pbrMetallicRoughness.baseColorTexture.index >= 0) {
		auto tex = CreateEngineTexture(model,
									   gm.pbrMetallicRoughness.baseColorTexture.index,
									   /*srgb=*/true, // base color is colorspace data
									   input_stem,
									   input_dir);
		if (tex) material->SetTexture(Material::DIFFUSE_TEXTURE, tex);
	}

	// emissiveFactor -> emissive_color
	const auto& ef = gm.emissiveFactor;
	float er = ef.size() > 0 ? static_cast<float>(ef[0]) : 0.0f;
	float eg = ef.size() > 1 ? static_cast<float>(ef[1]) : 0.0f;
	float eb = ef.size() > 2 ? static_cast<float>(ef[2]) : 0.0f;
	material->SetUniform(Material::EMISSIVE_COLOR, Uniform3f::Create({er, eg, eb}));

	// Engine-Lambert defaults that the existing pipeline shaders expect.
	// Cross-referenced against examples/bin/Resource/Scene/scene.json L26-111.
	material->SetUniform(Material::SHININESS, Uniform1f::Create({32.0f}));
	material->SetUniform(Material::AMBIENT_COLOR, Uniform3f::Create({0.0f, 0.0f, 0.0f}));
	material->SetUniform(Material::AMBIENT_FACTOR, Uniform1f::Create({1.0f}));
	material->SetUniform(Material::DIFFUSE_FACTOR, Uniform1f::Create({1.0f}));
	material->SetUniform(Material::SPECULAR_FACTOR, Uniform1f::Create({0.25f}));
	material->SetUniform(Material::EMISSIVE_FACTOR, Uniform1f::Create({0.0f}));
	material->SetUniform(Material::SPECULAR_COLOR, Uniform3f::Create({0.2f, 0.2f, 0.2f}));

	if (hdr) {
		// HDR target: map the full PBR field set. No field is
		// discarded, so no "discarded PBR fields" warning is logged.
		// Factors default to glTF defaults (metallic=1, roughness=1)
		// when the source omits them, which matches the glTF 2.0
		// spec's "PBR metallic-roughness" model.
		material->SetUniform(Material::METALLIC_FACTOR,
			Uniform1f::Create({static_cast<float>(gm.pbrMetallicRoughness.metallicFactor)}));
		material->SetUniform(Material::ROUGHNESS_FACTOR,
			Uniform1f::Create({static_cast<float>(gm.pbrMetallicRoughness.roughnessFactor)}));

		if (gm.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
			auto tex = CreateEngineTexture(model,
				gm.pbrMetallicRoughness.metallicRoughnessTexture.index,
				/*srgb=*/false, // metallic-roughness is data
				input_stem, input_dir);
			if (tex) material->SetTexture(Material::METALLIC_ROUGHNESS_TEXTURE, tex);
		}

		// Reuse the existing NORMAL_TEXTURE slot for the PBR
		// normal map (the engine had a single normal slot; PBR
		// inherits the same path).
		if (gm.normalTexture.index >= 0) {
			auto tex = CreateEngineTexture(model,
				gm.normalTexture.index,
				/*srgb=*/false, // normals are data
				input_stem, input_dir);
			if (tex) material->SetTexture(Material::NORMAL_TEXTURE, tex);
		}

		if (gm.occlusionTexture.index >= 0) {
			auto tex = CreateEngineTexture(model,
				gm.occlusionTexture.index,
				/*srgb=*/false, // occlusion is data
				input_stem, input_dir);
			if (tex) material->SetTexture(Material::OCCLUSION_TEXTURE, tex);
		}
	} else {
		// Build a discarded-fields signature so we don't log the same
		// warning twice when many materials share the same shape.
		std::string sig;
		if (gm.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) sig += "metallicRoughnessTexture,";
		if (gm.normalTexture.index >= 0) sig += "normalTexture,";
		if (gm.occlusionTexture.index >= 0) sig += "occlusionTexture,";
		if (gm.emissiveTexture.index >= 0) sig += "emissiveTexture,";
		if (gm.pbrMetallicRoughness.metallicFactor != 1.0) sig += "metallicFactor,";
		if (gm.pbrMetallicRoughness.roughnessFactor != 1.0) sig += "roughnessFactor,";
		if (!sig.empty() && already_warned.insert(sig).second) {
			FURYW << "gltf-importer: material '" << name
				  << "' -- discarded PBR fields: " << sig
				  << " (engine pipeline is Lambert in v1; HDR/PBR pipeline deferred)";
		}
	}

	// Material::SetTexture recomputes m_TextureFlags. If we never
	// added a texture (no baseColorTexture), flags are still 0 from
	// the default constructor -- and Pass::GetShader treats `flags == 0`
	// as "first shader of this type", which picks `gbuffer_shader`
	// (the with-texture variant) over `gbuffer_notexture_shader`.
	// Result: the gbuffer fragment shader samples an unbound
	// diffuse_texture, the diffuse buffer ends up black, and the
	// final Lambert combine produces a black scene.
	//
	// Force a flag recompute by calling SetTexture(diffuse, nullptr)
	// when no diffuse texture is registered. With a real diffuse
	// texture present, an unconditional SetTexture(name, nullptr)
	// would erase it -- so check first.
	if (!material->GetTexture(Material::DIFFUSE_TEXTURE))
		material->SetTexture(Material::DIFFUSE_TEXTURE, nullptr);

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
	const tinygltf::Model& model,
	int accessor_index,
	int num_components_to_copy,
	std::vector<float>& out) {
	if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
	const auto& accessor = model.accessors[accessor_index];
	if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;
	const int actual_components = tinygltf::GetNumComponentsInType(accessor.type);
	if (actual_components <= 0) return false;
	if (accessor.bufferView < 0) return false;
	const auto& bv = model.bufferViews[accessor.bufferView];
	const auto& buf = model.buffers[bv.buffer];
	const uint8_t* base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
	const size_t element_stride = sizeof(float) * actual_components;
	const size_t stride = (bv.byteStride != 0) ? static_cast<size_t>(bv.byteStride) : element_stride;
	for (size_t i = 0; i < accessor.count; ++i) {
		const float* fp = reinterpret_cast<const float*>(base + i * stride);
		const int copy = std::min(num_components_to_copy, actual_components);
		for (int c = 0; c < copy; ++c) out.push_back(fp[c]);
	}
	return true;
}

// Read COLOR_0 into a flat vec4-float vector. glTF allows VEC3/VEC4 in
// float, normalized UNSIGNED_BYTE, or normalized UNSIGNED_SHORT; vec3
// gets alpha=1. (Fury stores vertex colors as float4 -- Kraut trees pack
// wind weights here.)
bool ReadColorAccessor(
	const tinygltf::Model& model,
	int accessor_index,
	std::vector<float>& out) {
	if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
	const auto& accessor = model.accessors[accessor_index];
	const int actual_components = tinygltf::GetNumComponentsInType(accessor.type);
	if (actual_components < 3 || actual_components > 4) return false;
	if (accessor.bufferView < 0) return false;
	const auto& bv = model.bufferViews[accessor.bufferView];
	const auto& buf = model.buffers[bv.buffer];
	const uint8_t* base = buf.data.data() + bv.byteOffset + accessor.byteOffset;

	size_t comp_size = 0;
	switch (accessor.componentType) {
	case TINYGLTF_COMPONENT_TYPE_FLOAT: comp_size = 4; break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: comp_size = 1; break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: comp_size = 2; break;
	default: return false;
	}
	const size_t element_stride = comp_size * actual_components;
	const size_t stride = (bv.byteStride != 0) ? static_cast<size_t>(bv.byteStride) : element_stride;

	for (size_t i = 0; i < accessor.count; ++i) {
		const uint8_t* ep = base + i * stride;
		for (int c = 0; c < 4; ++c) {
			float v = 1.0f; // alpha default when vec3
			if (c < actual_components) {
				switch (accessor.componentType) {
				case TINYGLTF_COMPONENT_TYPE_FLOAT:
					v = reinterpret_cast<const float*>(ep)[c]; break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					v = ep[c] / 255.0f; break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					v = reinterpret_cast<const uint16_t*>(ep)[c] / 65535.0f; break;
				}
			}
			out.push_back(v);
		}
	}
	return true;
}

// Read indices into uint32 (glTF allows UNSIGNED_BYTE / UNSIGNED_SHORT /
// UNSIGNED_INT). offset_to_add is applied to each value -- used when we
// renumber primitive-local indices into a combined per-mesh vertex
// buffer.
bool ReadIndexAccessor(
	const tinygltf::Model& model,
	int accessor_index,
	unsigned int offset_to_add,
	std::vector<unsigned int>& out) {
	if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
	const auto& accessor = model.accessors[accessor_index];
	if (accessor.bufferView < 0) return false;
	const auto& bv = model.bufferViews[accessor.bufferView];
	const auto& buf = model.buffers[bv.buffer];
	const uint8_t* base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
	const size_t stride = (bv.byteStride != 0) ? static_cast<size_t>(bv.byteStride) : 0;
	switch (accessor.componentType) {
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		for (size_t i = 0; i < accessor.count; ++i)
			out.push_back(offset_to_add + static_cast<unsigned int>(base[stride ? i * stride : i]));
		return true;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		for (size_t i = 0; i < accessor.count; ++i)
			out.push_back(offset_to_add + static_cast<unsigned int>(reinterpret_cast<const uint16_t*>(base + (stride ? i * stride : i * sizeof(uint16_t)))[0]));
		return true;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		for (size_t i = 0; i < accessor.count; ++i)
			out.push_back(offset_to_add + reinterpret_cast<const uint32_t*>(base + (stride ? i * stride : i * sizeof(uint32_t)))[0]);
		return true;
	default:
		return false;
	}
}

// Read JOINTS_0 (vec4 of uint8/uint16) -> 4 uint32 per vertex.
bool ReadJointsAccessor(
	const tinygltf::Model& model,
	int accessor_index,
	std::vector<unsigned int>& out) {
	if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
	const auto& accessor = model.accessors[accessor_index];
	if (accessor.type != TINYGLTF_TYPE_VEC4) return false;
	if (accessor.bufferView < 0) return false;
	const auto& bv = model.bufferViews[accessor.bufferView];
	const auto& buf = model.buffers[bv.buffer];
	const uint8_t* base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
	const size_t tight = sizeof(uint16_t) * 4;
	const size_t stride = (bv.byteStride != 0) ? static_cast<size_t>(bv.byteStride) : tight;
	switch (accessor.componentType) {
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		for (size_t i = 0; i < accessor.count; ++i) {
			const uint8_t* bp = base + i * (bv.byteStride != 0 ? bv.byteStride : 4);
			for (int c = 0; c < 4; ++c) out.push_back(bp[c]);
		}
		return true;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		for (size_t i = 0; i < accessor.count; ++i) {
			const uint16_t* bp = reinterpret_cast<const uint16_t*>(base + i * stride);
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
	const tinygltf::Model& model,
	int accessor_index,
	std::vector<float>& out) {
	if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
	const auto& accessor = model.accessors[accessor_index];
	if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;
	if (accessor.type != TINYGLTF_TYPE_VEC4) return false;
	if (accessor.bufferView < 0) return false;
	const auto& bv = model.bufferViews[accessor.bufferView];
	const auto& buf = model.buffers[bv.buffer];
	const uint8_t* base = buf.data.data() + bv.byteOffset + accessor.byteOffset;
	const size_t tight = sizeof(float) * 4;
	const size_t stride = (bv.byteStride != 0) ? static_cast<size_t>(bv.byteStride) : tight;
	for (size_t i = 0; i < accessor.count; ++i) {
		const float* fp = reinterpret_cast<const float*>(base + i * stride);
		out.push_back(fp[0]);
		out.push_back(fp[1]);
		out.push_back(fp[2]);
		// fp[3] is recoverable as 1 - sum and is intentionally dropped
	}
	return true;
}

// Decompose a glTF node matrix or compose its TRS into a Matrix4.
// Falls through to identity if neither matrix nor TRS is present.
Matrix4 NodeLocalMatrix(const tinygltf::Node& node) {
	if (node.matrix.size() == 16) {
		float raw[16];
		for (int i = 0; i < 16; ++i) raw[i] = static_cast<float>(node.matrix[i]);
		return Matrix4(raw);
	}
	// glTF node local = T * R * S (column-major, M*v). The engine's
	// SceneNode::Recompose / Transform compose in the same order
	// (AppendTranslation -> AppendRotation -> AppendScale). Reversed S*R*T
	// was a latent bug that mangles any joint with both non-trivial
	// rotation and translation.
	Matrix4 m;
	m.Identity();
	if (node.translation.size() == 3)
		m.AppendTranslation(Vector4(static_cast<float>(node.translation[0]),
									static_cast<float>(node.translation[1]),
									static_cast<float>(node.translation[2]), 1.0f));
	if (node.rotation.size() == 4) {
		Quaternion q(
			static_cast<float>(node.rotation[0]),
			static_cast<float>(node.rotation[1]),
			static_cast<float>(node.rotation[2]),
			static_cast<float>(node.rotation[3]));
		m.AppendRotation(q);
	}
	if (node.scale.size() == 3) {
		m.AppendScale(Vector4(static_cast<float>(node.scale[0]),
							  static_cast<float>(node.scale[1]),
							  static_cast<float>(node.scale[2]), 1.0f));
	}
	return m;
}

//
// The submesh_materials out-parameter is parallel to engine
// Mesh::m_SubMeshes -- each entry is the glTF material index for that
// submesh, or -1 if none. The node-walking pass uses this to populate
// the corresponding MeshRender's material list.
std::shared_ptr<Mesh> TranslateMesh(
	const tinygltf::Model& model,
	int mesh_index,
	GltfImporter::Options::NormalGen normal_gen,
	bool optimize_mesh,
	std::vector<int>& submesh_materials) {
	const auto& gm = model.meshes[mesh_index];
	const std::string name = gm.name.empty()
								 ? "Mesh_" + std::to_string(mesh_index)
								 : gm.name;
	auto mesh = Mesh::Create(name);

	// Track scene-space AABB; use POSITION.minValues / maxValues when
	// present (glTF 2.0 mandates them on POSITION accessors).
	float aabb_min[3] = {std::numeric_limits<float>::max(),
						 std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
	float aabb_max[3] = {-std::numeric_limits<float>::max(),
						 -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
	bool aabb_valid = false;

	unsigned int vertex_base = 0;
	for (const auto& prim : gm.primitives) {
		const size_t pos_before = mesh->Positions.Data.size() / 3;

		auto pos_it = prim.attributes.find("POSITION");
		if (pos_it == prim.attributes.end()) {
			FURYE << "gltf-importer: mesh '" << name
				  << "' primitive missing POSITION; skipping";
			continue;
		}

		if (!ReadFloatAccessor(model, pos_it->second, 3, mesh->Positions.Data)) {
			FURYE << "gltf-importer: failed to read POSITION on mesh '" << name << "'";
			return nullptr;
		}

		const size_t verts_added = mesh->Positions.Data.size() / 3 - pos_before;

		// Accumulate AABB from accessor min/max when present.
		const auto& pos_acc = model.accessors[pos_it->second];
		if (pos_acc.minValues.size() >= 3 && pos_acc.maxValues.size() >= 3) {
			for (int c = 0; c < 3; ++c) {
				aabb_min[c] = std::min(aabb_min[c], static_cast<float>(pos_acc.minValues[c]));
				aabb_max[c] = std::max(aabb_max[c], static_cast<float>(pos_acc.maxValues[c]));
			}
			aabb_valid = true;
		}

		auto nrm_it = prim.attributes.find("NORMAL");
		const bool has_normals = (nrm_it != prim.attributes.end());
		if (has_normals)
			ReadFloatAccessor(model, nrm_it->second, 3, mesh->Normals.Data);

		auto tan_it = prim.attributes.find("TANGENT");
		if (tan_it != prim.attributes.end())
			ReadFloatAccessor(model, tan_it->second, 3, mesh->Tangents.Data); // drop w (handedness)

		auto uv_it = prim.attributes.find("TEXCOORD_0");
		if (uv_it != prim.attributes.end())
			ReadFloatAccessor(model, uv_it->second, 2, mesh->UVs.Data);

		// COLOR_0 -> Mesh::Colors (vec4). If an earlier primitive had
		// colors and this one doesn't, pad rigid defaults so the channel
		// stays vertex-aligned.
		auto color_it = prim.attributes.find("COLOR_0");
		const bool had_colors = mesh->Colors.Data.size() > 0;
		if (color_it != prim.attributes.end())
			ReadColorAccessor(model, color_it->second, mesh->Colors.Data);
		if (color_it == prim.attributes.end() && had_colors) {
			for (size_t v = 0; v < verts_added; ++v)
				mesh->Colors.Data.insert(mesh->Colors.Data.end(), {0.0f, 0.0f, 0.0f, 1.0f});
		}

		auto joints_it = prim.attributes.find("JOINTS_0");
		auto weights_it = prim.attributes.find("WEIGHTS_0");
		if (joints_it != prim.attributes.end() && weights_it != prim.attributes.end()) {
			if (!ReadJointsAccessor(model, joints_it->second, mesh->IDs.Data))
				FURYW << "gltf-importer: mesh '" << name << "' JOINTS_0 in unsupported type";
			if (!ReadWeights3Accessor(model, weights_it->second, mesh->Weights.Data))
				FURYW << "gltf-importer: mesh '" << name << "' WEIGHTS_0 in unsupported type";
		}

		// SubMesh: indices for just this primitive, into the
		// per-mesh combined vertex buffer.
		auto sub = SubMesh::Create();
		if (prim.indices >= 0) {
			ReadIndexAccessor(model, prim.indices, vertex_base, sub->Indices.Data);
		} else {
			// Non-indexed primitive -- synthesize a linear index range.
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

		// glTF spec: a primitive without NORMAL should get normals
		// generated by the loader. Smooth mode (the default) accumulates
		// area-weighted face normals per POSITION -- welding vertices with
		// bit-identical positions first, so exporter-split meshes (per-face
		// vertices, e.g. the Khronos Fox) still shade smooth -- then
		// normalizes. Flat mode assigns each triangle's normalized face
		// normal to its corners. Without either, Normals stays empty, its
		// buffer stays dirty, and Shader::BindMeshData warns + the mesh
		// renders unlit.
		if (!has_normals && verts_added > 0) {
			const size_t vbase_f = pos_before * 3;
			std::vector<float> nx(verts_added, 0.0f), ny(verts_added, 0.0f), nz(verts_added, 0.0f);
			const auto& idx = sub->Indices.Data;

			// Position-weld map for smooth mode (exact float match --
			// exporter duplicates are bit-identical).
			struct PosKey {
				float x, y, z;
				bool operator==(const PosKey& o) const { return x == o.x && y == o.y && z == o.z; }
			};
			struct PosKeyHash {
				size_t operator()(const PosKey& k) const {
					uint32_t ux, uy, uz;
					std::memcpy(&ux, &k.x, 4); std::memcpy(&uy, &k.y, 4); std::memcpy(&uz, &k.z, 4);
					size_t h = (size_t)ux * 0x9E3779B1u;
					h ^= (size_t)uy * 0x85EBCA77u; h ^= (size_t)uz * 0xC2B2AE3Du;
					return h;
				}
			};
			std::unordered_map<PosKey, size_t, PosKeyHash> weld;
			if (normal_gen == GltfImporter::Options::NormalGen::Smooth) {
				weld.reserve(verts_added);
				for (size_t v = 0; v < verts_added; ++v) {
					PosKey k{ mesh->Positions.Data[vbase_f + v * 3],
							  mesh->Positions.Data[vbase_f + v * 3 + 1],
							  mesh->Positions.Data[vbase_f + v * 3 + 2] };
					weld.try_emplace(k, v);
				}
			}

			for (size_t ti = 0; ti + 2 < idx.size(); ti += 3) {
				const unsigned int l0 = idx[ti] - vertex_base;
				const unsigned int l1 = idx[ti + 1] - vertex_base;
				const unsigned int l2 = idx[ti + 2] - vertex_base;
				const float* p0 = &mesh->Positions.Data[vbase_f + l0 * 3];
				const float* p1 = &mesh->Positions.Data[vbase_f + l1 * 3];
				const float* p2 = &mesh->Positions.Data[vbase_f + l2 * 3];
				const float ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
				const float bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
				const float cx = ay * bz - az * by;
				const float cy = az * bx - ax * bz;
				const float cz = ax * by - ay * bx;

				if (normal_gen == GltfImporter::Options::NormalGen::Flat) {
					float len = std::sqrt(cx * cx + cy * cy + cz * cz);
					const float inv = len > 1e-12f ? 1.0f / len : 0.0f;
					nx[l0] = nx[l1] = nx[l2] = cx * inv;
					ny[l0] = ny[l1] = ny[l2] = cy * inv;
					nz[l0] = nz[l1] = nz[l2] = cz * inv;
				}
				else {
					// Accumulate into the canonical (first-seen) vertex of
					// each corner's position group.
					const unsigned int c0 = (unsigned int)weld[{ p0[0], p0[1], p0[2] }];
					const unsigned int c1 = (unsigned int)weld[{ p1[0], p1[1], p1[2] }];
					const unsigned int c2 = (unsigned int)weld[{ p2[0], p2[1], p2[2] }];
					nx[c0] += cx; ny[c0] += cy; nz[c0] += cz;
					nx[c1] += cx; ny[c1] += cy; nz[c1] += cz;
					nx[c2] += cx; ny[c2] += cy; nz[c2] += cz;
				}
			}
			for (size_t v = 0; v < verts_added; ++v) {
				if (normal_gen == GltfImporter::Options::NormalGen::Smooth) {
					// Broadcast the canonical normal across the weld group.
					PosKey k{ mesh->Positions.Data[vbase_f + v * 3],
							  mesh->Positions.Data[vbase_f + v * 3 + 1],
							  mesh->Positions.Data[vbase_f + v * 3 + 2] };
					const size_t c = weld[k];
					nx[v] = nx[c]; ny[v] = ny[c]; nz[v] = nz[c];
				}
				float len = std::sqrt(nx[v] * nx[v] + ny[v] * ny[v] + nz[v] * nz[v]);
				if (len > 1e-12f) { nx[v] /= len; ny[v] /= len; nz[v] /= len; }
				mesh->Normals.Data.push_back(nx[v]);
				mesh->Normals.Data.push_back(ny[v]);
				mesh->Normals.Data.push_back(nz[v]);
			}
		}

		vertex_base += static_cast<unsigned int>(verts_added);
	}

	// Weld + dedup coincident vertices. Skipped on ragged optional
	// buffers to avoid OOB in OptimizeMesh.
	if (optimize_mesh) {
		const size_t vcount = mesh->Positions.Data.size() / 3;
		auto consistent = [vcount](size_t sz, size_t stride) {
			return sz == 0 || sz == vcount * stride;
		};
		if (vcount > 0
			&& consistent(mesh->Normals.Data.size(), 3)
			&& consistent(mesh->Tangents.Data.size(), 3)
			&& consistent(mesh->UVs.Data.size(), 2)
			&& consistent(mesh->IDs.Data.size(), 4)
			&& consistent(mesh->Weights.Data.size(), 3)) {
			MeshUtil::OptimizeMesh(mesh);
		} else if (vcount > 0) {
			FURYW << "gltf-importer: mesh '" << name << "' has ragged vertex "
				  << "streams; skipping weld/dedup (OptimizeMesh)";
		}
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
	const tinygltf::Model& model,
	int skin_index,
	const std::shared_ptr<Mesh>& mesh) {
	const auto& skin = model.skins[skin_index];

	// Read inverse-bind matrices (vec16 floats per joint).
	std::vector<float> ibms;
	if (skin.inverseBindMatrices >= 0)
		ReadFloatAccessor(model, skin.inverseBindMatrices, 16, ibms);

	std::vector<Joint::Ptr> joints;
	joints.reserve(skin.joints.size());

	// First pass: create joints, set local + offset matrices.
	for (size_t j = 0; j < skin.joints.size(); ++j) {
		int node_index = skin.joints[j];
		if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size())) continue;
		const auto& node = model.nodes[node_index];
		const std::string jname = node.name.empty()
									  ? "Joint_" + std::to_string(node_index)
									  : node.name;
		auto joint = Joint::Create(jname, mesh);
		joint->SetLocalMatrix(NodeLocalMatrix(node));
		if (ibms.size() >= (j + 1) * 16) {
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

	for (size_t j = 0; j < skin.joints.size(); ++j) {
		int node_index = skin.joints[j];
		const auto& node = model.nodes[node_index];
		for (int child_node : node.children) {
			auto child_it = node_to_jointidx.find(child_node);
			if (child_it == node_to_jointidx.end()) continue; // non-joint child
			auto parent = joints[j];
			auto child = joints[child_it->second];
			child->SetParent(parent);
			auto existing = parent->GetFirstChild();
			child->SetSibling(existing);
			parent->SetFirstChild(child);
		}
	}
	// (intentionally no per-joint debug log here -- joint structure is
	// verified through the visualization overlay)

	// Determine root: prefer skin.skeleton (a node index) if it's in
	// the joint set; else use the first joint.
	Joint::Ptr root;
	if (skin.skeleton >= 0) {
		auto it = node_to_jointidx.find(skin.skeleton);
		if (it != node_to_jointidx.end()) root = joints[it->second];
	}
	if (!root && !joints.empty()) root = joints[0];

	// Attach to mesh: the m_Joints / m_JointMap / m_RootJoint fields
	// are protected -- we use the Mesh's friend-class trick? No,
	// they're not accessible from a helper. We'll need to add a
	// public setter or befriend GltfImporter. The cleanest path is
	// a small public setter pair on Mesh.
	//
	// (See companion Mesh.h change.)
	mesh->SetJointTree(joints, root);
	return true;
}

// Extract bytes for embedded images: deleted. Embedded image bytes
// flow through Texture::CreateFromMemory at import time and are
// extracted to disk by FileUtil::SaveFile / SaveCompressedFile when
// (and only when) the scene is serialized.

// Build engine Light prototypes from KHR_lights_punctual definitions
// in `model.lights`. Each prototype is cloned per-node when attached
// (Light::Clone()) so SceneNodes don't share state.
//
// Mapping (lossy in v1):
//   point  -> LightType::POINT  (range -> radius; 0/unset -> 10.0 default)
//   spot   -> LightType::SPOT   (range -> radius; inner/outer cones in radians, 1:1)
//   directional -> LightType::DIRECTIONAL (range ignored)
// color  (linear float[3]) -> engine Color (alpha = 1)
// intensity              -> engine intensity (1:1; PBR-unit conversion deferred)
//
// glTF "range == 0" means infinite per spec, but the engine's deferred
// Lambert pipeline draws a finite light-volume mesh scaled by radius;
// radius 0 produces a zero-size volume and the light contributes
// nothing. We default to 10.0 (matches the demo scale of hand-authored
// scene.json lights) so imported lights are visible by default. Users
// who need a different falloff can edit the scene file post-import.
//
// CalculateAABB is invoked after all fields are set so OnAttaching
// (which copies m_AABB onto the SceneNode for octree visibility) sees
// the right bounds.
//
// Unknown types fall back to POINT with a one-line warning (no abort).
// kDefaultPointSpotRadius is the engine-units (cm) stand-in for glTF
// range==0 (infinite per spec) -- see BuildLightPrototypes' comment.
constexpr float kDefaultPointSpotRadius = 10.0f;
void BuildLightPrototypes(
	const tinygltf::Model& model,
	std::vector<Light::Ptr>& prototypes) {
	prototypes.clear();
	prototypes.reserve(model.lights.size());
	for (size_t i = 0; i < model.lights.size(); ++i) {
		const auto& gl = model.lights[i];
		auto light = Light::Create();

		if (gl.type == "point")
			light->SetType(LightType::POINT);
		else if (gl.type == "spot")
			light->SetType(LightType::SPOT);
		else if (gl.type == "directional")
			light->SetType(LightType::DIRECTIONAL);
		else {
			FURYW << "gltf-importer: light[" << i << "] '" << gl.name
				  << "' has unknown type '" << gl.type
				  << "'; defaulting to POINT";
			light->SetType(LightType::POINT);
		}

		if (gl.color.size() >= 3)
			light->SetColor(Color(
				static_cast<float>(gl.color[0]),
				static_cast<float>(gl.color[1]),
				static_cast<float>(gl.color[2]),
				1.0f));

		light->SetIntensity(static_cast<float>(gl.intensity));

		if (gl.type != "directional") {
			float radius = (gl.range > 0.0)
							   ? static_cast<float>(gl.range)
							   : kDefaultPointSpotRadius;
			light->SetRadius(radius);
		}

		if (gl.type == "spot") {
			light->SetInnerAngle(static_cast<float>(gl.spot.innerConeAngle));
			light->SetOutterAngle(static_cast<float>(gl.spot.outerConeAngle));
		}

		// Build the AABB from the now-populated fields. OnAttaching
		// reads it onto the SceneNode for octree visibility queries.
		light->CalculateAABB();

		prototypes.push_back(light);
	}
}

// Walk a glTF node and emit a matching engine SceneNode tree under
// `parent`. Recursive over node.children. Each emitted SceneNode gets
// a Transform component (carrying TRS) and, if the glTF node refs a
// mesh, a MeshRender component with the right material list per
// submesh.
//
// gltf_node_to_scene_node out-parameter maps glTF node indices to the
// engine SceneNodes we created -- used by the animation pass to look
// up channel targets.
void WalkNode(
	const tinygltf::Model& model,
	int node_index,
	const std::shared_ptr<SceneNode>& parent,
	const std::vector<std::shared_ptr<Mesh>>& meshes,
	const std::vector<std::shared_ptr<Material>>& materials,
	const std::vector<std::vector<int>>& submesh_to_gltf_material,
	const std::vector<Light::Ptr>& light_prototypes,
	int& lights_attached,
	std::vector<std::shared_ptr<SceneNode>>& gltf_node_to_scene_node) {
	if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size())) return;
	const auto& node = model.nodes[node_index];
	const std::string nname = node.name.empty()
								  ? "Node_" + std::to_string(node_index)
								  : node.name;
	auto sn = SceneNode::Create(nname);

	// TRS -> SceneNode local transform. If glTF gives a matrix only,
	// decompose by hand (the engine's SceneNode doesn't have a "set
	// matrix" path). Fallback: identity if neither is set.
	if (node.matrix.size() == 16) {
		// glTF column-major matrix. We pass through the Matrix4 path
		// then let SceneNode rebuild from local matrix. SceneNode
		// doesn't expose SetLocalMatrix; the closest path is to set
		// TRS components individually. Use Matrix4 -> Decompose if
		// we want this to be lossless.
		//
		// For v1, prefer node.translation/rotation/scale when present
		// even if a matrix is also set (glTF spec: only one form is
		// allowed per node anyway).
		FURYW << "gltf-importer: node '" << nname
			  << "' uses raw 4x4 matrix; v1 supports TRS-decomposed transforms only -- "
			  << "transform may be incorrect. Re-export with TRS or use a glTF tool to decompose.";
	}
	if (node.translation.size() == 3)
		sn->SetLocalPosition(Vector4(
			static_cast<float>(node.translation[0]),
			static_cast<float>(node.translation[1]),
			static_cast<float>(node.translation[2]), 1.0f));
	if (node.rotation.size() == 4)
		sn->SetLocalRoattion(Quaternion(
			static_cast<float>(node.rotation[0]),
			static_cast<float>(node.rotation[1]),
			static_cast<float>(node.rotation[2]),
			static_cast<float>(node.rotation[3])));
	if (node.scale.size() == 3)
		sn->SetLocalScale(Vector4(
			static_cast<float>(node.scale[0]),
			static_cast<float>(node.scale[1]),
			static_cast<float>(node.scale[2]), 1.0f));

	// Transform component first (engine convention -- even though TRS
	// is also tracked directly on SceneNode, the Transform component
	// participates in the interpolation pipeline).
	sn->AddComponent(Transform::Create());

	// MeshRender component if this node references a mesh.
	if (node.mesh >= 0 && node.mesh < static_cast<int>(meshes.size())) {
		auto mesh = meshes[node.mesh];
		const auto& per_sub_mat = submesh_to_gltf_material[node.mesh];
		// Default material reference for slot 0; subsequent submeshes
		// get SetMaterial(idx).
		Material::Ptr first_mat;
		if (!per_sub_mat.empty() && per_sub_mat[0] >= 0 && per_sub_mat[0] < static_cast<int>(materials.size()))
			first_mat = materials[per_sub_mat[0]];
		auto render = MeshRender::Create(first_mat, mesh);
		for (size_t s = 1; s < per_sub_mat.size(); ++s) {
			if (per_sub_mat[s] >= 0 && per_sub_mat[s] < static_cast<int>(materials.size()))
				render->SetMaterial(materials[per_sub_mat[s]], static_cast<unsigned int>(s));
		}

		sn->AddComponent(render);
	}

	if (node.camera >= 0)
		FURYI << "gltf-importer: node '" << nname << "' references a glTF camera (skipping; v1 doesn't bind cameras)";

	// KHR_lights_punctual: attach a Light component if this node
	// references a light. Out-of-range indices are warned and skipped
	// rather than aborting the import.
	if (node.light >= 0) {
		if (node.light < static_cast<int>(light_prototypes.size())) {
			auto light = std::dynamic_pointer_cast<Light>(
				light_prototypes[node.light]->Clone());
			if (light) {
				sn->AddComponent(light);
				++lights_attached;
				const auto col = light->GetColor();
				FURYI << "gltf-importer: attached "
					  << EnumUtil::LightTypeToString(light->GetType())
					  << " light to node '" << nname
					  << "' (intensity=" << light->GetIntensity()
					  << ", color=(" << col.r << "," << col.g << "," << col.b << "))";

				// FBX-rooted glTFs typically inherit a 100x cm-to-m
				// scale on every node, including light-only nodes.
				// The deferred-Lambert pipeline draws the point-light
				// volume by `worldMatrix.AppendScale(light_radius)`
				// (PrelightPipeline.cpp), so a parent scale of 100
				// produces a volume mesh thousands of units wide
				// which gets z-clipped against the camera far plane
				// and contributes no fragments. Light-only nodes
				// have no geometry, so the scale is parasitic --
				// reset it to 1 so the volume mesh renders at a
				// sensible size.
				sn->SetLocalScale(Vector4(1.0f, 1.0f, 1.0f, 1.0f));

				// Units: glTF light range is in METRES (spec), the
				// engine is 1 unit = 1 cm, so radius = range x 100.
				// The range==0 stand-in (kDefaultPointSpotRadius) is
				// already engine units -- no conversion. The volume
				// scales by the node's world scale, so divide it
				// back out -- after the strip above the world scale
				// is the parent's. Keeps the world-space volume
				// correct for FBX-rooted scenes (parent ~100x) and
				// pure glTFs (parent 1x) alike.
				if (light->GetType() != LightType::DIRECTIONAL) {
					float parent_scale = 1.0f;
					if (parent) parent_scale = parent->GetWorldScale().x;
					if (parent_scale < 1e-6f) parent_scale = 1.0f;
					const auto& gl = model.lights[node.light];
					float radius = (gl.range > 0.0)
									   ? static_cast<float>(gl.range) * 100.0f / parent_scale
									   : kDefaultPointSpotRadius;
					light->SetRadius(radius);
					light->CalculateAABB();
				}
			}
		} else {
			FURYW << "gltf-importer: node '" << nname
				  << "' references light index " << node.light
				  << " but model has only " << light_prototypes.size()
				  << " light(s); skipping";
		}
	}

	sn->Recompose(false);
	parent->AddChild(sn);
	gltf_node_to_scene_node[node_index] = sn;

	for (int child : node.children)
		WalkNode(model, child, sn, meshes, materials, submesh_to_gltf_material,
				 light_prototypes, lights_attached, gltf_node_to_scene_node);
}

// Resample a glTF animation sampler at the engine's fixed tick rate
// (24 fps). Time inputs are float seconds; output samples are vec3 or
// vec4 (quat). For vec3 we linearly interpolate; for quat we slerp.
// CUBICSPLINE samples are treated as LINEAR with a one-shot warning.
//
// Each output keyframe has tick = round(t_seconds * 24).
struct ResampledKeys {
	std::vector<KeyFrame> values; // KeyFrame.tick + (x,y,z); rotation stores Euler radians
};

// Read a sampler's time/value pair.
bool ReadSampler(
	const tinygltf::Model& model,
	const tinygltf::AnimationSampler& sampler,
	std::vector<float>& times,
	std::vector<float>& values,
	int& value_components) {
	if (!ReadFloatAccessor(model, sampler.input, 1, times)) return false;
	const auto& val_acc = model.accessors[sampler.output];
	value_components = tinygltf::GetNumComponentsInType(val_acc.type);
	if (value_components <= 0) return false;
	if (!ReadFloatAccessor(model, sampler.output, value_components, values)) return false;
	return true;
}

// Linearly interpolate between two vec3 samples at parameter u (0..1).
void LerpVec3(const float* a, const float* b, float u, float* out) {
	out[0] = a[0] + (b[0] - a[0]) * u;
	out[1] = a[1] + (b[1] - a[1]) * u;
	out[2] = a[2] + (b[2] - a[2]) * u;
}

// Slerp between two quaternions sampled from a glTF rotation channel.
Quaternion SlerpQuat(const float* a, const float* b, float u) {
	Quaternion qa(a[0], a[1], a[2], a[3]);
	Quaternion qb(b[0], b[1], b[2], b[3]);
	return qa.Slerp(qb, u);
}

// Resample one animation channel into engine KeyFrames at 24 fps.
// path = "translation" | "rotation" | "scale".
void ResampleChannel(
	const std::vector<float>& times,
	const std::vector<float>& values,
	int value_components,
	const std::string& path,
	float ticks_per_second,
	std::vector<KeyFrame>& out) {
	if (times.empty() || values.empty()) return;
	const float t_start = times.front();
	const float t_end = times.back();
	const unsigned int tick_start = static_cast<unsigned int>(std::floor(t_start * ticks_per_second));
	const unsigned int tick_end = static_cast<unsigned int>(std::ceil(t_end * ticks_per_second));

	size_t cursor = 0; // index of the *next* sample to advance past
	for (unsigned int tick = tick_start; tick <= tick_end; ++tick) {
		float t = static_cast<float>(tick) / ticks_per_second;
		// Find the bracketing samples around t.
		while (cursor + 1 < times.size() && times[cursor + 1] < t) ++cursor;
		const float t0 = times[cursor];
		const float t1 = (cursor + 1 < times.size()) ? times[cursor + 1] : t0;
		const float span = (t1 > t0) ? (t1 - t0) : 0.0f;
		const float u = (span > 0.0f) ? std::clamp((t - t0) / span, 0.0f, 1.0f) : 0.0f;

		const float* v0 = &values[cursor * value_components];
		const float* v1 = (cursor + 1 < times.size())
							  ? &values[(cursor + 1) * value_components]
							  : v0;

		if (path == "rotation") {
			Quaternion q = SlerpQuat(v0, v1, u);
			Vector4 e = MathUtil::QuatToEulerRad(q);
			out.emplace_back(tick, e.x, e.y, e.z);
		} else {
			float v[3] = {0, 0, 0};
			LerpVec3(v0, v1, u, v);
			out.emplace_back(tick, v[0], v[1], v[2]);
		}
	}
}
} // namespace

// ---- Kraut postprocess (add-kraut-vegetation) ---------------------------
// Kraut-exported trees (KrautCLI `export --format glb`) carry
// asset.extras.kraut: { seed, descriptor, lod_thresholds?, billboard:
// { atlas_cols, atlas_rows, mode, texture, ... } }. When present, this
// pass replaces the name-suffix chain's synthesized thresholds, attaches
// the <base>_Billboard mesh as the flagged terminal tier, sets foliage
// material flags (two-sided on MASK, wind on all), and scales the tree's
// root nodes meters -> cm (kraut glb is meters per glTF spec; fury's
// world unit is cm -- LOD coverage math is scale-invariant).
static void KrautImportPostprocess(
	const tinygltf::Model& model,
	const Scene::Ptr& scene,
	const std::vector<std::shared_ptr<Mesh>>& meshes,
	const std::vector<std::shared_ptr<Material>>& materials,
	const std::vector<std::vector<int>>& submesh_to_gltf_material,
	std::vector<std::shared_ptr<SceneNode>>& gltf_node_to_scene_node)
{
	if (!model.asset.extras.IsObject())
		return;
	const auto& extrasObj = model.asset.extras.Get<tinygltf::Value::Object>();
	auto krautIt = extrasObj.find("kraut");
	if (krautIt == extrasObj.end() || !krautIt->second.IsObject())
		return;
	const auto& kraut = krautIt->second.Get<tinygltf::Value::Object>();

	std::vector<float> thresholds;
	if (auto it = kraut.find("lod_thresholds"); it != kraut.end() && it->second.IsArray())
	{
		for (const auto& v : it->second.Get<tinygltf::Value::Array>())
			if (v.IsNumber())
				thresholds.push_back(static_cast<float>(v.GetNumberAsDouble()));
	}

	int atlasCols = 8, atlasRows = 1;
	float bbUpBias = 0.28f;
	if (auto it = kraut.find("billboard"); it != kraut.end() && it->second.IsObject())
	{
		const auto& bb = it->second.Get<tinygltf::Value::Object>();
		if (auto c = bb.find("atlas_cols"); c != bb.end() && c->second.IsNumber())
			atlasCols = c->second.GetNumberAsInt();
		if (auto r = bb.find("atlas_rows"); r != bb.end() && r->second.IsNumber())
			atlasRows = r->second.GetNumberAsInt();
		// optional per-asset shading-normal override (10.3 tuned 0.28 for
		// tree canopies; grass wants 1.0 = pure up, matching its card
		// normals so the billboard tier doesn't go dark)
		if (auto u = bb.find("up_bias"); u != bb.end() && u->second.IsNumber())
			bbUpBias = static_cast<float>(u->second.GetNumberAsDouble());
	}

	// Foliage flags on all kraut tree materials: wind everywhere (trunk
	// weights are ~0 at the base but branches sway), two-sided on MASK
	// (leaf/frond cutouts).
	for (auto& mat : materials)
	{
		mat->SetWindEnabled(true);
		if (mat->GetAlphaMode() == AlphaMode::MASK)
			mat->SetTwoSided(true);
	}

	// Canopy-normal bend: kraut leaf cards carry horizontal (card-plane)
	// normals, so raw geometric normals leave the canopy dark from overhead
	// while the old camera-facing flip washed it flat at low sun. Bend
	// foliage normals toward a flattened crown-dome proxy instead:
	// proxy = normalize(dir.x, dir.y*0.4 + crownR*0.6, dir.z) with dir from
	// the crown center -- the canopy reads as a volumetric dome at every
	// sun angle. Applied per tier so all chain levels shade identically
	// (deep-tier generator normals are degraded). Height < 2 m meshes are
	// skipped: ground cover (grass) uses deliberate all-up normals.
	for (size_t mi = 0; mi < meshes.size(); ++mi)
	{
		auto& m = meshes[mi];
		const BoxBounds aabb = m->GetAABB();
		const float height = aabb.GetMax().y - aabb.GetMin().y;
		if (height < 2.0f || m->GetSubMeshCount() == 0)
			continue;

		const float cx = (aabb.GetMin().x + aabb.GetMax().x) * 0.5f;
		const float cz = (aabb.GetMin().z + aabb.GetMax().z) * 0.5f;
		const float crownTop = aabb.GetMin().y + height * 0.85f;
		const float crownR = std::max(aabb.GetMax().x - aabb.GetMin().x,
			aabb.GetMax().z - aabb.GetMin().z) * 0.5f;

		bool bent = false;
		for (unsigned int si = 0; si < m->GetSubMeshCount(); ++si)
		{
			// foliage submesh = MASK + two-sided material (trunk/bark keep
			// their authored normals)
			bool foliage = false;
			if (mi < submesh_to_gltf_material.size() && si < submesh_to_gltf_material[mi].size())
			{
				int gm = submesh_to_gltf_material[mi][si];
				if (gm >= 0 && gm < static_cast<int>(materials.size()))
				{
					auto& mat = materials[gm];
					foliage = mat->GetAlphaMode() == AlphaMode::MASK && mat->GetTwoSided();
				}
			}
			if (!foliage)
				continue;

			auto sub = m->GetSubMeshAt(si);
			if (!sub)
				continue;
			// per-vertex, one-time at import. Indices visit shared verts
			// ~6x each -- skip repeats so big canopies stay linear.
			std::vector<char> seen(m->Positions.Data.size() / 3, 0);
			for (unsigned int vi : sub->Indices.Data)
			{
				size_t base = static_cast<size_t>(vi) * 3;
				if (vi >= seen.size() || seen[vi] ||
					base + 2 >= m->Normals.Data.size() || base + 2 >= m->Positions.Data.size())
					continue;
				seen[vi] = 1;
				float dx = m->Positions.Data[base] - cx;
				float dy = m->Positions.Data[base + 1] - crownTop;
				float dz = m->Positions.Data[base + 2] - cz;
				float px = dx, py = dy * 0.4f + crownR * 0.6f, pz = dz;
				float len = std::sqrt(px * px + py * py + pz * pz);
				if (len < 1e-5f)
					continue;
				m->Normals.Data[base] = px / len;
				m->Normals.Data[base + 1] = py / len;
				m->Normals.Data[base + 2] = pz / len;
				bent = true;
			}
		}
		if (bent)
		{
			m->Normals.SetDirty();
			m->SetDirty();
		}
	}

	// Attach each <base>_Billboard mesh as the flagged terminal LOD tier
	// of base's chain, and remove its standalone scene node.
	for (size_t mi = 0; mi < meshes.size(); ++mi)
	{
		const auto& name = meshes[mi]->GetName();
		if (name.size() < 10 || name.compare(name.size() - 10, 10, "_Billboard") != 0)
			continue;
		const std::string base = name.substr(0, name.size() - 10);

		std::shared_ptr<Mesh> lod0;
		for (auto& m : meshes)
		{
			if (m->GetName() == base + "_LOD0") { lod0 = m; break; }
		}
		if (!lod0)
		{
			FURYW << "gltf-importer: kraut billboard '" << name << "' has no " << base << "_LOD0 chain root";
			continue;
		}
		auto bbMesh = meshes[mi];

		std::vector<std::shared_ptr<Mesh>> chain;
		std::vector<bool> flags;
		for (unsigned int i = 1; i < lod0->GetLodCount(); ++i)
		{
			chain.push_back(lod0->GetLodMesh(i));
			flags.push_back(lod0->IsLodBillboard(i));
		}
		chain.push_back(bbMesh);
		flags.push_back(true);

		std::vector<float> newThresholds;
		if (!thresholds.empty() && thresholds.size() == chain.size())
		{
			newThresholds = thresholds;
		}
		else
		{
			if (!thresholds.empty())
				FURYW << "gltf-importer: kraut lod_thresholds count (" << thresholds.size()
					  << ") != chain size (" << chain.size() << "); using synthesized thresholds";
			for (unsigned int i = 1; i < lod0->GetLodCount(); ++i)
				newThresholds.push_back(lod0->GetLodThreshold(i));
			// billboard tier: below the deepest mesh tier
			float last = newThresholds.empty() ? 1.0f : newThresholds.back();
			newThresholds.push_back(last * 0.5f);
		}
		// SetLodMeshes validates non-increasing; clamp defensively
		for (size_t i = 1; i < newThresholds.size(); ++i)
			if (newThresholds[i] > newThresholds[i - 1])
				newThresholds[i] = newThresholds[i - 1];
		// Cap the billboard tier's threshold: a mesh->billboard swap is only
		// imperceptible once the tree is small on screen; the kraut exporter's
		// default (1.5x the deepest tier's distance) puts it at ~25% coverage
		// where the flat atlas look visibly pops. 0.12 keeps billboards to
		// distant trees; the deepest mesh tier covers the gap.
		if (!newThresholds.empty())
			newThresholds.back() = std::min(newThresholds.back(), 0.12f);

		lod0->SetLodMeshes(chain, newThresholds, flags);

		// billboard material: atlas dims uniform + foliage flags
		if (mi < submesh_to_gltf_material.size() && !submesh_to_gltf_material[mi].empty())
		{
			int gm = submesh_to_gltf_material[mi][0];
			if (gm >= 0 && gm < static_cast<int>(materials.size()))
			{
				auto bbMat = materials[gm];
				bbMat->SetUniform("u_billboard_atlas",
					Uniform2f::Create({static_cast<float>(atlasCols), static_cast<float>(atlasRows)}));
				// Tuned against the mesh tiers (10.3): camera-facing
				// normals go ~10x dark under overhead sun, pure up
				// overshoots ~3x; 0.28 lands within ~0% at high/front sun.
				// extras.kraut.billboard.up_bias overrides per asset.
				bbMat->SetUniform("u_billboard_up_bias", Uniform1f::Create({ bbUpBias }));
				bbMat->SetAlphaMode(AlphaMode::MASK);
				bbMat->SetTwoSided(true);
				// billboards never sway: displacing a camera-facing quad
				// around its origin reads as sliding, not wind
				bbMat->SetWindEnabled(false);
				// the billboard tier's material travels with the mesh
				// (renderer material slots key to LOD 0's submeshes)
				lod0->SetBillboardMaterial(bbMat);
			}
		}

		// detach the billboard's standalone scene node (it's a LOD tier,
		// not a scene object)
		for (size_t ni = 0; ni < model.nodes.size() && ni < gltf_node_to_scene_node.size(); ++ni)
		{
			if (model.nodes[ni].mesh == static_cast<int>(mi) && gltf_node_to_scene_node[ni])
			{
				gltf_node_to_scene_node[ni]->RemoveFromParent();
				gltf_node_to_scene_node[ni].reset();
			}
		}

		FURYI << "gltf-importer: kraut tree '" << base << "': chain of "
			  << lod0->GetLodCount() << " tiers (billboard terminal, atlas "
			  << atlasCols << "x" << atlasRows << ")";
	}

	// Also detach the _LOD1..N nodes of every kraut chain: kraut tiers are
	// co-located chain members, not scene objects -- leaving their nodes
	// would draw the tree once per tier (overlapping duplicates). The
	// LOD0 node's MeshRender with the chain is the single live renderer.
	for (size_t mi = 0; mi < meshes.size() && mi < model.meshes.size(); ++mi)
	{
		const auto& name = meshes[mi]->GetName();
		auto pos = name.rfind("_LOD");
		if (pos == std::string::npos || pos + 4 >= name.size())
			continue;
		// tier index must be > 0 (LOD0 stays)
		bool allDigits = true;
		for (size_t c = pos + 4; c < name.size(); ++c)
			if (name[c] < '0' || name[c] > '9') { allDigits = false; break; }
		if (!allDigits || std::atoi(name.c_str() + pos + 4) == 0)
			continue;
		for (size_t ni = 0; ni < model.nodes.size() && ni < gltf_node_to_scene_node.size(); ++ni)
		{
			if (model.nodes[ni].mesh == static_cast<int>(mi) && gltf_node_to_scene_node[ni])
			{
				gltf_node_to_scene_node[ni]->RemoveFromParent();
				gltf_node_to_scene_node[ni].reset();
			}
		}
	}

	// meters -> cm at the tree's top-level nodes
	auto root = scene->GetRootNode();
	for (unsigned int i = 0; i < root->GetChildCount(); ++i)
	{
		auto c = root->GetChildAt(i);
		c->SetLocalScale(c->GetLocalScale() * 100.0f);
		c->Recompose(true);
	}
}

std::shared_ptr<Scene> GltfImporter::Import(
	const std::string& input_path,
	const std::string& scene_name,
	const std::string& working_dir,
	const Options& opts) {
	tinygltf::TinyGLTF loader;
	// We build with TINYGLTF_NO_STB_IMAGE (the engine vendors its own stb;
	// having two copies linked is an ODR violation). Tell tinygltf not to
	// decode image bytes -- we keep the raw encoded bytes in the bufferView
	// and copy them out later for embedded-image extraction.
	loader.SetImagesAsIs(true);
	// SetImagesAsIs flags the load_image_option but tinygltf still requires
	// a non-null LoadImageData callback at parse time; install a no-op that
	// just returns success (we ignore the decoded result).
	loader.SetImageLoader(
		[](tinygltf::Image*, const int, std::string*, std::string*,
		   int, int, const unsigned char*, int, void*) -> bool {
			return true;
		},
		nullptr);
	tinygltf::Model model;
	std::string err, warn;

	bool ok = false;
	if (HasSuffix(input_path, ".glb"))
		ok = loader.LoadBinaryFromFile(&model, &err, &warn, input_path);
	else if (HasSuffix(input_path, ".gltf"))
		ok = loader.LoadASCIIFromFile(&model, &err, &warn, input_path);
	else {
		FURYE << "gltf-importer: input '" << input_path
			  << "' has neither .gltf nor .glb extension";
		return nullptr;
	}

	if (!warn.empty()) FURYW << "gltf-importer: tinygltf warn: " << warn;
	if (!ok || !err.empty()) {
		FURYE << "gltf-importer: failed to load '" << input_path
			  << "': " << (err.empty() ? "(no error message)" : err);
		return nullptr;
	}

	if (HasUnsupportedFeatures(model, input_path))
		return nullptr;

	// Embedded image bytes are routed through Texture::CreateFromMemory
	// during material translation below; extraction to sibling files is
	// deferred to FileUtil::SaveFile / SaveCompressedFile.

	// Derive a stem from the input path for synthesizing
	// "<stem>_image<i>.<ext>" original-filename hints when the embedded
	// glTF image lacks an `image.name` (which FBX2glTF normally fills in).
	std::string input_stem;
	std::string input_dir; // directory of the .gltf/.glb, with trailing slash -- for resolving relative image URIs.
	{
		auto slash = input_path.find_last_of("/\\");
		std::string base = (slash == std::string::npos)
							   ? input_path
							   : input_path.substr(slash + 1);
		auto dot = base.find_last_of('.');
		input_stem = (dot == std::string::npos) ? base : base.substr(0, dot);
		input_dir = (slash == std::string::npos) ? std::string() : input_path.substr(0, slash + 1);
	}

	auto tree = OcTree::Create();
	auto scene = Scene::Create(scene_name, working_dir, tree);
	auto entities = scene->GetEntityManager();

	// Materials. We translate them all up-front so meshes (next groups)
	// can reference engine Material::Ptrs by glTF material index.
	std::set<std::string> warned_signatures;
	std::vector<std::shared_ptr<Material>> materials;
	materials.reserve(model.materials.size());
	for (size_t mi = 0; mi < model.materials.size(); ++mi) {
		auto mat = TranslateMaterial(model, static_cast<int>(mi),
									 input_stem, input_dir, opts.hdr_target,
									 warned_signatures);
		entities->Add(mat);
		// Register the material's textures as first-class assets so
		// the picker can find them via em->ForEach<Texture>. Add
		// returns false if the texture is already registered (same
		// UUID) -- that's fine, just means it's a duplicate reference.
		for (const auto& kv : mat->GetTextures()) {
			if (kv.second)
				entities->Add(kv.second);
		}
		materials.push_back(mat);
	}

	// Meshes. Each glTF mesh becomes one engine Mesh; each glTF primitive
	// becomes one SubMesh. The submesh_to_gltf_material map below carries
	// each submesh's glTF material index forward so the node-walking pass
	// can wire MeshRender's material list.
	std::vector<std::shared_ptr<Mesh>> meshes;
	std::vector<std::vector<int>> submesh_to_gltf_material; // [mesh_i][sub_j] = gltf_material_index or -1
	meshes.reserve(model.meshes.size());
	submesh_to_gltf_material.reserve(model.meshes.size());
	for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
		std::vector<int> per_submesh_mat;
		auto m = TranslateMesh(model, static_cast<int>(mi), opts.normal_gen, opts.optimize_mesh, per_submesh_mat);
		if (!m) return nullptr; // unrecoverable translation error
		entities->Add(m);
		meshes.push_back(m);
		submesh_to_gltf_material.push_back(std::move(per_submesh_mat));
	}

	// Skins. A glTF node references both a mesh and a skin; the engine
	// stores joints on the Mesh itself, so we attach the skin to the mesh
	// the first referencing node names. v1 rejects multi-skin-per-mesh.
	std::vector<int> mesh_to_skin(meshes.size(), -1); // -1 = no skin
	for (const auto& node : model.nodes) {
		if (node.mesh < 0 || node.skin < 0) continue;
		if (node.mesh >= static_cast<int>(meshes.size())) continue;
		int existing = mesh_to_skin[node.mesh];
		if (existing >= 0 && existing != node.skin) {
			FURYE << "gltf-importer: mesh '" << meshes[node.mesh]->GetName()
				  << "' is referenced by nodes using different skins ("
				  << existing << " and " << node.skin
				  << ") -- v1 supports one skin per mesh, rejecting";
			return nullptr;
		}
		mesh_to_skin[node.mesh] = node.skin;
	}
	for (size_t mi = 0; mi < meshes.size(); ++mi) {
		if (mesh_to_skin[mi] < 0) continue;
		if (!TranslateSkin(model, mesh_to_skin[mi], meshes[mi])) return nullptr;
	}

	// Node tree walk. Roots: either the default scene's nodes, or every
	// root node if no default. WalkNode populates the gltf_node_to_scene_node
	// map so the animation pass can resolve channel targets by node index.
	std::vector<Light::Ptr> light_prototypes;
	BuildLightPrototypes(model, light_prototypes);

	int lights_attached = 0;
	std::vector<std::shared_ptr<SceneNode>> gltf_node_to_scene_node(model.nodes.size());
	auto root = scene->GetRootNode();
	int default_scene = model.defaultScene >= 0 ? model.defaultScene : 0;
	if (default_scene < static_cast<int>(model.scenes.size())) {
		for (int n : model.scenes[default_scene].nodes)
			WalkNode(model, n, root, meshes, materials, submesh_to_gltf_material,
					 light_prototypes, lights_attached, gltf_node_to_scene_node);
	}

	if (lights_attached == 0 && model.lights.empty()) {
		FURYW << "gltf-importer: '" << input_path
			  << "' has no lights -- viewport will render black under deferred Lambert pipeline";
	}

	// glTF-standard skinning: each Joint mirrors its glTF joint node's
	// SceneNode (already built by WalkNode into gltf_node_to_scene_node).
	// The joint's Final = sceneNodeWorld * ibm is computed at render
	// time from the scene graph (which includes ancestors like James,
	// the skeleton root's parent), and the skin shader uses an identity
	// model matrix. The glb inverseBindMatrices (already loaded onto
	// each Joint as m_OffsetMatrix by TranslateSkin) are used verbatim.
	// This is the glTF spec formula: v_world = Sum w_i * (J_i W * ibm_i) * v.
	if (!meshes.empty()) {
		for (size_t mi = 0; mi < meshes.size(); ++mi) {
			if (mesh_to_skin[mi] < 0) continue;
			const auto& skin = model.skins[mesh_to_skin[mi]];
			const auto& mesh = meshes[mi];
			for (size_t j = 0; j < skin.joints.size(); ++j) {
				int node_index = skin.joints[j];
				if (node_index < 0 || node_index >= static_cast<int>(gltf_node_to_scene_node.size()))
					continue;
				const auto& sn = gltf_node_to_scene_node[node_index];
				if (!sn) continue;
				auto joint = mesh->GetJoint(sn->GetName());
				if (joint) joint->SetSceneNode(sn);
			}
		}
	}

	// Animations: one engine AnimationClip per glTF animation, resampled
	// at opts.anim_ticks_per_second (24 by default).
	std::set<std::string> cubic_warned;
	for (size_t ai = 0; ai < model.animations.size(); ++ai) {
		const auto& anim = model.animations[ai];
		const std::string aname = anim.name.empty()
									  ? "Animation_" + std::to_string(ai)
									  : anim.name;
		auto clip = AnimationClip::Create(aname,
										  static_cast<int>(opts.anim_ticks_per_second));

		// Group channels by target node index (engine convention: one
		// channel per node, carrying positions/rotations/scalings).
		std::unordered_map<int, AnimationClip::ChannelPtr> by_target;
		for (size_t ci = 0; ci < anim.channels.size(); ++ci) {
			const auto& ch = anim.channels[ci];
			if (ch.target_node < 0) continue;
			if (ch.target_path == "weights") continue; // morph (already rejected earlier)
			if (ch.sampler < 0 || ch.sampler >= static_cast<int>(anim.samplers.size())) continue;

			const auto& sampler = anim.samplers[ch.sampler];
			if (sampler.interpolation == "CUBICSPLINE" && cubic_warned.insert("anim" + std::to_string(ai) + "ch" + std::to_string(ci)).second) {
				FURYW << "gltf-importer: animation '" << aname
					  << "' sampler " << ch.sampler
					  << " uses CUBICSPLINE; resampling as LINEAR";
			}

			std::vector<float> times, values;
			int vc = 0;
			if (!ReadSampler(model, sampler, times, values, vc)) {
				FURYW << "gltf-importer: animation '" << aname
					  << "' channel " << ci << " sampler unreadable; skipping";
				continue;
			}

			// Resolve the target's name (matches what TranslateSkin uses
			// for joint names and WalkNode uses for non-joint SceneNodes).
			if (ch.target_node >= static_cast<int>(model.nodes.size())) continue;
			const auto& target_node = model.nodes[ch.target_node];
			const std::string target_name = target_node.name.empty()
												? (target_node.skin >= 0
													   ? "Joint_" + std::to_string(ch.target_node)
													   : "Node_" + std::to_string(ch.target_node))
												: target_node.name;

			auto channel_it = by_target.find(ch.target_node);
			AnimationClip::ChannelPtr engine_ch;
			if (channel_it == by_target.end()) {
				engine_ch = clip->AddChannel(target_name);
				by_target[ch.target_node] = engine_ch;
			} else
				engine_ch = channel_it->second;

			std::vector<KeyFrame>* bucket = nullptr;
			if (ch.target_path == "translation")
				bucket = &engine_ch->positions;
			else if (ch.target_path == "rotation")
				bucket = &engine_ch->rotations;
			else if (ch.target_path == "scale")
				bucket = &engine_ch->scalings;
			if (!bucket) continue;

			ResampleChannel(times, values, vc, ch.target_path,
							opts.anim_ticks_per_second, *bucket);
		}

		clip->CalculateDuration();
		entities->Add(clip);
	}

	FURYI << "gltf-importer: '" << input_path << "' translated "
		  << materials.size() << " material(s), "
		  << meshes.size() << " mesh(es), "
		  << model.scenes.size() << " glTF scene(s) -> SceneNode tree, "
		  << model.animations.size() << " animation(s)";

	// Name-suffix LOD fallback. Group meshes whose names match
	// `<base>_LOD<n>` into
	// a LodGroup ordered by `n` ascending. Thresholds are synthesized
	// as a 1.0 -> 0.0 linear ramp. A LodGroup is built only when a
	// `<base>` has at least 2 entries; lone _LOD<n> meshes are kept
	// as plain Meshes and logged as a debug note.
	{
		struct GroupEntry {
			std::vector<std::shared_ptr<Mesh>> meshes;
			std::vector<int> indices;
		};
		std::unordered_map<std::string, GroupEntry> by_base;
		for (size_t mi = 0; mi < meshes.size(); ++mi) {
			const auto& name = meshes[mi]->GetName();
			auto pos = name.rfind("_LOD");
			if (pos == std::string::npos || pos + 4 >= name.size()) continue;
			bool all_digits = true;
			for (size_t i = pos + 4; i < name.size(); ++i) {
				if (name[i] < '0' || name[i] > '9') { all_digits = false; break; }
			}
			if (!all_digits) continue;
			int n = 0;
			for (size_t i = pos + 4; i < name.size(); ++i) {
				n = n * 10 + (name[i] - '0');
			}
			std::string base = name.substr(0, pos);
			by_base[base].meshes.push_back(meshes[mi]);
			by_base[base].indices.push_back(n);
		}
		for (auto& kv : by_base) {
			if (kv.second.meshes.size() < 2) {
				for (size_t i = 0; i < kv.second.meshes.size(); ++i) {
					FURYD << "gltf-importer: lone _LOD<N> mesh '" << kv.second.meshes[i]->GetName() << "' (no matching siblings; keeping as plain mesh)";
				}
				continue;
			}
			std::vector<size_t> order(kv.second.meshes.size());
			for (size_t i = 0; i < order.size(); ++i) order[i] = i;
			std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
				return kv.second.indices[a] < kv.second.indices[b];
			});
			bool contiguous = true;
			for (size_t i = 0; i < order.size(); ++i) {
				if (kv.second.indices[order[i]] != static_cast<int>(i)) {
					contiguous = false;
					break;
				}
			}
			if (!contiguous) {
				FURYW << "gltf-importer: name-suffix LOD group for base '" << kv.first << "' has non-contiguous indices; using 1.0 -> 0.0 linear ramp anyway";
			}

			std::vector<std::shared_ptr<Mesh>> sorted_meshes;
			std::vector<float> thresholds;
			sorted_meshes.reserve(order.size());
			thresholds.reserve(order.size());
			const int last = static_cast<int>(order.size()) - 1;
			for (size_t i = 0; i < order.size(); ++i) {
				sorted_meshes.push_back(kv.second.meshes[order[i]]);
				thresholds.push_back(last > 0 ? (1.0f - static_cast<float>(i) / static_cast<float>(last)) : 0.0f);
			}
						// Attach the chain to the highest-detail mesh (LOD 0).
			// The chain lives on the Mesh itself -- any MeshRender
			// referencing this mesh automatically sees the new LODs
			// without further wiring. Skip if this mesh is already
			// part of a chain.
			auto lod0 = sorted_meshes.front();
			if (lod0->GetLodCount() <= 1 && sorted_meshes.size() > 1) {
				std::vector<std::shared_ptr<Mesh>> extra_meshes(
					sorted_meshes.begin() + 1, sorted_meshes.end());
				std::vector<float> extra_thresholds(
					thresholds.begin() + 1, thresholds.end());
				lod0->SetLodMeshes(extra_meshes, extra_thresholds);
			}
			FURYI << "gltf-importer: name-suffix LOD group '" << kv.first
				  << "' has " << sorted_meshes.size() << " entries; attached to mesh '"
				  << lod0->GetName() << "'";
		}
	}

	// Kraut trees (glb with asset.extras.kraut): thresholds from extras,
	// billboard terminal tier, foliage material flags, meters -> cm.
	KrautImportPostprocess(model, scene, meshes, materials,
		submesh_to_gltf_material, gltf_node_to_scene_node);

	// Self-register the imported hierarchy with the scene's scene
	// manager so the returned scene renders as-is (Scene.SetActive +
	// Execute) without a MergeInto round-trip -- SceneNode::AddChild
	// does NOT register. MergeInto stays safe: it removes nodes from
	// this tree before re-registering them into the target's.
	scene->GetSceneManager()->AddSceneNodeRecursively(scene->GetRootNode());

	return scene;
}
} // namespace fury
