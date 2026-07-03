#include "Fury/Editor/EditorAssetWindows.h"

#include "Fury/BoxBounds.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Joint.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/MathUtil.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/Scene.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"
#include "Fury/Vector4.h"
#include "ImGui/imgui.h"
#include "ImGuizmo.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
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

// Per-editor orbit camera state. Keyed by popup ID so each
// open mesh editor has its own azimuth / elevation / distance.
struct OrbitState {
	float azimuth = 30.0f * 0.0174532925f;	 // 30° in rad
	float elevation = 20.0f * 0.0174532925f; // 20° in rad
	float distance = 0.0f;					 // computed on open
	float initialDistance = 0.0f;
	bool initialized = false;
};
std::unordered_map<std::string, OrbitState> g_OrbitState;

OrbitState& OrbitFor(const std::string& popup_id) {
	auto it = g_OrbitState.find(popup_id);
	if (it == g_OrbitState.end())
		it = g_OrbitState.emplace(popup_id, OrbitState{}).first;
	return it->second;
}

// Compute the initial camera distance so the mesh's bounding
// sphere fills ~60% of the preview's shorter axis.
// For PerspectiveFov(fov, aspect), `fov` is the vertical FOV
// and the horizontal FOV is 2*atan(tan(fov/2)*aspect). The
// mesh fits in the view iff it fits in BOTH axes, so we take
// the larger distance:
//   dist_v = radius / tan(fov/2)
//   dist_h = radius / (tan(fov/2) * aspect)
//   dist   = max(dist_v, dist_h) = radius / tan(fov/2) / min(1, aspect)
float ComputeInitialDistance(const BoxBounds& aabb,
							 float fov, float aspect) {
	auto mn = aabb.GetMin();
	auto mx = aabb.GetMax();
	Vector4 size(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 0);
	// Bounding sphere radius (half the diagonal of the AABB).
	float radius = 0.5f * std::sqrt(size.x * size.x +
									size.y * size.y + size.z * size.z);
	if (radius < 1e-6f) radius = 1.0f;
	// 0.6 fill factor + perspective formula (tan, not sin).
	float dist = (radius * 0.6f) / std::tan(fov * 0.5f);
	// Account for aspect: the mesh must fit in the shorter
	// axis, which means a larger distance for tall windows.
	dist /= std::min(1.0f, aspect);
	return dist;
}

// ---- Mesh thumbnail cache (tasks 7.1, 7.2, 7.3) ----
// Per-mesh FBO keyed on BufferId. The mesh is rendered once
// offscreen into a 128×128 color RT + depth attachment with
// a flat-shaded directional-light shader, then the color RT
// is reused on subsequent frames until the mesh's BufferId
// changes (i.e. the mesh was re-uploaded to the GPU).
struct ThumbnailCacheEntry {
	GLuint fbo = 0;
	std::shared_ptr<Texture> colorRT;
	std::shared_ptr<Texture> depthRT;
	size_t bufferId = 0; // the BufferId the RT was rendered with
};
std::unordered_map<size_t /*BufferId*/, ThumbnailCacheEntry> g_MeshThumbnails;

// One-time-compiled flat-shaded thumbnail shader (D4 / 6.5).
std::shared_ptr<Shader> GetThumbnailShader() {
	static auto shader = Shader::Create("EditorThumbnailShader", ShaderType::OTHER);
	if (shader->GetDirty()) {
		const char* vs =
			"in vec3 vertex_position;"
			"in vec3 vertex_normal;"
			"uniform mat4 _ViewMatrix;"
			"uniform mat4 _ProjectionMatrix;"
			"out vec3 v_normal;"
			"void main()"
			"{"
			"	v_normal = normalize(vertex_normal);"
			"	gl_Position = _ProjectionMatrix * _ViewMatrix * vec4(vertex_position, 1.0);"
			"}";
		const char* fs =
			"in vec3 v_normal;"
			"out vec4 fragment_output;"
			"void main()"
			"{"
			"	vec3 lightDir = normalize(vec3(0.4, 0.8, 0.3));"
			"	float ndotl = max(dot(normalize(v_normal), lightDir), 0.2);"
			"	fragment_output = vec4(vec3(0.7) * ndotl, 1.0);"
			"}";
		shader->Compile(vs, fs, "");
	}
	return shader;
}

// Render `mesh` offscreen into the 128×128 `entry.colorRT`
// using `shader`. Camera is a fixed orbit at (azimuth=30°,
// elev=20°), distance computed from the mesh's AABB.
void RenderMeshToThumbnail(const std::shared_ptr<Mesh>& mesh,
						   const std::shared_ptr<Shader>& shader,
						   ThumbnailCacheEntry& entry) {
	// Ensure the mesh's GL buffers are uploaded before
	// binding. BindMesh calls UpdateBuffer too, but its
	// early-return when dirty prevents the bind from
	// running on the first frame.
	if (mesh->GetDirty())
		mesh->UpdateBuffer();

	glBindFramebuffer(GL_FRAMEBUFFER, entry.fbo);
	glViewport(0, 0, 128, 128);
	glClearColor(0.24f, 0.24f, 0.27f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	shader->Bind();

	// Orbit camera at (30°, 20°), distance from AABB.
	auto aabb = mesh->GetAABB();
	auto mn = aabb.GetMin();
	auto mx = aabb.GetMax();
	Vector4 center((mn.x + mx.x) * 0.5f,
				   (mn.y + mx.y) * 0.5f,
				   (mn.z + mx.z) * 0.5f, 1.0f);
	float aspect = 1.0f;
	float dist = ComputeInitialDistance(aabb,
										45.0f * 0.0174532925f, aspect);
	float az = 30.0f * 0.0174532925f;
	float el = 20.0f * 0.0174532925f;
	float cx = std::cos(el) * std::cos(az);
	float cy = std::sin(el);
	float cz = std::cos(el) * std::sin(az);
	Vector4 eye(center.x + cx * dist,
				center.y + cy * dist,
				center.z + cz * dist, 1.0f);
	Matrix4 view, proj;
	view.LookAt(eye, center, Vector4(0, 1, 0, 0));
	proj.PerspectiveFov(45.0f * 0.0174532925f, aspect, 0.1f, 1000.0f);
	shader->BindMatrix("_ViewMatrix", view);
	shader->BindMatrix("_ProjectionMatrix", proj);

	// Draw. For multi-submesh meshes, iterate submeshes.
	// Skinned meshes: joint matrices are not applied here
	// (the thumbnail shows the bind pose).
	auto submeshCount = mesh->GetSubMeshCount();
	if (submeshCount == 0) {
		shader->BindMesh(mesh);
		glDrawElements(GL_TRIANGLES,
					   static_cast<GLsizei>(mesh->Indices.Data.size()),
					   GL_UNSIGNED_INT, 0);
	} else {
		for (unsigned int i = 0; i < submeshCount; ++i) {
			auto sm = mesh->GetSubMeshAt(i);
			if (!sm) continue;
			shader->BindSubMesh(mesh, i);
			glDrawElements(GL_TRIANGLES,
						   static_cast<GLsizei>(sm->Indices.Data.size()),
						   GL_UNSIGNED_INT, 0);
		}
	}

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

	// Textures: displayed inline (no scrolllist). One row per
	// entry in GetTextures(), rendered by RenderMaterialTextureRow.
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

	// Uniforms table: one row per entry in GetUniforms().
	// Dispatch by runtime uniform type. All slider widgets use
	// a fixed width (via PushItemWidth) so the UI looks tidy.
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
void RenderMeshMetadata(const std::shared_ptr<Mesh>& mesh) {
	if (!mesh) return;

	ImGui::TextDisabled("Name: %s", mesh->GetName().c_str());

	unsigned int totalVerts =
		static_cast<unsigned int>(mesh->Positions.Data.size() / 3);
	unsigned int totalIndices = 0;
	unsigned int totalTris = 0;
	for (unsigned int i = 0; i < mesh->GetSubMeshCount(); ++i) {
		auto sm = mesh->GetSubMeshAt(i);
		if (sm) {
			totalIndices += static_cast<unsigned int>(sm->Indices.Data.size());
			totalTris += static_cast<unsigned int>(sm->Indices.Data.size()) / 3;
		}
	}
	ImGui::Text("Vertices: %u", totalVerts);
	ImGui::Text("Indices: %u", totalIndices);
	ImGui::Text("Triangles: %u", totalTris);
	ImGui::Text("Submeshes: %u", mesh->GetSubMeshCount());

	if (ImGui::TreeNode("Submeshes")) {
		for (unsigned int i = 0; i < mesh->GetSubMeshCount(); ++i) {
			auto sm = mesh->GetSubMeshAt(i);
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::TreeNode("Submesh", "Submesh %u", i)) {
				if (sm) {
					ImGui::Text("Indices: %u",
								static_cast<unsigned int>(sm->Indices.Data.size()));
					ImGui::Text("Triangles: %u",
								static_cast<unsigned int>(sm->Indices.Data.size()) / 3);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
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

// ---- Mesh editor body: 3D preview (tasks 6.5, 6.6, 6.7) ----
// Offscreen render into a Texture::GetTemporary color RT +
// raw GL FBO + depth attachment. The mesh is drawn in
// wireframe mode (glPolygonMode GL_LINE) with the thumbnail
// shader's view/projection matrices — simpler than the
// flat-shaded directional-light render, and gives a clean
// "preview" look without needing lighting.
struct PreviewRT {
	GLuint fbo = 0;
	std::shared_ptr<Texture> colorRT;
	std::shared_ptr<Texture> depthRT;
	int width = 0;
	int height = 0;
};
std::unordered_map<std::string, PreviewRT> g_PreviewRTs;

// PLACEHOLDER: The offscreen mesh render is not yet implemented.
// Shows a gray rect with a "(3D preview — render pending)" label.
// This will be finished in a follow-up proposal.
void RenderMeshPreview(const std::shared_ptr<Mesh>& mesh,
					   const std::string& popup_id, const ImVec2& size) {
	(void)popup_id;
	ImGui::BeginChild("preview", size, true,
					  ImGuiWindowFlags_NoScrollbar);
	ImGui::GetWindowDrawList()->AddRectFilled(
		ImGui::GetCursorScreenPos(),
		ImVec2(ImGui::GetCursorScreenPos().x + size.x,
			   ImGui::GetCursorScreenPos().y + size.y),
		ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.22f, 1.0f)));
	ImGui::TextDisabled("(3D preview — render pending)");
	ImGui::EndChild();
}
} // namespace

// ---- Mesh editor window (tasks 6.1, 6.2, 6.4-6.7) ----
// Regular dockable ImGui window (NOT a modal popup). Matches
// the Content Browser / Scene Inspector pattern so the editor
// docks into the editor shell like every other window.
void RenderMeshEditorWindow(const std::shared_ptr<Mesh>& mesh, bool* p_open) {
	if (!mesh) return;

	ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
	// Center on first appearance.
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

	// Metadata block (task 6.4).
	RenderMeshMetadata(mesh);
	ImGui::Separator();

	// 3D preview pane (tasks 6.5, 6.6, 6.7).
	std::string popup_id = "MeshEditor:" + mesh->GetName();
	ImVec2 avail = ImGui::GetContentRegionAvail();
	RenderMeshPreview(mesh, popup_id, ImVec2(avail.x, std::max(avail.y - 20.0f, 100.0f)));

	ImGui::End();
}

// ---- Material editor window (tasks 6.1, 6.2, 6.3) ----
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

// Lookup or allocate a 128×128 thumbnail for `mesh`. Returns the
// color RT's GL texture ID (cast to ImTextureID by the caller).
// Defined outside the file-local anonymous namespace so the
// Content Browser (EditorWindows.cpp) can call it. Delegates to
// the anonymous-namespace helpers (g_MeshThumbnails cache,
// GetThumbnailShader, RenderMeshToThumbnail).
//
// PLACEHOLDER: The offscreen mesh render is not yet implemented.
// Returns 0 so callers render the gray fallback rect. This will be
// finished in a follow-up proposal.
unsigned int GetMeshThumbnail(const std::shared_ptr<Mesh>& mesh) {
	(void)mesh;
	return 0;
}

void EvictStaleMeshThumbnails(const std::unordered_set<size_t>& liveIds) {
	for (auto it = g_MeshThumbnails.begin(); it != g_MeshThumbnails.end();) {
		if (liveIds.count(it->first) == 0) {
			if (it->second.fbo)
				glDeleteFramebuffers(1, &it->second.fbo);
			it = g_MeshThumbnails.erase(it);
		} else {
			++it;
		}
	}
}

void RenderAllOpenAssetEditors() {
	// Render mesh editors as regular dockable windows.
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

	// Render material editors as regular dockable windows.
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
