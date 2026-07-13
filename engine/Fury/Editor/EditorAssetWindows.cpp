#include "Fury/Editor/EditorAssetWindows.h"

#include "Fury/BoxBounds.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/FileUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Joint.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshSimplifier.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/ThreadUtil.h"
#include "Fury/Uniform.h"
#include "Fury/Vector4.h"
#include "Fury/Editor/Editor.h"
#include "ImGui/imgui.h"
#include "ImGuizmo.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Defined in EditorNodeProperties.cpp (lifted out of the file-local
// anonymous namespace so this TU can call it). Renders a 48×48
// material texture row with a Browse… button.
namespace fury {
namespace Editor {
void RenderMaterialTextureRow(Material* mat, const std::string& key);
}
} // namespace fury

namespace fury {
namespace Editor {
namespace {
// Open editor windows, keyed by popup ID ("MeshEditor:<name>"
// / "MaterialEditor:<name>"). Inserting opens the window next
// frame; closing the window (via the close button or setting
// the open bool to false) erases the entry.
std::unordered_set<std::string> g_OpenMeshEditors;
std::unordered_set<std::string> g_OpenMaterialEditors;

// Per-editor camera state. Keyed by popup ID so each open
// mesh editor has its own camera.
struct OrbitState {
	Vector4 target{0, 0, 0, 1};	// orbit center (mesh AABB center)
	float distance = 0.0f;			// current orbit radius
	float initialDistance = 0.0f;	// initial radius (zoom clamp)
	float yaw = 30.0f * 0.0174532925f;	 // initial orbit azimuth (rad)
	float pitch = 20.0f * 0.0174532925f; // initial orbit elevation (rad)
	int preview_lod_override = -1;	// -1 = Auto (runtime-driven); [0, GetLodCount()) = override
	std::shared_ptr<Mesh> framed_mesh;	// the mesh the orbit is currently framed on
	bool initialized = false;
};
std::unordered_map<std::string, OrbitState> g_OrbitState;

OrbitState& OrbitFor(const std::string& popup_id) {
	auto it = g_OrbitState.find(popup_id);
	if (it == g_OrbitState.end())
		it = g_OrbitState.emplace(popup_id, OrbitState{}).first;
	return it->second;
}

// ---- Mesh thumbnail cache ----
// Per-mesh FBO keyed on BufferId. `contentHash` invalidates the FBO when the
// mesh's vertex data changes; `diskLoaded` tracks whether the FBO has been
// populated from the disk cache this session.
struct ThumbnailCacheEntry {
	GLuint fbo = 0;
	std::shared_ptr<Texture> colorRT;
	std::shared_ptr<Texture> depthRT;
	size_t bufferId = 0;			// the BufferId the RT was rendered with
	unsigned int contentHash = 0;	// 0 = not yet hashed
	bool hashInFlight = false;		// an async hash is in flight
	bool wasDirtyLastFrame = false; // for dirty-transition detection
	bool diskLoaded = false;		// FBO has been populated from disk this session
};
std::unordered_map<size_t /*BufferId*/, ThumbnailCacheEntry> g_MeshThumbnails;

// ---- Thumbnail disk cache (mesh-thumbnail-disk-cache) ----
// Persistent PNG cache under Resource/.thumbcache/. Keyed by a
// 64-bit content hash (see MeshContentHash). The in-memory
// `g_DiskCacheIndex` lets IsCached answer in O(1) without a
// filesystem syscall per mesh per frame; the index is
// populated lazily by WarmDiskCacheIndex at editor startup and
// appended to on every successful write.
//
// Files are named `furye_<hex>.png` where `<hex>` is the 16-
// character lowercase hex form of the content hash.
namespace
{
	std::unordered_set<std::string> g_DiskCacheIndex;
	std::string g_CacheDirAbs;		// resolved once on first use

	// Resolve (and lazily create) the on-disk cache directory; cached on first call.
	std::string GetCacheDir()
	{
		if (!g_CacheDirAbs.empty()) return g_CacheDirAbs;
		std::error_code ec;
		std::string dir = FileUtil::GetAbsPath("Resource/.thumbcache/");
		std::filesystem::create_directories(dir, ec);
		// create_directories sets ec on failure but the path
		// itself is still resolvable; the editor's IsCached
		// returns false for anything in that case and writes
		// silently fail (we just don't add to the index).
		g_CacheDirAbs = dir;
		return g_CacheDirAbs;
	}

	// Build the absolute path for a given content hash.
	std::string GetCachePath(unsigned int hash)
	{
		return GetCacheDir() + "furye_" + FormatHashHex(hash) + ".png";
	}

	// O(1) cache-hit check against the in-memory index.
	bool IsCached(unsigned int hash)
	{
		if (hash == 0) return false;
		const std::string filename = "furye_" + FormatHashHex(hash) + ".png";
		return g_DiskCacheIndex.count(filename) > 0;
	}

	// Insert a filename into the index after a successful write.
	// Idempotent; called from the PNG-encode worker callback
	// (which always runs on the main thread via
	// ThreadUtil::Update).
	void IndexCacheFile(const std::string& filename)
	{
		g_DiskCacheIndex.insert(filename);
	}

	// Walk the cache directory once and add every furye_*.png
	// filename to the in-memory index. Wrapped in try/catch so
	// a missing directory is not an error — it just means the
	// index starts empty and warms as the user opens scenes.
	void WarmDiskCacheIndexImpl()
	{
		const std::string dir = GetCacheDir();
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec)) return;
		try
		{
			for (auto it = std::filesystem::directory_iterator(dir, ec);
				 !ec && it != std::filesystem::end(it);
				 it.increment(ec))
			{
				const auto& p = it->path();
				if (!p.has_filename()) continue;
				const std::string fname = p.filename().string();
				// Only count files that look like our cache so a
				// user dropping a manual file in the folder
				// doesn't pollute the index.
				if (fname.rfind("furye_", 0) == 0 &&
					fname.size() > 9 &&
					fname.substr(fname.size() - 4) == ".png")
				{
					g_DiskCacheIndex.insert(fname);
				}
			}
		}
		catch (...)
		{
			// Defensive: an inaccessible directory must not
			// crash the editor. The index just stays empty.
		}
	}

	// Encode + write a PNG off the main thread. We capture the
	// pixel buffer by value (move) so the main thread can drop
	// its copy the moment the worker is enqueued. Returns
	// nothing on success; on failure, the worker emits a
	// rate-limited FURYW and the in-memory FBO is left as the
	// source of truth for the thumbnail.
	void WritePngAsync(const std::string& path,
					   std::vector<unsigned char> pixels,
					   int w, int h, std::string mesh_name)
	{
		if (pixels.empty() || w <= 0 || h <= 0) return;
		ThreadUtil::Instance()->Enqueue<void>(
			[path, pixels = std::move(pixels), w, h, mesh_name](int&) -> std::shared_ptr<void>
			{
				const int row_stride = w * 4;
				const int rc = stbi_write_png(path.c_str(), w, h, 4,
											  pixels.data(), row_stride);
				if (rc == 0)
				{
					// Single rate-limited log; the index isn't
					// updated, so subsequent sessions will
					// re-attempt the write.
					static std::unordered_set<std::string> g_Logged;
					if (g_Logged.insert(mesh_name + "|" + path).second)
					{
						FURYW << "Mesh thumbnail: failed to write "
							  << path << " for mesh '" << mesh_name << "'";
					}
				}
				return nullptr;
			},
			[path](std::shared_ptr<void>)
			{
				// On success, add the filename to the in-memory
				// index so the next IsCached query hits. Even on
				// failure this is safe — the index lookup is
				// advisory (we re-stat the directory at startup
				// for the authoritative answer).
				if (!path.empty())
				{
					auto p = std::filesystem::path(path);
					if (p.has_filename())
						IndexCacheFile(p.filename().string());
				}
			});
	}
} // namespace ThumbnailDiskCache helpers

// Render `mesh` into the 128×128 thumbnail FBO. Allocates the FBO + RTs on
// first call; reuses them after. Camera + shader live in RenderMeshLambert
// (shared with the `fury render-mesh` CLI).
void RenderMeshToThumbnail(const std::shared_ptr<Mesh>& mesh,
						   ThumbnailCacheEntry& entry) {
	if (!mesh) return;

	if (entry.fbo == 0)
	{
		glGenFramebuffers(1, &entry.fbo);
		entry.colorRT = Texture::GetTemporary(128, 128, 1,
			TextureFormat::RGBA8, TextureType::TEXTURE_2D);
		entry.depthRT = Texture::GetTemporary(128, 128, 1,
			TextureFormat::DEPTH24, TextureType::TEXTURE_2D);
		glBindFramebuffer(GL_FRAMEBUFFER, entry.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, entry.colorRT->GetID(), 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_TEXTURE_2D, entry.depthRT->GetID(), 0);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			FURYW << "RenderMeshToThumbnail: FBO incomplete (0x"
				  << std::hex << status << std::dec << ") for mesh '"
				  << mesh->GetName() << "'";
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			return;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, entry.fbo);
	glViewport(0, 0, 128, 128);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	RenderMeshLambert(mesh, 128, 128);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_DEPTH_TEST);
}

// ---- Material editor body (task 6.3) ----
void RenderMaterialEditorBody(const std::shared_ptr<Material>& mat) {
	if (!mat) return;

	// Header: name (read-only), opaque checkbox, texture_flags hex.
	ImGui::TextDisabled("Name: %s", mat->GetName().c_str());
	bool opaque = mat->GetOpaque();
	if (ImGui::Checkbox("Opaque", &opaque))
		mat->SetOpaque(opaque);
	ImGui::SameLine();
	ImGui::Text("Texture Flags: 0x%08X", mat->GetTextureFlags());
	ImGui::Separator();

	// Textures.
	ImGui::TextDisabled("Textures:");
	std::vector<std::string> keys;
	auto add = [&](const std::string& k) {
		if (std::find(keys.begin(), keys.end(), k) == keys.end())
			keys.push_back(k);
	};
	add(Material::DIFFUSE_TEXTURE);
	add(Material::SPECULAR_TEXTURE);
	add(Material::NORMAL_TEXTURE);
	for (auto& pair : mat->GetTextures()) add(pair.first);

	for (auto& key : keys) {
		ImGui::PushID(key.c_str());
		RenderMaterialTextureRow(mat.get(), key);
		ImGui::PopID();
	}

	ImGui::Separator();

	// Uniforms.
	ImGui::TextDisabled("Uniforms:");
	static const std::unordered_set<std::string> kColorUniforms = {
		Material::AMBIENT_COLOR, Material::DIFFUSE_COLOR,
		Material::SPECULAR_COLOR, Material::EMISSIVE_COLOR};
	static const std::unordered_set<std::string> kFactorUniforms = {
		Material::SHININESS, Material::TRANSPARENCY,
		Material::AMBIENT_FACTOR, Material::DIFFUSE_FACTOR,
		Material::SPECULAR_FACTOR, Material::EMISSIVE_FACTOR};
	// Fixed slider width for all uniform editors.
	constexpr float kUniformSliderW = 180.0f;

	for (auto& pair : mat->GetUniforms()) {
		const std::string& uname = pair.first;
		auto& u = pair.second;
		ImGui::PushID(uname.c_str());

		if (kColorUniforms.count(uname)) {
			auto col4 = std::dynamic_pointer_cast<Uniform4f>(u);
			if (col4) {
				float c[4] = {
					col4->GetDataAt(0), col4->GetDataAt(1),
					col4->GetDataAt(2), col4->GetDataAt(3)};
				ImGui::PushItemWidth(kUniformSliderW);
				if (ImGui::ColorEdit4(uname.c_str(), c)) {
					col4->SetData({c[0], c[1], c[2], c[3]});
				}
				ImGui::PopItemWidth();
			} else {
				ImGui::TextDisabled("%s (non-color uniform)", uname.c_str());
			}
		} else if (kFactorUniforms.count(uname)) {
			auto f1 = std::dynamic_pointer_cast<Uniform1f>(u);
			if (f1) {
				float v = f1->GetDataAt(0);
				ImGui::PushItemWidth(kUniformSliderW);
				if (ImGui::DragFloat(uname.c_str(), &v, 0.01f))
					f1->SetData({v});
				ImGui::PopItemWidth();
			} else {
				ImGui::TextDisabled("%s", uname.c_str());
			}
		} else if (u) {
			auto ti = u->GetTypeIndex();
			if (ti == typeid(Uniform1f)) {
				auto uu = std::dynamic_pointer_cast<Uniform1f>(u);
				float v = uu->GetDataAt(0);
				ImGui::PushItemWidth(kUniformSliderW);
				if (ImGui::DragFloat(uname.c_str(), &v, 0.01f))
					uu->SetData({v});
				ImGui::PopItemWidth();
			} else if (ti == typeid(Uniform2f)) {
				auto uu = std::dynamic_pointer_cast<Uniform2f>(u);
				float v[2] = {uu->GetDataAt(0), uu->GetDataAt(1)};
				ImGui::PushItemWidth(kUniformSliderW);
				if (ImGui::DragFloat2(uname.c_str(), v, 0.01f))
					uu->SetData({v[0], v[1]});
				ImGui::PopItemWidth();
			} else if (ti == typeid(Uniform3f)) {
				auto uu = std::dynamic_pointer_cast<Uniform3f>(u);
				float v[3] = {uu->GetDataAt(0), uu->GetDataAt(1), uu->GetDataAt(2)};
				ImGui::PushItemWidth(kUniformSliderW);
				if (ImGui::DragFloat3(uname.c_str(), v, 0.01f))
					uu->SetData({v[0], v[1], v[2]});
				ImGui::PopItemWidth();
			} else if (ti == typeid(Uniform4f)) {
				auto uu = std::dynamic_pointer_cast<Uniform4f>(u);
				float v[4] = {uu->GetDataAt(0), uu->GetDataAt(1),
							  uu->GetDataAt(2), uu->GetDataAt(3)};
				ImGui::PushItemWidth(kUniformSliderW);
				if (ImGui::DragFloat4(uname.c_str(), v, 0.01f))
					uu->SetData({v[0], v[1], v[2], v[3]});
				ImGui::PopItemWidth();
			} else if (ti == typeid(Uniform1i)) {
				auto uu = std::dynamic_pointer_cast<Uniform1i>(u);
				int v = uu->GetDataAt(0);
				ImGui::PushItemWidth(kUniformSliderW);
				if (ImGui::DragInt(uname.c_str(), &v))
					uu->SetData({v});
				ImGui::PopItemWidth();
			} else if (ti == typeid(UniformMatrix4fv)) {
				ImGui::TextDisabled("%s (mat4)", uname.c_str());
			} else {
				ImGui::TextDisabled("%s", uname.c_str());
			}
		} else {
			ImGui::TextDisabled("%s (null)", uname.c_str());
		}

		ImGui::PopID();
	}

	ImGui::Separator();

	// Shader passes: read-only list, omitting empty slots.
	ImGui::TextDisabled("Shader Passes:");
	for (unsigned int i = 0; i < 8; ++i) {
		auto sh = mat->GetShaderForPass(i);
		if (!sh) continue;
		ImGui::Text("Pass %u: %s", i, sh->GetName().c_str());
	}
}

// ---- Mesh editor body: metadata (task 6.4) ----
//
// `popup_id` ties per-window state (LOD selection) to a particular
// mesh editor instance. The LOD dropdown is shown only when at
// least one MeshRender referencing `mesh` has a LodGroup — the
// first such group is shown (we don't merge multiple groups). The
// per-window selection lives in a static keyed by popup_id so
// closing and re-opening the window resets to LOD 0.
void RenderMeshMetadata(const std::shared_ptr<Mesh>& mesh,
						const std::string& popup_id) {
	if (!mesh) return;

	ImGui::TextDisabled("Name: %s", mesh->GetName().c_str());

	// Find the first MeshRender in the active scene that references
	// this mesh and has a non-empty LodGroup.
	// The LOD chain lives on the mesh itself (single-asset
	// model). Use the mesh's chain directly for the preview /
	// dropdown / stats.
	const unsigned int lod_count = mesh->GetLodCount();

	// Per-window LOD preview override (-1 = Auto, [0, lod_count) = specific LOD).
	OrbitState& os = OrbitFor(popup_id);
	if (os.preview_lod_override != -1 &&
		static_cast<unsigned int>(os.preview_lod_override) >= lod_count)
	{
		os.preview_lod_override = -1;
	}
	int selected_lod = os.preview_lod_override;

	if (lod_count > 1)
	{
		ImGui::Text("LOD:");
		ImGui::SameLine();
		ImGui::PushItemWidth(120.0f);
		std::string label;
		if (selected_lod < 0)
			label = "Auto";
		else
			label = "LOD " + std::to_string(selected_lod);
		if (ImGui::BeginCombo("##lod_dropdown", label.c_str()))
		{
			bool auto_selected = (selected_lod < 0);
			if (ImGui::Selectable("Auto", auto_selected))
				os.preview_lod_override = -1;
			if (auto_selected) ImGui::SetItemDefaultFocus();
			for (unsigned int i = 0; i < lod_count; ++i)
			{
				bool is_selected = (static_cast<int>(i) == selected_lod);
				char entry[32];
				std::snprintf(entry, sizeof(entry), "LOD %u", i);
				if (ImGui::Selectable(entry, is_selected))
					os.preview_lod_override = static_cast<int>(i);
				if (is_selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
	}
	else
	{
		ImGui::TextDisabled("LOD: (no LOD chain)");
	}

	// Editable interior LOD thresholds (LOD 0 = 1.0, LOD N = 0.0, locked).
	if (lod_count > 1)
	{
		auto get_th = [&](unsigned int i) -> float {
			return mesh->GetLodThreshold(i);
		};
		auto set_th = [&](unsigned int i, float v) {
			auto cur = mesh->GetLodMeshes();
			std::vector<float> new_th;
			new_th.reserve(cur.size());
			for (size_t k = 0; k < cur.size(); ++k)
				new_th.push_back(mesh->GetLodThreshold(static_cast<unsigned int>(k) + 1));
			// Clamp to [0, 1] so a stray typed value doesn't poison the chain.
			if (v < 0.0f) v = 0.0f;
			if (v > 1.0f) v = 1.0f;
			new_th[i - 1] = v;
			mesh->SetLodMeshes(cur, new_th);
		};
		std::string table_id = "lod_thresholds##" + popup_id;
		if (ImGui::BeginTable(table_id.c_str(), 2,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Index");
			ImGui::TableSetupColumn("Threshold");
			ImGui::TableHeadersRow();
			for (unsigned int i = 1; i < lod_count; ++i)
			{
				ImGui::TableNextRow();
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextColumn();
				ImGui::Text("LOD %u", i);
				ImGui::TableNextColumn();
				const bool is_last = (i + 1 == lod_count);
				if (is_last)
				{
					ImGui::TextDisabled("0.000");
				}
				else
				{
					float t = get_th(i);
					ImGui::PushItemWidth(120.0f);
					if (ImGui::InputFloat("##th", &t, 0.0f, 0.0f, "%.3f"))
						set_th(i, t);
					ImGui::PopItemWidth();
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	// "Generate LODs..." button — opens a modal that drives the
	// meshopt_simplify wrapper. Available whether or not a chain
	// already exists (lets the user re-generate).
	if (ImGui::Button("Generate LODs..."))
	{
		ImGui::OpenPopup("GenerateLODsModal");
	}

	// Modal body. Per-window option state keyed by popup_id so
	// the dialog remembers values between opens.
	{
		struct LODOpts {
			int total_levels = 3;
			float ratio = 0.5f;
			float error = 0.5f;
			// 0 = Quadric (default, border-preserving),
			// 1 = Sloppy (aggressive), 2 = QuadricLegacy.
			int method = 0;
		};
		static std::unordered_map<std::string, LODOpts> g_LODOpts;
		LODOpts &opts = g_LODOpts[popup_id];

		ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);
		if (ImGui::BeginPopupModal("GenerateLODsModal", nullptr,
								   ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Source mesh: %s", mesh->GetName().c_str());
			ImGui::Separator();
			// Total levels = LOD0 (source) + N-1 generated. Min 2
			// (LOD0 + LOD1), max 5 (LOD0..LOD4).
			ImGui::DragInt("Total levels", &opts.total_levels, 1.0f, 2, 5);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Total LOD levels including the source.\nLOD0 = this mesh (highest detail).\nLOD1..LOD(N-1) are generated.");
			ImGui::DragFloat("Reduction ratio", &opts.ratio, 0.05f, 0.05f, 0.95f, "%.2f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Per-level reduction factor.\ne.g. 0.5 = each level halves the triangle count of the previous.\nLower = more aggressive (fewer triangles per LOD).");
			ImGui::DragFloat("Target error", &opts.error, 0.001f, 0.0f, 1.0f, "%.3f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("meshopt_simplifySloppy error tolerance (in mesh units).\nLarger = more aggressive reduction with more geometric drift.\nDefault 0.5 = coarse-grid LODs (typical production).\n0.001 = nearly lossless.");
			// Simplification method. The labels match the three
			// MeshSimplifyOptions::Method values; the dropdown is
			// indexed by position so it round-trips through the
			// per-popup g_LODOpts map.
			static const char *method_labels[] = {
				"Quadric (preserve borders)",
				"Sloppy (aggressive)",
				"Quadric (legacy)"
			};
			ImGui::Combo("Method", &opts.method, method_labels,
						 IM_ARRAYSIZE(method_labels));
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Simplification algorithm.\nQuadric (default) — preserves UV seams / borders.\nSloppy — grid-based, ignores borders (most aggressive).\nQuadric (legacy) — quadric without border locking.");
			ImGui::Separator();

			if (ImGui::Button("Generate"))
			{
				MeshSimplifyOptions simp;
				simp.lod_count = std::max(0, opts.total_levels - 1);
				simp.reduction_ratio = opts.ratio;
				simp.target_error = opts.error;
				switch (opts.method)
				{
					case 0: simp.method = MeshSimplifyOptions::Method::Quadric; break;
					case 1: simp.method = MeshSimplifyOptions::Method::Sloppy; break;
					case 2: simp.method = MeshSimplifyOptions::Method::QuadricLegacy; break;
					default: simp.method = MeshSimplifyOptions::Method::Quadric; break;
				}
				auto result = SimplifyMesh(mesh, simp);

				if (!result.lod_meshes.empty())
				{
					// Attach the generated LODs to the source
					// mesh's own chain. The mesh is the single
					// asset; any MeshRender referencing it
					// automatically sees the new LODs.
					mesh->SetLodMeshes(result.lod_meshes, result.thresholds);
					FURYI << "Generate LODs: attached " << result.lod_meshes.size()
						  << " LOD mesh(es) to '" << mesh->GetName()
						  << "' (a single loded mesh asset)";
					Editor::MarkSceneDirty();
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	// Per-LOD or single-mesh stats. When the mesh has a chain and
	// an LOD is picked, show stats for the selected LOD's mesh.
	std::shared_ptr<Mesh> stats_mesh = mesh;
	if (os.preview_lod_override >= 0 &&
		static_cast<unsigned int>(os.preview_lod_override) < lod_count)
	{
		auto picked = mesh->GetLodMesh(
			static_cast<unsigned int>(os.preview_lod_override));
		if (picked) stats_mesh = picked;
	}

	unsigned int totalVerts =
		static_cast<unsigned int>(stats_mesh->Positions.Data.size() / 3);
	unsigned int totalIndices = 0;
	unsigned int totalTris = 0;
	for (unsigned int i = 0; i < stats_mesh->GetSubMeshCount(); ++i) {
		auto sm = stats_mesh->GetSubMeshAt(i);
		if (sm) {
			totalIndices += static_cast<unsigned int>(sm->Indices.Data.size());
			totalTris += static_cast<unsigned int>(sm->Indices.Data.size()) / 3;
		}
	}
	ImGui::Text("Vertices: %u", totalVerts);
	ImGui::Text("Indices: %u", totalIndices);
	ImGui::Text("Triangles: %u", totalTris);
	ImGui::Text("Submeshes: %u", stats_mesh->GetSubMeshCount());

	if (stats_mesh->GetSubMeshCount() > 0) {
		std::string table_id = "submeshes##" + popup_id;
		if (ImGui::BeginTable(table_id.c_str(), 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Index");
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Indices");
			ImGui::TableSetupColumn("Triangles");
			ImGui::TableHeadersRow();
			for (unsigned int i = 0; i < stats_mesh->GetSubMeshCount(); ++i) {
				auto sm = stats_mesh->GetSubMeshAt(i);
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%u", i);
				ImGui::TableNextColumn();
				if (sm) {
					const std::string name = "Submesh " + std::to_string(i);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%u", static_cast<unsigned int>(sm->Indices.Data.size()));
					ImGui::TableNextColumn();
					ImGui::Text("%u", static_cast<unsigned int>(sm->Indices.Data.size()) / 3);
				} else {
					ImGui::TextUnformatted("<null>");
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("-");
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("-");
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	auto aabb = mesh->GetAABB();
	auto mn = aabb.GetMin();
	auto mx = aabb.GetMax();
	ImGui::Text("AABB Min: (%.2f, %.2f, %.2f)", mn.x, mn.y, mn.z);
	ImGui::Text("AABB Max: (%.2f, %.2f, %.2f)", mx.x, mx.y, mx.z);
	ImGui::Text("AABB Size: (%.2f, %.2f, %.2f)",
				mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
	ImGui::Text("AABB Center: (%.2f, %.2f, %.2f)",
				(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);

	bool cast = mesh->GetCastShadows();
	if (ImGui::Checkbox("Cast Shadows", &cast))
		mesh->SetCastShadows(cast);

	if (mesh->IsSkinnedMesh()) {
		auto root = mesh->GetRootJoint();
		ImGui::Text("Root Joint: %s",
					(root ? root->GetName().c_str() : "(none)"));
		ImGui::Text("Joints: %u", mesh->GetJointCount());
	}
}

// ---- Mesh editor body: 3D preview ----
struct PreviewRT {
	GLuint fbo = 0;
	std::shared_ptr<Texture> colorRT;
	std::shared_ptr<Texture> depthRT;
	int width = 0;
	int height = 0;
};
std::unordered_map<std::string, PreviewRT> g_PreviewRTs;

// 3D preview of the mesh. Renders into a per-popup PreviewRT and presents via
// ImGui::Image. The mesh is placed at world origin; the orbit camera frames on
// the mesh's local AABB so any mesh renders at any scale. LMB = orbit,
// wheel = zoom, RMB = pan.
void RenderMeshPreview(const std::shared_ptr<Mesh>& mesh,
					   const std::string& popup_id, const ImVec2& size,
					   const std::shared_ptr<Mesh>& display_mesh) {
	const std::shared_ptr<Mesh>& render_mesh =
		(display_mesh && display_mesh->GetSubMeshCount() >= 0) ? display_mesh : mesh;
	if (!render_mesh) {
		ImGui::BeginChild("preview", size, true,
						  ImGuiWindowFlags_NoScrollbar);
		ImGui::GetWindowDrawList()->AddRectFilled(
			ImGui::GetCursorScreenPos(),
			ImVec2(ImGui::GetCursorScreenPos().x + size.x,
				   ImGui::GetCursorScreenPos().y + size.y),
			ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 1.0f)));
		ImGui::TextDisabled("(3D preview - no mesh)");
		ImGui::EndChild();
		return;
	}

	const ImVec2 pad(8.0f, 8.0f);
	const int w = std::max(32,
		static_cast<int>(size.x - 2.0f * pad.x + 0.5f));
	const int h = std::max(32,
		static_cast<int>(size.y - 2.0f * pad.y + 0.5f));
	const float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.0f;

	// (Re)allocate the PreviewRT on resize.
	PreviewRT& rt = g_PreviewRTs[popup_id];
	const bool rt_size_changed = (rt.fbo != 0 &&
		(rt.width != w || rt.height != h));
	if (rt.fbo == 0 || rt.width != w || rt.height != h) {
		if (rt.fbo) {
			glDeleteFramebuffers(1, &rt.fbo);
			rt.fbo = 0;
		}
		glGenFramebuffers(1, &rt.fbo);
		rt.colorRT = Texture::GetTemporary(w, h, 1,
			TextureFormat::RGBA8, TextureType::TEXTURE_2D);
		rt.depthRT = Texture::GetTemporary(w, h, 1,
			TextureFormat::DEPTH24, TextureType::TEXTURE_2D);
		glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, rt.colorRT->GetID(), 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_TEXTURE_2D, rt.depthRT->GetID(), 0);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			FURYW << "RenderMeshPreview: FBO incomplete for popup_id "
				  << popup_id << " (0x" << std::hex << status << std::dec << ")";
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			rt.fbo = 0;
			return;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		rt.width = w;
		rt.height = h;
	}

	// Frame on the mesh's local AABB (mesh placed at world origin).
	auto aabb = render_mesh->GetAABB();
	auto mn = aabb.GetMin();
	auto mx = aabb.GetMax();
	Vector4 aabb_center((mn.x + mx.x) * 0.5f,
						(mn.y + mx.y) * 0.5f,
						(mn.z + mx.z) * 0.5f, 1.0f);
	Vector4 aabb_size(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 0);
	float radius = 0.5f * std::sqrt(
		aabb_size.x * aabb_size.x + aabb_size.y * aabb_size.y + aabb_size.z * aabb_size.z);
	// Degenerate AABB fallback; tiny-but-valid meshes keep their real radius.
	if (radius < 1e-6f) radius = 0.5f;

	OrbitState& os = OrbitFor(popup_id);
	// Re-frame on first appearance, on FBO resize (aspect changes), or
	// when the displayed mesh switches (e.g. user picks a different LOD
	// in the dropdown — each LOD has its own AABB and the previous
	// orbit's target / distance would frame the wrong geometry).
	const bool mesh_changed = (os.framed_mesh.get() != render_mesh.get());
	if (!os.initialized || rt_size_changed || mesh_changed) {
		const float fov0 = 45.0f * 0.0174532925f;
		const float adjusted_dist = (radius * 0.6f) /
			(std::tan(fov0 * 0.5f) * std::min(1.0f, aspect));
		// Preserve the user's manual zoom fraction across resizes / mesh
		// switches (only meaningful if we'd already framed the same mesh).
		const float zoom_factor = (os.initialized && !mesh_changed)
			? (os.distance / std::max(1e-6f, os.initialDistance))
			: 1.0f;
		os.initialDistance = adjusted_dist;
		os.distance = adjusted_dist * zoom_factor;
		os.target = aabb_center;
		os.framed_mesh = render_mesh;
		os.initialized = true;
	}

	const float fov = 45.0f * 0.0174532925f;
	Matrix4 proj;
	// Near/far sized to the bounding sphere so any scale renders without clipping.
	proj.PerspectiveFov(fov, aspect,
		std::max(radius * 0.05f, 1e-5f), os.distance + radius * 5.0f);

	const float cx = std::cos(os.pitch) * std::cos(os.yaw);
	const float cy = std::sin(os.pitch);
	const float cz = std::cos(os.pitch) * std::sin(os.yaw);
	const Vector4 eye(os.target.x + cx * os.distance,
		os.target.y + cy * os.distance,
		os.target.z + cz * os.distance, 1.0f);
	Matrix4 view;
	view.LookAt(eye, os.target, Vector4(0, 1, 0, 0));

	if (render_mesh->GetDirty())
		render_mesh->UpdateBuffer();

	glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
	glViewport(0, 0, w, h);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	auto shader = GetSimpleLambertShader();
	shader->Bind();
	Matrix4 world;
	world.Identity();
	shader->BindMatrix("_WorldMatrix", world);
	shader->BindMatrix("_ViewMatrix", view);
	shader->BindMatrix("_ProjectionMatrix", proj);

	auto submeshCount = render_mesh->GetSubMeshCount();
	if (submeshCount == 0) {
		shader->BindMesh(render_mesh);
		glDrawElements(GL_TRIANGLES,
					   static_cast<GLsizei>(render_mesh->Indices.Data.size()),
					   GL_UNSIGNED_INT, 0);
	} else {
		shader->BindMesh(render_mesh);
		for (unsigned int i = 0; i < submeshCount; ++i) {
			auto sm = render_mesh->GetSubMeshAt(i);
			if (!sm) continue;
			shader->BindSubMesh(render_mesh, i);
			glDrawElements(GL_TRIANGLES,
						   static_cast<GLsizei>(sm->Indices.Data.size()),
						   GL_UNSIGNED_INT, 0);
		}
	}

	// Ground grid + AABB wireframe.
	{
		static std::shared_ptr<Shader> line_shader;
		if (!line_shader) {
			line_shader = Shader::Create("EditorLineShader",
				ShaderType::OTHER);
			const char* vs =
				"in vec3 vertex_position;"
				"uniform mat4 _ViewMatrix;"
				"uniform mat4 _ProjectionMatrix;"
				"uniform mat4 _OffsetMat;"
				"void main()"
				"{"
				"	gl_Position = _ProjectionMatrix * _ViewMatrix *"
				"		_OffsetMat * vec4(vertex_position, 1.0);"
				"}";
			const char* fs =
				"uniform vec4 _Color;"
				"out vec4 fragment_output;"
				"void main() { fragment_output = _Color; }";
			line_shader->Compile(vs, fs, "");
		}
		// Grid scales with the mesh's bounding radius (no fixed floor).
		const float grid_extent = radius * 2.0f;
		const float grid_step = grid_extent / 5.0f;
		const int grid_lines_per_axis =
			static_cast<int>((2.0f * grid_extent) / grid_step) + 1;
		const int grid_vertex_count = grid_lines_per_axis * 4;
		static std::vector<float> grid_vbo_data;
		static GLuint grid_vbo = 0, grid_vao = 0;
		static float cached_extent = -1.0f;
		if (grid_vbo == 0 || cached_extent != grid_extent) {
			grid_vbo_data.clear();
			grid_vbo_data.reserve(grid_vertex_count * 3);
			for (float x = -grid_extent; x <= grid_extent + 1e-4f; x += grid_step) {
				grid_vbo_data.push_back(x); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back(-grid_extent);
				grid_vbo_data.push_back(x); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back( grid_extent);
			}
			for (float z = -grid_extent; z <= grid_extent + 1e-4f; z += grid_step) {
				grid_vbo_data.push_back(-grid_extent); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back(z);
				grid_vbo_data.push_back( grid_extent); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back(z);
			}
			if (grid_vbo == 0) {
				glGenBuffers(1, &grid_vbo);
				glGenVertexArrays(1, &grid_vao);
				glBindVertexArray(grid_vao);
				glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
				glEnableVertexAttribArray(0);
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
				glBindVertexArray(0);
			}
			glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
			glBufferData(GL_ARRAY_BUFFER, grid_vbo_data.size() * sizeof(float),
						 grid_vbo_data.data(), GL_DYNAMIC_DRAW);
			cached_extent = grid_extent;
		}
		// AABB wireframe (8 corners, 12 edges), rebuilt per frame.
		const float aabbVerts[24 * 3] = {
			mn.x, mn.y, mn.z,  mx.x, mn.y, mn.z,
			mx.x, mn.y, mn.z,  mx.x, mn.y, mx.z,
			mx.x, mn.y, mx.z,  mn.x, mn.y, mx.z,
			mn.x, mn.y, mx.z,  mn.x, mn.y, mn.z,
			mn.x, mx.y, mn.z,  mx.x, mx.y, mn.z,
			mx.x, mx.y, mn.z,  mx.x, mx.y, mx.z,
			mx.x, mx.y, mx.z,  mn.x, mx.y, mx.z,
			mn.x, mx.y, mx.z,  mn.x, mx.y, mn.z,
			mn.x, mn.y, mn.z,  mn.x, mx.y, mn.z,
			mx.x, mn.y, mn.z,  mx.x, mx.y, mn.z,
			mx.x, mn.y, mx.z,  mx.x, mx.y, mx.z,
			mn.x, mn.y, mx.z,  mn.x, mx.y, mx.z,
		};
		static GLuint aabb_vbo = 0, aabb_vao = 0;
		if (aabb_vbo == 0) {
			glGenBuffers(1, &aabb_vbo);
			glBindBuffer(GL_ARRAY_BUFFER, aabb_vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(aabbVerts),
						 aabbVerts, GL_DYNAMIC_DRAW);
			glGenVertexArrays(1, &aabb_vao);
			glBindVertexArray(aabb_vao);
			glBindBuffer(GL_ARRAY_BUFFER, aabb_vbo);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
			glBindVertexArray(0);
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, aabb_vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(aabbVerts), aabbVerts);
		}

		line_shader->Bind();
		line_shader->BindMatrix("_ViewMatrix", view);
		line_shader->BindMatrix("_ProjectionMatrix", proj);

		// Grid offset: AABB center XZ, AABB bottom Y.
		Matrix4 gridOffMat;
		gridOffMat.Identity();
		gridOffMat.Raw[12] = aabb_center.x;
		gridOffMat.Raw[13] = mn.y;
		gridOffMat.Raw[14] = aabb_center.z;
		line_shader->BindMatrix("_OffsetMat", gridOffMat);
		glBindVertexArray(grid_vao);
		glLineWidth(1.0f);
		glDrawArrays(GL_LINES, 0, grid_vertex_count);
		glBindVertexArray(0);

		// AABB wireframe (offset = identity; vertices already in world space).
		Matrix4 aabbOffMat;
		aabbOffMat.Identity();
		line_shader->BindMatrix("_OffsetMat", aabbOffMat);
		glBindVertexArray(aabb_vao);
		glLineWidth(1.5f);
		glDrawArrays(GL_LINES, 0, 24);
		glBindVertexArray(0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_DEPTH_TEST);

	ImGui::BeginChild("preview", size, false,
					  ImGuiWindowFlags_NoScrollbar);
	ImGui::SetCursorPos(pad);
	const ImVec2 img_size(size.x - 2.0f * pad.x,
						   size.y - 2.0f * pad.y);
	ImGui::Image((ImTextureID)(intptr_t)rt.colorRT->GetID(),
				 img_size, ImVec2(0, 1), ImVec2(1, 0));

	// Camera input: orbit (LMB), zoom (wheel), pan (RMB). Speeds scale with
	// the mesh's bounding radius so any scale handles the same.
	const bool hovered = ImGui::IsItemHovered() || ImGui::IsWindowHovered();
	if (hovered) {
		const ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			os.yaw   += io.MouseDelta.x * 0.01f;
			os.pitch += io.MouseDelta.y * 0.01f;
			const float lim = static_cast<float>(M_PI_2) - 0.01f;
			if (os.pitch >  lim) os.pitch =  lim;
			if (os.pitch < -lim) os.pitch = -lim;
		}
		if (io.MouseWheel != 0.0f && os.initialDistance > 0.0f) {
			os.distance *= (1.0f - io.MouseWheel * 0.1f);
			os.distance = std::max(0.1f * os.initialDistance,
				std::min(10.0f * os.initialDistance, os.distance));
		}
		// Pan: translate the orbit target along the camera's right/up vectors.
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
			Vector4 forward = eye - os.target;
			float flen = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
			if (flen > 1e-6f) {
				forward.x /= flen; forward.y /= flen; forward.z /= flen;
				Vector4 right(
					forward.y * 0.0f - forward.z * 1.0f,
					forward.z * 0.0f - forward.x * 0.0f,
					forward.x * 1.0f - forward.y * 0.0f, 0.0f);
				float rlen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
				if (rlen > 1e-6f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }
				Vector4 up(
					right.y * forward.z - right.z * forward.y,
					right.z * forward.x - right.x * forward.z,
					right.x * forward.y - right.y * forward.x, 0.0f);
				const float pan_scale = radius * 0.002f;
				os.target.x -= right.x * io.MouseDelta.x * pan_scale;
				os.target.y -= right.y * io.MouseDelta.x * pan_scale;
				os.target.z -= right.z * io.MouseDelta.x * pan_scale;
				os.target.x += up.x * io.MouseDelta.y * pan_scale;
				os.target.y += up.y * io.MouseDelta.y * pan_scale;
				os.target.z += up.z * io.MouseDelta.y * pan_scale;
			}
		}
	}

	ImGui::EndChild();
}
} // namespace

// ---- Mesh editor window ----
void RenderMeshEditorWindow(const std::shared_ptr<Mesh>& mesh, bool* p_open) {
	if (!mesh) return;

	ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->GetCenter().y),
							ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	std::string title = "Mesh: " + mesh->GetName();
	if (!ImGui::Begin(title.c_str(), p_open)) {
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Mesh: %s", mesh->GetName().c_str());
	ImGui::Separator();

	std::string popup_id = "MeshEditor:" + mesh->GetName();

	// Two-pane layout: viewer left (~70%), metadata right (~30%).
	ImVec2 avail = ImGui::GetContentRegionAvail();
	const float sidebar_width = std::max(240.0f, avail.x * 0.30f);
	const float viewer_width = std::max(120.0f, avail.x - sidebar_width - 8.0f);

	// LOD-selected mesh for the preview (keyed by popup_id). -1 = Auto
	// (render whatever the runtime would pick); [0, lod_count) = override.
	std::shared_ptr<Mesh> display_mesh = mesh;
	{
		OrbitState& os = OrbitFor(popup_id);
		const unsigned int lod_count = mesh->GetLodCount();
		if (os.preview_lod_override >= 0 &&
			static_cast<unsigned int>(os.preview_lod_override) < lod_count)
		{
			auto picked = mesh->GetLodMesh(
				static_cast<unsigned int>(os.preview_lod_override));
			if (picked) display_mesh = picked;
		}
	}

	RenderMeshPreview(mesh, popup_id, ImVec2(viewer_width, avail.y), display_mesh);

	ImGui::SameLine();
	ImGui::BeginChild("metadata", ImVec2(sidebar_width, avail.y), true);
	RenderMeshMetadata(mesh, popup_id);
	ImGui::EndChild();

	ImGui::End();
}

void RenderMaterialEditorWindow(const std::shared_ptr<Material>& mat, bool* p_open) {
	if (!mat) return;

	ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_FirstUseEver);
	// Center on first appearance.
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->GetCenter().y),
							ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	std::string title = "Material: " + mat->GetName();
	if (!ImGui::Begin(title.c_str(), p_open)) {
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Material: %s", mat->GetName().c_str());
	ImGui::Separator();

	RenderMaterialEditorBody(mat);

	ImGui::End();
}

void OpenMeshEditor(const std::shared_ptr<Mesh>& mesh) {
	if (!mesh) return;
	std::string popup = "MeshEditor:" + mesh->GetName();
	g_OpenMeshEditors.insert(popup);
}

void OpenMaterialEditor(const std::shared_ptr<Material>& mat) {
	if (!mat) return;
	std::string popup = "MaterialEditor:" + mat->GetName();
	g_OpenMaterialEditors.insert(popup);
}

// Lookup or allocate a 128×128 thumbnail for `mesh`; returns the color RT's GL
// texture ID (0 until populated). Enqueues an async content hash on dirty
// transitions and warms from the disk cache on a hit.
unsigned int GetMeshThumbnail(const std::shared_ptr<Mesh>& mesh) {
	if (!mesh) return 0;
	const size_t bufferId = mesh->GetBufferId();
	auto& entry = g_MeshThumbnails[bufferId];
	entry.bufferId = bufferId;

	// Slow path A: dirty-transition. The mesh was just re-uploaded
	// to the GPU (true→false), so the content may have changed. Enqueue
	// a hash worker; mark in-flight so we don't enqueue duplicates.
	const bool isDirty = mesh->GetDirty();
	if (entry.wasDirtyLastFrame && !isDirty && !entry.hashInFlight) {
		entry.hashInFlight = true;
		// Hash reads only Data vectors, safe off-thread; re-check BufferId
		// in the callback (the mesh may have been replaced under the same id).
		std::weak_ptr<Mesh> weak = mesh;
		ThreadUtil::Instance()->Enqueue<unsigned int>(
			[weak](int&) -> std::shared_ptr<unsigned int>
			{
				auto p = weak.lock();
				if (!p) return std::make_shared<unsigned int>(0);
				return std::make_shared<unsigned int>(MeshContentHash(p.get()));
			},
			[weak, bufferId](std::shared_ptr<unsigned int> result)
			{
				auto p = weak.lock();
				if (!p) return; // mesh was destroyed in flight
				auto it = g_MeshThumbnails.find(bufferId);
				if (it == g_MeshThumbnails.end()) return;
				auto& e = it->second;
				e.hashInFlight = false;
				if (!result) return;
			if (*result != e.contentHash)
			{
				// Content changed: invalidate the FBO so it re-populates.
				FURYD << "Mesh thumbnail: content changed for BufferId "
					  << bufferId << " (" << e.contentHash
					  << " -> " << *result << ")";
				e.contentHash = *result;
				e.diskLoaded = false;
				if (e.fbo) {
					glDeleteFramebuffers(1, &e.fbo);
					e.fbo = 0;
				}
				e.colorRT.reset();
				e.depthRT.reset();
			}
			});
	}

	// Slow path B: first time seeing this mesh — kick off the initial hash.
	if (entry.contentHash == 0 && !entry.hashInFlight) {
		entry.hashInFlight = true;
		std::weak_ptr<Mesh> weak = mesh;
		ThreadUtil::Instance()->Enqueue<unsigned int>(
			[weak](int&) -> std::shared_ptr<unsigned int>
			{
				auto p = weak.lock();
				if (!p) return std::make_shared<unsigned int>(0);
				return std::make_shared<unsigned int>(MeshContentHash(p.get()));
			},
			[weak, bufferId](std::shared_ptr<unsigned int> result)
			{
				auto p = weak.lock();
				if (!p) return;
				auto it = g_MeshThumbnails.find(bufferId);
				if (it == g_MeshThumbnails.end()) return;
				auto& e = it->second;
				e.hashInFlight = false;
				if (!result) return;
			e.contentHash = *result;
			e.diskLoaded = false;
		});
		// No hash yet — return 0 (gray fallback) until the worker completes.
		entry.wasDirtyLastFrame = isDirty;
		return 0;
	}

	// Slow path C: hash is in flight — wait one more frame.
	if (entry.hashInFlight) {
		entry.wasDirtyLastFrame = isDirty;
		return 0;
	}

	// Slow path D: have a hash but haven't populated the FBO this session.
	// On a disk-cache hit, re-render (cheap). On a miss, render + queue a
	// PNG encode.
	if (!entry.diskLoaded && entry.contentHash != 0) {
		if (IsCached(entry.contentHash))
		{
			RenderMeshToThumbnail(mesh, entry);
			FURYD << "Mesh thumbnail: cache hit for BufferId "
				  << bufferId << " (hash "
				  << FormatHashHex(entry.contentHash) << ")";
			entry.diskLoaded = true;
		}
		else
		{
			RenderMeshToThumbnail(mesh, entry);
			std::vector<unsigned char> pixels(128 * 128 * 4);
			glBindFramebuffer(GL_FRAMEBUFFER, entry.fbo);
			glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE,
						 pixels.data());
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			// Flip rows: GL origin is bottom-left, PNG is top-left.
			std::vector<unsigned char> flipped(pixels.size());
			const size_t row_bytes = 128 * 4;
			for (int y = 0; y < 128; ++y) {
				std::memcpy(&flipped[y * row_bytes],
							&pixels[(127 - y) * row_bytes],
							row_bytes);
			}
			const std::string path = GetCachePath(entry.contentHash);
			const std::string name = mesh->GetName();
			WritePngAsync(path, std::move(flipped), 128, 128, name);
			entry.diskLoaded = true;
		}
	}

	// Fast path: return the cached color RT id.
	entry.wasDirtyLastFrame = isDirty;
	if (entry.colorRT && entry.colorRT->GetID() != 0)
		return entry.colorRT->GetID();
	return 0;
}

void EvictStaleMeshThumbnails(const std::unordered_set<size_t>& liveIds) {
	for (auto it = g_MeshThumbnails.begin(); it != g_MeshThumbnails.end();) {
		if (liveIds.count(it->first) == 0) {
			if (it->second.fbo)
				glDeleteFramebuffers(1, &it->second.fbo);
			it->second.colorRT.reset();
			it->second.depthRT.reset();
			it = g_MeshThumbnails.erase(it);
		} else {
			++it;
		}
	}
}

// Periodic refresh poll. Re-runs GetMeshThumbnail for every mesh in the active
// scene (rate-limited by the caller). Scans the EntityManager directly so the
// poll needs no UI state.
void RefreshMeshThumbnailCache() {
	if (!Scene::Active) return;
	auto em = Scene::Active->GetEntityManager();
	if (!em) return;
	std::function<bool(const std::shared_ptr<Mesh>&)> fn =
		[](const std::shared_ptr<Mesh>& mesh) {
			if (!mesh) return true;
			GetMeshThumbnail(mesh);
			return true;
		};
	em->ForEach<Mesh>(fn);
}

// Force a re-hash + re-render of `mesh`'s thumbnail on the next poll.
void RefreshMeshThumbnailNow(const std::shared_ptr<Mesh>& mesh) {
	if (!mesh) return;
	auto it = g_MeshThumbnails.find(mesh->GetBufferId());
	if (it == g_MeshThumbnails.end()) return;
	it->second.contentHash = 0;
	it->second.diskLoaded = false;
	it->second.hashInFlight = false;
	if (it->second.fbo) {
		glDeleteFramebuffers(1, &it->second.fbo);
		it->second.fbo = 0;
	}
	it->second.colorRT.reset();
	it->second.depthRT.reset();
}

void WarmDiskCacheIndex() {
	WarmDiskCacheIndexImpl();
}

void RenderAllOpenAssetEditors() {
	std::vector<std::string> closedMesh;
	for (const auto& popup : g_OpenMeshEditors) {
		bool open = true;
		auto pos = popup.find(':');
		std::string name = (pos != std::string::npos)
							   ? popup.substr(pos + 1)
							   : popup;
		std::shared_ptr<Mesh> mesh;
		if (Scene::Active)
			if (auto em = Scene::Active->GetEntityManager())
				mesh = em->Get<Mesh>(name);
		if (mesh)
			RenderMeshEditorWindow(mesh, &open);
		if (!open)
			closedMesh.push_back(popup);
	}
	for (const auto& popup : closedMesh)
		g_OpenMeshEditors.erase(popup);

	std::vector<std::string> closedMat;
	for (const auto& popup : g_OpenMaterialEditors) {
		bool open = true;
		auto pos = popup.find(':');
		std::string name = (pos != std::string::npos)
							   ? popup.substr(pos + 1)
							   : popup;
		std::shared_ptr<Material> mat;
		if (Scene::Active)
			if (auto em = Scene::Active->GetEntityManager())
				mat = em->Get<Material>(name);
		if (mat)
			RenderMaterialEditorWindow(mat, &open);
		if (!open)
			closedMat.push_back(popup);
	}
	for (const auto& popup : closedMat)
		g_OpenMaterialEditors.erase(popup);
}
} // namespace Editor
} // namespace fury
