#if WITH_EDITOR

#include <cmath>
#include <functional>
#include <random>

#include "Fury/Camera.h"
#include "Fury/Component.h"
#include "Fury/EntityManager.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorAnimationWindow.h"
#include "Fury/Editor/EditorAssetPicker.h"
#include "Fury/Editor/EditorParticleWindow.h"
#include "Fury/Editor/EditorBodySetupWindow.h"
#include "Fury/Editor/EditorSkyWindow.h"
#include "Fury/Editor/EditorTerrainWindow.h"
#include "Fury/Editor/EditorReflect.hpp"
#include "Fury/Editor/EditorUiRow.h"
#include "Fury/EnumUtil.h"
#include "Fury/Heightmap.h"
#include "Fury/Light.h"
#include "Fury/OceanComponent.h"
#include "Fury/OceanWaves.h"
#include "Fury/Material.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/InstancedMeshRender.h"
#include "Fury/BodySetup.h"
#include "Fury/BuoyancyComponent.h"
#include "Fury/CharacterController.h"
#include "Fury/Engine.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/PlayerController.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/Terrain.h"
#include "Fury/Texture.h"
#include "Fury/Transform.h"
#include "Fury/Uniform.h"
#include "Fury/AnimationClip.h"
#include "Fury/AnimationPlayer.h"
#include "Fury/AnimationState.h"
#include "ImGui/imgui.h"
#include "ImGuizmo.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace fury {
namespace Editor {
extern SceneNode* g_SelectedSceneNode;

namespace {
// Walks Scene::Active->GetRootNode() and returns true iff `target`
// is reachable. Cheap (the editor's selection invariant).
bool IsReachable(const std::shared_ptr<SceneNode>& root, SceneNode* target) {
	if (!root || !target) return false;
	if (root.get() == target) return true;
	for (unsigned int i = 0; i < root->GetChildCount(); ++i) {
		if (IsReachable(root->GetChildAt(i), target)) return true;
	}
	return false;
}

bool g_NodeShowWorld = false;

void RenderSceneNodeSection(SceneNode* node) {
	if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen)) return;

	ImGui::Text("Name: %s", node->GetName().empty() ? "(unnamed)" : node->GetName().c_str());

	if (ImGui::RadioButton("Local##NodeSpace", !g_NodeShowWorld)) g_NodeShowWorld = false;
	ImGui::SameLine();
	if (ImGui::RadioButton("World##NodeSpace", g_NodeShowWorld)) g_NodeShowWorld = true;

	// Root-node transform is conventionally identity at runtime:
	// any non-identity on the active scene's root cascades into
	// the gizmo (ImGuizmo reads matrix scale as gizmo size), the
	// editor camera, and the entire world-AABB walk. Lock the
	// inputs to identity when the user selects the root so a stray
	// edit can't poison everything downstream. CLI auto-scale
	// (`ApplyRootScale`) scales *children* for this reason.
	const bool isRoot = (node == (Scene::Active
		? Scene::Active->GetRootNode().get()
		: nullptr));
	if (isRoot) {
		ImGui::SameLine();
		ImGui::TextDisabled("(root transform locked)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Root node transform is locked to identity.\nScale or move children instead.");
	}
	if (isRoot) ImGui::BeginDisabled();

	bool changed = false;

	if (!g_NodeShowWorld) {
		Vector4 pos = node->GetLocalPosition();
		if (ImReflect::Input("Position", pos).get<Vector4>().is_changed()) {
			node->SetLocalPosition(pos);
			changed = true;
		}
		Quaternion rot = node->GetLocalRoattion();
		if (ImReflect::Input("Rotation", rot).get<Quaternion>().is_changed()) {
			node->SetLocalRoattion(rot);
			changed = true;
		}
		Vector4 scl = node->GetLocalScale();
		if (ImReflect::Input("Scale", scl).get<Vector4>().is_changed()) {
			node->SetLocalScale(scl);
			changed = true;
		}
	} else {
		Vector4 pos = node->GetWorldPosition();
		Quaternion rot = node->GetWorldRoattion();
		Vector4 scl = node->GetWorldScale();
		ImGui::BeginDisabled();
		ImReflect::Input("Position", pos);
		ImReflect::Input("Rotation", rot);
		ImReflect::Input("Scale", scl);
		ImGui::EndDisabled();
	}
	if (changed) node->Recompose(false);
	if (isRoot) ImGui::EndDisabled();
}

// ----- Component body renderers -----
// Each renderer takes the SceneNode (for context) and the typed
// component pointer. They are dispatched from the generic loop
// in RenderNodePropertiesWindow below.
void RenderTransformBody(SceneNode* node, Transform* t) {
	(void)node;
	Vector4 pos = t->GetPosition();
	if (ImReflect::Input("Position", pos).get<Vector4>().is_changed()) {
		t->SetPreTransforms(pos, t->GetRotation(), t->GetScale());
	}
	Quaternion rot = t->GetRotation();
	if (ImReflect::Input("Rotation", rot).get<Quaternion>().is_changed()) {
		t->SetPreTransforms(t->GetPosition(), rot, t->GetScale());
	}
	Vector4 scl = t->GetScale();
	if (ImReflect::Input("Scale", scl).get<Vector4>().is_changed()) {
		t->SetPreTransforms(t->GetPosition(), t->GetRotation(), scl);
	}
}

void RenderLightBody(SceneNode* node, Light* light) {
	(void)node;
	bool aabb_dirty = false;

	LightType lt = light->GetType();
	if (ImReflect::Input("Type", lt).get<LightType>().is_changed()) {
		light->SetType(lt);
		aabb_dirty = true;
	}

	Color col = light->GetColor();
	if (ImReflect::Input("Color", col).get<Color>().is_changed()) {
		light->SetColor(col);
	}

	float intensity = light->GetIntensity();
	if (ImGui::DragFloat("Intensity", &intensity, 0.01f, 0.0f, 100.0f)) {
		light->SetIntensity(intensity);
	}

	float inner_deg = light->GetInnerAngle() * MathUtil::RadToDeg;
	if (ImGui::DragFloat("Inner Angle", &inner_deg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
		light->SetInnerAngle(inner_deg * MathUtil::DegToRad);
		aabb_dirty = true;
	}

	float outer_deg = light->GetOutterAngle() * MathUtil::RadToDeg;
	if (ImGui::DragFloat("Outer Angle", &outer_deg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
		light->SetOutterAngle(outer_deg * MathUtil::DegToRad);
		aabb_dirty = true;
	}

	float falloff = light->GetFalloff();
	if (ImGui::DragFloat("Falloff", &falloff, 0.01f, 0.0f, 10.0f)) {
		light->SetFalloff(falloff);
	}

	float radius = light->GetRadius();
	if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 1000.0f)) {
		light->SetRadius(radius);
		aabb_dirty = true;
	}

	bool cast = light->GetCastShadows();
	if (ImGui::Checkbox("Cast Shadows", &cast)) {
		light->SetCastShadows(cast);
	}

	if (aabb_dirty) {
		light->CalculateAABB();
		light->EvaluateVolume();
	}
}

// Camera keeps the full body for v1 -- the only component whose
// editable state isn't trivially "remove + re-add" to recover
// from a mistake. Drop the static s_Aspect in favor of
// GetAspect-ish behavior by tracking the user's last value
// across edits.
void RenderCameraBody(SceneNode* node, Camera* cam) {
	(void)node;
	static float s_Aspect = 16.0f / 9.0f;
	float fov = cam->GetFov();
	if (ImGui::DragFloat("FOV (deg)", &fov, 0.5f, 1.0f, 179.0f, "%.1f")) {
		cam->PerspectiveFov(fov, s_Aspect, cam->GetNear(), cam->GetFar());
	}
	float near_p = cam->GetNear();
	if (ImGui::DragFloat("Near", &near_p, 0.01f, 0.001f, 100.0f)) {
		cam->PerspectiveFov(fov, s_Aspect, near_p, cam->GetFar());
	}
	float far_p = cam->GetFar();
	if (ImGui::DragFloat("Far", &far_p, 1.0f, 1.0f, 10000.0f)) {
		cam->PerspectiveFov(fov, s_Aspect, cam->GetNear(), far_p);
	}
	if (ImGui::DragFloat("Aspect", &s_Aspect, 0.01f, 0.1f, 10.0f, "%.2f")) {
		cam->SetAspect(s_Aspect);
	}
	// shadow range/distribution live in Render Settings (scene-wide)
	BoxBounds sb = cam->GetShadowBounds(false);
	Vector4 mn = sb.GetMin();
	Vector4 mx = sb.GetMax();
	bool bounds_changed = false;
	bounds_changed |= ImGui::DragFloat4("Shadow Min", &mn.x, 0.5f);
	bounds_changed |= ImGui::DragFloat4("Shadow Max", &mx.x, 0.5f);
	if (bounds_changed) {
		cam->SetShadowBounds(mn, mx);
	}
}

// MeshRender body: cast-shadows + editable mesh row + per-slot
// editable material rows + Add Material Slot. Replaces the old
// read-only summary (mesh name + per-slot collapsing headers).
// Buttons are placed at the front of the line so the action cluster
// is consistent across rows.
void RenderMeshRenderBody(SceneNode* node, MeshRender* mr) {
	(void)node;
	auto mesh = mr->GetMesh();

	// Cast Shadows is a per-MeshRender flag now, not a per-Mesh
	// one. The mesh is a shared resource, so the knob lives on
	// the instance -- toggling one tank's shadow leaves the
	// others that share the mesh alone.
	bool cast = mr->GetCastShadows();
	if (ImGui::Checkbox("Cast Shadows", &cast)) {
		mr->SetCastShadows(cast);
	}

	// Mesh row: buttons first, then label + name.
	ImGui::AlignTextToFramePadding();

	// Change button (opens mesh picker).
	if (ImGui::Button("Change")) {
		ImGui::OpenPopup("MeshPicker");
	}
	ImGui::SameLine();

	// -> jump-to-asset button (disabled when no mesh).
	if (!mesh) ImGui::BeginDisabled();
	if (ImGui::Button("->")) {
		if (mesh)
			Editor::SelectAssetInBrowser(typeid(Mesh), mesh->GetName());
	}
	if (!mesh) ImGui::EndDisabled();
	ImGui::SameLine();

	// x remove button -- unbinds the mesh from this MeshRender.
	if (ImGui::Button("x")) {
		mr->SetMesh(nullptr);
	}
	ImGui::SameLine();

	ImGui::TextUnformatted("Mesh:");
	ImGui::SameLine();
	if (mesh) {
		ImGui::TextUnformatted(mesh->GetName().c_str());
	} else {
		ImGui::TextDisabled("(no mesh)");
	}

	// LOD readout. The chain lives on the bound Mesh (LOD 0 is
	// the source itself; LODs 1..N are the additional levels).
	// Edit the thresholds from the mesh editor; here we only show
	// the current chain + the runtime's active-LOD selection.
	{
		const unsigned int lod_count = mesh ? mesh->GetLodCount() : 1;
		if (lod_count <= 1) {
			ImGui::TextDisabled("LOD: (single mesh)");
		} else {
			ImGui::Text("LOD chain: %u level(s); active = %u", lod_count, mr->GetActiveLod());
		}
	}

	// Mesh picker modal (Change button above opens it).
	RenderAssetPickerModal("MeshPicker", "Pick Mesh", typeid(Mesh),
						   [mr](std::shared_ptr<void> p) {
							   auto m = std::static_pointer_cast<Mesh>(p);
							   mr->SetMesh(m);
						   });

	// Per-material-slot rows. Buttons first, then label + name.
	for (unsigned int i = 0; i < mr->GetMaterialCount(); ++i) {
		auto mat = mr->GetMaterial(i);

		ImGui::PushID(static_cast<int>(i));
		ImGui::AlignTextToFramePadding();

		// Change button (opens material picker for this slot).
		if (ImGui::Button("Change")) {
			char popup[64];
			std::snprintf(popup, sizeof(popup), "MaterialPicker%u", i);
			ImGui::OpenPopup(popup);
		}
		ImGui::SameLine();

		// -> jump-to-asset button (disabled when slot is null).
		if (!mat) ImGui::BeginDisabled();
		if (ImGui::Button("->")) {
			if (mat)
				Editor::SelectAssetInBrowser(typeid(Material), mat->GetName());
		}
		if (!mat) ImGui::EndDisabled();
		ImGui::SameLine();

		// x remove-slot button -- sets the slot to null weak_ptr.
		if (ImGui::Button("x")) {
			mr->SetMaterial(nullptr, i);
		}
		ImGui::SameLine();

		ImGui::Text("Slot %u:", i);
		ImGui::SameLine();
		if (mat) {
			ImGui::TextUnformatted(mat->GetName().c_str());
		} else {
			ImGui::TextDisabled("(none)");
		}

		// Material picker modal for this slot.
		char popup[64];
		std::snprintf(popup, sizeof(popup), "MaterialPicker%u", i);
		char title[80];
		std::snprintf(title, sizeof(title), "Pick Material (slot %u)", i);
		RenderAssetPickerModal(popup, title, typeid(Material),
							   [mr, i](std::shared_ptr<void> p) {
								   auto m = std::static_pointer_cast<Material>(p);
								   mr->SetMaterial(m, i);
							   });

		ImGui::PopID();
	}

	// Add Material Slot button. Only available when a mesh is
	// bound (we need its submesh count) and the slot count is
	// short of it. SetMaterial(nullptr, count) appends a null
	// slot (hits the else-branch push_back).
	if (mesh && mr->GetMaterialCount() < mesh->GetSubMeshCount()) {
		if (ImGui::Button("Add Material Slot")) {
			mr->SetMaterial(nullptr, mr->GetMaterialCount());
		}
	}
}

// ----- InstancedMeshRender inspector (task 6.7, add-kraut-vegetation) -----

// Finds the first Terrain component in the active scene (recursive).
static Terrain* FindSceneTerrain() {
	if (!Scene::Active) return nullptr;
	auto root = Scene::Active->GetRootNode();
	if (!root) return nullptr;
	Terrain* found = nullptr;
	std::function<void(const std::shared_ptr<SceneNode>&)> walk = [&](const std::shared_ptr<SceneNode>& n) {
		if (found || !n) return;
		if (auto t = n->GetComponent<Terrain>()) { found = t.get(); return; }
		for (unsigned int i = 0; i < n->GetChildCount(); ++i)
			walk(n->GetChildAt(i));
	};
	walk(root);
	return found;
}

void RenderInstancedMeshRenderBody(SceneNode* node, InstancedMeshRender* imr) {
	(void)node;
	auto mesh = imr->GetMesh();

	bool cast = imr->GetCastShadows();
	if (EditorUi::CheckboxRow("Cast Shadows", &cast))
		imr->SetCastShadows(cast);

	// ISM (single tier) vs HISM (per-instance LOD buckets).
	bool hism = imr->GetHierarchical();
	if (EditorUi::CheckboxRow("HISM (per-instance LOD)", &hism))
		imr->SetHierarchical(hism);

	float cullDist = imr->GetCullDistance();
	EditorUi::FieldRow("Cull Distance (cm)", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##cull", &cullDist, 50.0f, 0.0f, 500000.0f, "%.0f"))
			imr->SetCullDistance(cullDist);
	}, "0 = no cap; past this distance instances drop out entirely");

	// Mesh row (same button layout as the MeshRender body).
	ImGui::AlignTextToFramePadding();
	if (ImGui::Button("Change")) {
		ImGui::OpenPopup("InstMeshPicker");
	}
	ImGui::SameLine();
	if (!mesh) ImGui::BeginDisabled();
	if (ImGui::Button("->")) {
		if (mesh)
			Editor::SelectAssetInBrowser(typeid(Mesh), mesh->GetName());
	}
	if (!mesh) ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("x")) {
		imr->SetMesh(nullptr);
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("Mesh:");
	ImGui::SameLine();
	if (mesh) {
		ImGui::TextUnformatted(mesh->GetName().c_str());
	} else {
		ImGui::TextDisabled("(no mesh)");
	}

	{
		const unsigned int lod_count = mesh ? mesh->GetLodCount() : 1;
		if (lod_count <= 1) {
			ImGui::TextDisabled("LOD: (single mesh)");
		} else {
			ImGui::Text("LOD chain: %u level(s)", lod_count);
		}
	}

	RenderAssetPickerModal("InstMeshPicker", "Pick Mesh", typeid(Mesh),
						   [imr](std::shared_ptr<void> p) {
							   auto m = std::static_pointer_cast<Mesh>(p);
							   imr->SetMesh(m);
						   });

	// Material slots.
	for (unsigned int i = 0; i < imr->GetMaterialCount(); ++i) {
		auto mat = imr->GetMaterial(i);
		ImGui::PushID(static_cast<int>(i));
		ImGui::AlignTextToFramePadding();
		if (ImGui::Button("Change")) {
			char popup[64];
			std::snprintf(popup, sizeof(popup), "InstMaterialPicker%u", i);
			ImGui::OpenPopup(popup);
		}
		ImGui::SameLine();
		if (!mat) ImGui::BeginDisabled();
		if (ImGui::Button("->")) {
			if (mat)
				Editor::SelectAssetInBrowser(typeid(Material), mat->GetName());
		}
		if (!mat) ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("x")) {
			imr->SetMaterial(nullptr, i);
		}
		ImGui::SameLine();
		ImGui::Text("Slot %u:", i);
		ImGui::SameLine();
		if (mat) {
			ImGui::TextUnformatted(mat->GetName().c_str());
		} else {
			ImGui::TextDisabled("(none)");
		}
		char popup[64];
		std::snprintf(popup, sizeof(popup), "InstMaterialPicker%u", i);
		char title[80];
		std::snprintf(title, sizeof(title), "Pick Material (slot %u)", i);
		RenderAssetPickerModal(popup, title, typeid(Material),
							   [imr, i](std::shared_ptr<void> p) {
								   auto m = std::static_pointer_cast<Material>(p);
								   imr->SetMaterial(m, i);
							   });
		ImGui::PopID();
	}

	if (mesh && imr->GetMaterialCount() < mesh->GetSubMeshCount()) {
		if (ImGui::Button("Add Material Slot")) {
			imr->SetMaterial(nullptr, imr->GetMaterialCount());
		}
	}

	ImGui::Separator();

	// ---- Instance editing ----
	ImGui::Text("Instances: %u", imr->GetInstanceCount());

	if (ImGui::Button("Add Instance")) {
		InstancedMeshRender::Instance inst;
		imr->AddInstance(inst);
	}

	// Fixed-height scrolling list via ImGuiListClipper: the raw per-row
	// replication capped at 64 rows and rebuilt the whole list every frame;
	// the clipper renders only the visible rows so 10k+ instance components
	// stay interactive. Click a row to edit it below.
	static int s_SelectedInstance = -1;
	unsigned int removeIndex = 0xffffffff;
	unsigned int duplicateIndex = 0xffffffff;
	const float listH = ImGui::GetTextLineHeightWithSpacing() * 10.0f;
	if (ImGui::BeginChild("##imr_instances", ImVec2(0.0f, listH), true)) {
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(imr->GetInstanceCount()));
		char label[96];
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				const auto &inst = imr->GetInstance(static_cast<unsigned int>(i));
				// Y-twist readout: exact for pure-yaw instance rotations
				// (the scatter/common case), approximate otherwise
				const Quaternion &q = inst.Rotation;
				float yawDeg = 2.0f * std::atan2(q.y, q.w) * MathUtil::RadToDeg;
				std::snprintf(label, sizeof(label), "%4d  (%.0f, %.0f, %.0f)  yaw %.0f  x%.2f",
					i, inst.Position.x, inst.Position.y, inst.Position.z, yawDeg, inst.Scale.x);
				if (ImGui::Selectable(label, s_SelectedInstance == i))
					s_SelectedInstance = i;
			}
		}
	}
	ImGui::EndChild();

	// selected-instance editor
	if (s_SelectedInstance >= 0 && s_SelectedInstance < static_cast<int>(imr->GetInstanceCount())) {
		auto inst = imr->GetInstance(static_cast<unsigned int>(s_SelectedInstance));
		bool changed = false;
		ImGui::PushItemWidth(80.0f);
		changed |= ImGui::DragFloat("X", &inst.Position.x, 1.0f);
		ImGui::SameLine();
		changed |= ImGui::DragFloat("Y", &inst.Position.y, 1.0f);
		ImGui::SameLine();
		changed |= ImGui::DragFloat("Z", &inst.Position.z, 1.0f);
		ImGui::PopItemWidth();
		float yawDeg = 2.0f * std::atan2(inst.Rotation.y, inst.Rotation.w) * MathUtil::RadToDeg;
		if (ImGui::DragFloat("Yaw (deg)", &yawDeg, 0.5f)) {
			inst.Rotation = MathUtil::AxisRadToQuat(Vector4::YAxis, yawDeg * MathUtil::DegToRad);
			changed = true;
		}
		if (ImGui::DragFloat("Scale", &inst.Scale.x, 0.01f, 0.01f, 1000.0f)) {
			inst.Scale.y = inst.Scale.z = inst.Scale.x;
			changed = true;
		}
		if (changed)
			imr->SetInstance(static_cast<unsigned int>(s_SelectedInstance), inst);
		if (ImGui::Button("Remove")) {
			removeIndex = static_cast<unsigned int>(s_SelectedInstance);
		}
		ImGui::SameLine();
		if (ImGui::Button("Duplicate")) {
			duplicateIndex = static_cast<unsigned int>(s_SelectedInstance);
		}
	}
	if (removeIndex != 0xffffffff) {
		imr->RemoveInstance(removeIndex);
		s_SelectedInstance = -1;
	}
	if (duplicateIndex != 0xffffffff)
		imr->AddInstance(imr->GetInstance(duplicateIndex));

	ImGui::Separator();

	// ---- Seeded scatter over terrain ----
	// Distributes instances in a rectangle around the node's position,
	// snapped to the terrain surface. Deterministic per seed.
	static int s_ScatterCount = 100;
	static int s_ScatterSeed = 7;
	static float s_ScatterRadius = 5000.0f;   // cm, half-extent of the scatter square
	static float s_ScatterMinHeight = -1e30f; // below this terrain height instances are skipped (e.g. waterline)
	static bool s_ScatterReplace = true;
	ImGui::TextDisabled("Scatter over terrain:");

	EditorUi::FieldRowN("Count / Seed", 2, [&](float w) {
		ImGui::SetNextItemWidth(w);
		ImGui::InputInt("##scat_count", &s_ScatterCount);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(w);
		ImGui::InputInt("##scat_seed", &s_ScatterSeed);
	});

	EditorUi::FieldRow("Radius (cm)", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		ImGui::DragFloat("##scat_radius", &s_ScatterRadius, 10.0f, 1.0f, 100000.0f, "%.0f");
	});
	EditorUi::FieldRow("Min Height", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		ImGui::DragFloat("##scat_minh", &s_ScatterMinHeight, 1.0f, -100000.0f, 100000.0f, "%.1f");
	}, "skip terrain below this height (waterline)");
	EditorUi::CheckboxRow("Replace existing instances", &s_ScatterReplace);

	Terrain* terrain = FindSceneTerrain();
	if (!terrain) ImGui::BeginDisabled();
	if (ImGui::Button("Scatter")) {
		if (terrain) {
			auto owner = imr->GetOwner();
			Vector4 center = owner ? owner->GetWorldPosition() : Vector4(0.0f, 0.0f, 0.0f);

			// std::mt19937 for cross-platform determinism.
			std::mt19937 rng(static_cast<unsigned int>(s_ScatterSeed));
			std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
			std::uniform_real_distribution<float> yaw(0.0f, 6.2831853f);
			std::uniform_real_distribution<float> scaleJitter(0.85f, 1.25f);

			if (s_ScatterReplace)
				imr->ClearInstances();

			const Matrix4 invWorld = owner ? owner->GetWorldMatrix().Inverse() : Matrix4();
			unsigned int placed = 0, attempts = 0;
			const unsigned int maxAttempts = static_cast<unsigned int>(s_ScatterCount) * 10;
			while (placed < static_cast<unsigned int>(s_ScatterCount) && attempts < maxAttempts) {
				++attempts;
				float wx = center.x + dist(rng) * s_ScatterRadius;
				float wz = center.z + dist(rng) * s_ScatterRadius;
				float h = terrain->GetHeight(wx, wz);
				if (h < s_ScatterMinHeight)
					continue;

				// Instance transforms are node-local; convert the world
				// sample point back into the node's space.
				Vector4 localPos = invWorld.Multiply(Vector4(wx, h, wz));
				InstancedMeshRender::Instance inst;
				inst.Position = localPos;
				inst.Rotation = MathUtil::AxisRadToQuat(Vector4::YAxis, yaw(rng));
				float s = scaleJitter(rng);
				inst.Scale = Vector4(s, s, s);
				imr->AddInstance(inst);
				++placed;
			}
		}
	}
	if (!terrain) ImGui::EndDisabled();
	if (!terrain)
		ImGui::TextDisabled("(no Terrain in scene)");
}

// ----- Component dispatch table -----
struct ComponentEntry {
	std::string name;
	std::type_index type;
	bool removable; // false for Transform
	void (*render)(SceneNode*, Component*);
};

static const char* kAnimWrapModeNames[] = {"Default", "Once", "Loop", "ClampForever", "PingPong"};
static int kAnimWrapModeCount = 5;

// ParticleRenderer node-properties body. The system row mirrors the
// MeshRender mesh row: Change (asset picker) / -> (jump to asset) /
// x (unbind). Authoring of modules lives in the ParticleSystem asset
// (particle editor); the component only references it by name.
void RenderParticleRendererBody(SceneNode* node, ParticleRenderer* pr) {
	(void)node;
	if (!pr) return;
	auto system = pr->GetSystem();

	// System row: buttons first, then label + name.
	ImGui::AlignTextToFramePadding();
	if (ImGui::Button("Change"))
		ImGui::OpenPopup("ParticleSystemPicker");
	ImGui::SameLine();
	const bool hasSystem = !pr->GetSystemName().empty();
	if (!hasSystem) ImGui::BeginDisabled();
	if (ImGui::Button("->"))
		Editor::SelectAssetInBrowser(typeid(ParticleSystem), pr->GetSystemName());
	if (!hasSystem) ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("x"))
		pr->SetSystemName("");
	ImGui::SameLine();
	ImGui::TextUnformatted("System:");
	ImGui::SameLine();
	if (hasSystem)
		ImGui::TextUnformatted(pr->GetSystemName().c_str());
	else
		ImGui::TextDisabled("(no system)");

	RenderAssetPickerModal("ParticleSystemPicker", "Pick ParticleSystem",
		typeid(ParticleSystem),
		[pr](std::shared_ptr<void> p) {
			auto ps = std::static_pointer_cast<ParticleSystem>(p);
			pr->SetSystemName(ps ? ps->GetName() : "");
		});

	if (system)
		ImGui::TextDisabled("  Live: %u / %u", system->GetAliveCount(), system->GetMaxParticles());
	ImGui::TextDisabled("Blend mode: %s",
		pr->GetBlendMode() == ParticleBlend::ADDITIVE ? "ADDITIVE" : "ALPHA");
	ImGui::TextDisabled("Material: %s",
		(pr->GetMaterial() ? pr->GetMaterial()->GetName().c_str() : "(none)"));
	if (system && ImGui::Button("Open Particle Editor..."))
		Editor::OpenParticleEditor(system);
}

void RenderAnimatorBody(SceneNode* node, Animator* anim) {
	(void)node;
	bool phys = anim->GetAnimatePhysics();
	if (ImGui::Checkbox("Animate Physics", &phys)) {
		anim->SetAnimatePhysics(phys);
		Editor::MarkSceneDirty();
	}

	int wrapIdx = static_cast<int>(anim->GetDefaultWrapMode());
	if (ImGui::Combo("Default Wrap", &wrapIdx, kAnimWrapModeNames, kAnimWrapModeCount)) {
		anim->SetDefaultWrapMode(static_cast<AnimWrapMode>(wrapIdx));
		Editor::MarkSceneDirty();
	}

	// Skeleton debug overlay -- draws each joint's world position and
	// parent link on top of the viewport. Helps diagnose mangled
	// skin deformation by showing the runtime joint TRS vs. the
	// bind pose the mesh was authored against.
	bool showJoints = IsJointDebugEnabled();
	if (ImGui::Checkbox("Show Joints", &showJoints)) {
		SetJointDebugEnabled(showJoints);
	}

	ImGui::Separator();
	ImGui::TextDisabled("States (%u)", anim->GetStateCount());

	for (unsigned int i = 0; i < anim->GetStateCount(); ++i) {
		auto state = anim->GetStateAt(i);
		if (!state) continue;

		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(state->GetName().c_str())) {
			auto clip = state->GetClip();
			ImGui::Text("Clip: %s", clip ? clip->GetName().c_str() : "(none)");
			ImGui::Text("Playing: %s", anim->IsPlaying(state->GetName()) ? "yes" : "no");

			bool en = state->IsEnabled();
			if (ImGui::Checkbox("Enabled", &en)) {
				state->SetEnabled(en);
				Editor::MarkSceneDirty();
			}

			float w = state->GetWeight();
			if (ImGui::SliderFloat("Weight", &w, 0.0f, 1.0f)) {
				state->SetWeight(w);
				Editor::MarkSceneDirty();
			}

			float spd = state->GetSpeed();
			if (ImGui::DragFloat("Speed", &spd, 0.05f, -5.0f, 5.0f, "%.2f")) {
				state->SetSpeed(spd);
				Editor::MarkSceneDirty();
			}

			int lyr = state->GetLayer();
			if (ImGui::DragInt("Layer", &lyr)) {
				state->SetLayer(lyr);
				Editor::MarkSceneDirty();
			}

			int sw = static_cast<int>(state->GetWrapMode());
			if (ImGui::Combo("Wrap", &sw, kAnimWrapModeNames, kAnimWrapModeCount)) {
				state->SetWrapMode(static_cast<AnimWrapMode>(sw));
				Editor::MarkSceneDirty();
			}

			float t = state->GetTime();
			float len = state->GetLength();
			if (len > 0.0f) {
				if (ImGui::SliderFloat("Scrub", &t, 0.0f, len, "%.3f s")) {
					// Scrub: set time and re-pose immediately.
					bool wasEnabled = state->IsEnabled();
					state->SetEnabled(true);
					state->SetTime(t);
					anim->AdvanceTime(0.0f);
					anim->Display(1.0f);
					state->SetEnabled(wasEnabled);
					Editor::MarkSceneDirty();
				}
			}

			if (ImGui::Button("Play")) {
				anim->Play(state->GetName());
				Editor::MarkSceneDirty();
			}
			ImGui::SameLine();
			if (ImGui::Button("Stop")) {
				anim->Stop(state->GetName());
				Editor::MarkSceneDirty();
			}
			ImGui::SameLine();
		if (ImGui::Button("Rewind")) {
			anim->Rewind(state->GetName());
			Editor::MarkSceneDirty();
		}
		ImGui::SameLine();
		if (ImGui::Button("x Remove")) {
			anim->RemoveClip(state->GetName());
			Editor::MarkSceneDirty();
			ImGui::TreePop();
			ImGui::PopID();
			continue;
		}

			// CrossFade target row: small float input + button.
			ImGui::PushItemWidth(80.0f);
			static thread_local float s_CrossFadeLen = 0.3f;
			ImGui::DragFloat("##fadeLen", &s_CrossFadeLen, 0.05f, 0.0f, 10.0f, "%.2f s");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if (ImGui::Button("CrossFade")) {
				anim->CrossFade(state->GetName(), s_CrossFadeLen);
				Editor::MarkSceneDirty();
			}

			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	// Bind a registered AnimationClip as a new state on this Animator.
	// "Bind" lists every clip in the active scene's EntityManager; "Bind
	// from Selection" takes the currently selected node's MeshRender's
	// associated clip (handy for re-binding after the node picks change).
	ImGui::Separator();
	if (ImGui::Button("Bind Clip")) {
		ImGui::OpenPopup("AnimatorBindClipPopup");
	}
	ImGui::SameLine();
	if (ImGui::Button("Bind from Selection")) {
		ImGui::OpenPopup("AnimatorBindFromSelPopup");
	}

	// "Bind Clip" uses the shared asset-picker modal (same component as
	// the mesh/material/texture pickers). Re-binding an already-bound
	// clip name just overwrites -- harmless.
	RenderAssetPickerModal("AnimatorBindClipPopup", "Bind Clip", typeid(AnimationClip),
		[anim](std::shared_ptr<void> p) {
			auto c = std::static_pointer_cast<AnimationClip>(p);
			if (c) {
				anim->SetClip(c->GetName(), c);
				Editor::MarkSceneDirty();
			}
		});

	if (ImGui::BeginPopup("AnimatorBindFromSelPopup")) {
		std::shared_ptr<AnimationClip> selClip;
		// "Bind from Selection" picks the first clip that targets a joint
		// of the selected node's mesh; if no joint match, fall back to
		// the first clip registered in the active scene's EntityManager.
		if (auto sel = Editor::GetSelectedSceneNode()) {
			if (auto mr = sel->GetComponent<MeshRender>()) {
				auto mesh = mr->GetMesh();
				if (mesh && Scene::Active) {
					if (auto mgr = Scene::Active->GetEntityManager()) {
						mgr->ForEach<AnimationClip>([&](const std::shared_ptr<AnimationClip> &c) -> bool {
							if (selClip) return true;
							if (!c) return true;
							int n = c->GetChannelCount();
							for (int i = 0; i < n; ++i) {
								auto ch = c->GetChannelAt(i);
								if (ch && mesh->GetJoint(ch->name)) { selClip = c; break; }
							}
							return true;
						});
						if (!selClip) {
							mgr->ForEach<AnimationClip>([&](const std::shared_ptr<AnimationClip> &c) -> bool {
								if (!selClip) selClip = c;
								return !selClip;
							});
						}
					}
				}
			}
		}
		if (!selClip) {
			ImGui::TextDisabled("(no clip on selected node)");
		} else {
			bool alreadyBound = anim->GetState(selClip->GetName()) != nullptr;
			if (alreadyBound) ImGui::BeginDisabled();
			if (ImGui::MenuItem(selClip->GetName().c_str())) {
				anim->SetClip(selClip->GetName(), selClip);
				Editor::MarkSceneDirty();
			}
			if (alreadyBound) ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}
}

// BodySetup collision authoring. Shape/motion combos, per-shape dims,
// dynamic-body params, collision-mesh asset row, and the entry point
// into the BodySetup editor window (mesh-editor-style popup).
void RenderBodySetupBody(SceneNode* node, BodySetup* body) {
	if (!body) return;

	static const char* kShapeNames[] = { "Mesh", "Box", "Sphere", "HeightField" };
	int shapeIdx = static_cast<int>(body->GetShapeType());
	EditorUi::FieldRow("Shape", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::Combo("##shape", &shapeIdx, kShapeNames, 4)) {
			body->SetShapeType(static_cast<BodySetup::ShapeType>(shapeIdx));
			Editor::MarkSceneDirty();
		}
	});
	if (body->GetShapeType() == BodySetup::ShapeType::HeightField) {
		bool hasTerrain = node && node->GetComponent<Terrain>() != nullptr;
		if (hasTerrain)
			ImGui::TextWrapped("Heightfield from sibling Terrain heights");
		else
			ImGui::TextWrapped("Needs a sibling Terrain component!");
	}

	static const char* kMotionNames[] = { "Static", "Dynamic" };
	int motionIdx = static_cast<int>(body->GetMotionType());
	EditorUi::FieldRow("Motion", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::Combo("##motion", &motionIdx, kMotionNames, 2)) {
			body->SetMotionType(static_cast<BodySetup::MotionType>(motionIdx));
			Editor::MarkSceneDirty();
		}
	});

	if (body->GetShapeType() == BodySetup::ShapeType::Mesh) {
		// Collision mesh row - same Change/jump/clear pattern as the particle
		// system row. Empty means "use the sibling MeshRender's mesh".
		const bool hasMesh = !body->GetCollisionMeshName().empty();
		if (ImGui::Button("Change"))
			ImGui::OpenPopup("CollisionMeshPicker");
		ImGui::SameLine();
		if (!hasMesh) ImGui::BeginDisabled();
		if (ImGui::Button("->"))
			Editor::SelectAssetInBrowser(typeid(Mesh), body->GetCollisionMeshName());
		if (!hasMesh) ImGui::EndDisabled();
		ImGui::SameLine();
		if (!hasMesh) ImGui::BeginDisabled();
		if (ImGui::Button("x"))
			body->SetCollisionMeshName("");
		if (!hasMesh) ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextUnformatted("Collision Mesh:");
		ImGui::SameLine();
		if (hasMesh)
			ImGui::TextUnformatted(body->GetCollisionMeshName().c_str());
		else
			ImGui::TextDisabled("(render mesh)");

		RenderAssetPickerModal("CollisionMeshPicker", "Pick Collision Mesh",
			typeid(Mesh),
			[body](std::shared_ptr<void> p) {
				auto mesh = std::static_pointer_cast<Mesh>(p);
				body->SetCollisionMeshName(mesh ? mesh->GetName() : "");
			});
	}
	else if (body->GetShapeType() == BodySetup::ShapeType::Box) {
		Vector4 he = body->GetHalfExtents();
		EditorUi::FieldRowN("Half Extents", 3, [&](float w) {
			ImGui::SetNextItemWidth(w);
			bool c = ImGui::DragFloat("##hex", &he.x, 0.5f, 0.1f, 100000.0f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(w);
			c |= ImGui::DragFloat("##hey", &he.y, 0.5f, 0.1f, 100000.0f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(w);
			c |= ImGui::DragFloat("##hez", &he.z, 0.5f, 0.1f, 100000.0f);
			if (c) {
				body->SetHalfExtents(he);
				Editor::MarkSceneDirty();
			}
		});
		if (EditorUi::ButtonRow("Auto-Fit From Mesh")) {
			body->AutoFitFromMesh();
			Editor::MarkSceneDirty();
		}
	}
	else {
		float radius = body->GetRadius();
		EditorUi::FieldRow("Radius", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragFloat("##radius", &radius, 0.5f, 0.5f, 100000.0f)) {
				body->SetRadius(radius);
				Editor::MarkSceneDirty();
			}
		});
		if (EditorUi::ButtonRow("Auto-Fit From Mesh")) {
			body->AutoFitFromMesh();
			Editor::MarkSceneDirty();
		}
	}

	if (body->GetMotionType() == BodySetup::MotionType::Dynamic) {
		float mass = body->GetMass();
		EditorUi::FieldRow("Mass", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragFloat("##mass", &mass, 0.1f, 0.001f, 100000.0f)) {
				body->SetMass(mass);
				Editor::MarkSceneDirty();
			}
		});
	}
	float friction = body->GetFriction();
	EditorUi::FieldRow("Friction", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::SliderFloat("##friction", &friction, 0.0f, 1.0f)) {
			body->SetFriction(friction);
			Editor::MarkSceneDirty();
		}
	});
	float restitution = body->GetRestitution();
	EditorUi::FieldRow("Restitution", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::SliderFloat("##restitution", &restitution, 0.0f, 1.0f)) {
			body->SetRestitution(restitution);
			Editor::MarkSceneDirty();
		}
	});

	if (EditorUi::ButtonRow("Open Body Setup Editor..."))
		Editor::OpenBodySetupEditor(node->shared_from_this());
}

void RenderFreeFlyControllerBody(SceneNode* node, FreeFlyController* ctrl) {
	if (!ctrl) return;
	bool enabled = ctrl->IsEnabled();
	if (ImGui::Checkbox("Enabled", &enabled)) {
		ctrl->SetEnabled(enabled);
		Editor::MarkSceneDirty();
	}

	char buf[256];
	strncpy(buf, ctrl->GetCameraNodeName().c_str(), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	if (ImGui::InputText("Camera Node", buf, sizeof(buf))) {
		ctrl->SetCameraNodeName(buf);
		Editor::MarkSceneDirty();
	}
	if (ctrl->GetCameraNodeName().empty())
		ImGui::TextDisabled("(empty = drive the owning node)");

	float speed = ctrl->GetMoveSpeed();
	if (ImGui::DragFloat("Move Speed (cm/s)", &speed, 1.0f, 1.0f, 100000.0f)) {
		ctrl->SetMoveSpeed(speed);
		Editor::MarkSceneDirty();
	}
	float sens = ctrl->GetMouseSensitivity();
	if (ImGui::DragFloat("Mouse Sensitivity", &sens, 0.0001f, 0.0001f, 0.1f, "%.4f")) {
		ctrl->SetMouseSensitivity(sens);
		Editor::MarkSceneDirty();
	}
}

void RenderCharacterControllerBody(SceneNode* node, CharacterController* ctrl) {
	if (!ctrl) return;
	bool enabled = ctrl->IsEnabled();
	if (ImGui::Checkbox("Enabled", &enabled)) {
		ctrl->SetEnabled(enabled);
		Editor::MarkSceneDirty();
	}

	char buf[256];
	strncpy(buf, ctrl->GetCameraNodeName().c_str(), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	if (ImGui::InputText("Camera Node", buf, sizeof(buf))) {
		ctrl->SetCameraNodeName(buf);
		Editor::MarkSceneDirty();
	}

	float height = ctrl->GetHeight();
	if (ImGui::DragFloat("Capsule Height", &height, 1.0f, 1.0f, 100000.0f)) {
		ctrl->SetHeight(height);
		Editor::MarkSceneDirty();
	}
	float radius = ctrl->GetRadius();
	if (ImGui::DragFloat("Capsule Radius", &radius, 0.5f, 0.5f, 100000.0f)) {
		ctrl->SetRadius(radius);
		Editor::MarkSceneDirty();
	}
	if (ImGui::Button("Auto-Fit From Node Bounds")) {
		ctrl->AutoFitFromNode();
		Editor::MarkSceneDirty();
	}

	float walk = ctrl->GetWalkSpeed();
	if (ImGui::DragFloat("Walk Speed", &walk, 1.0f, 1.0f, 100000.0f)) {
		ctrl->SetWalkSpeed(walk);
		Editor::MarkSceneDirty();
	}
	float run = ctrl->GetRunSpeed();
	if (ImGui::DragFloat("Run Speed", &run, 1.0f, 1.0f, 100000.0f)) {
		ctrl->SetRunSpeed(run);
		Editor::MarkSceneDirty();
	}
	float jump = ctrl->GetJumpSpeed();
	if (ImGui::DragFloat("Jump Speed", &jump, 1.0f, 1.0f, 100000.0f)) {
		ctrl->SetJumpSpeed(jump);
		Editor::MarkSceneDirty();
	}

	float camDist = ctrl->GetCameraDistance();
	if (ImGui::DragFloat("Camera Distance", &camDist, 1.0f, 0.0f, 100000.0f)) {
		ctrl->SetCameraDistance(camDist);
		Editor::MarkSceneDirty();
	}
	float camHeight = ctrl->GetCameraHeight();
	if (ImGui::DragFloat("Camera Height", &camHeight, 1.0f, 0.0f, 100000.0f)) {
		ctrl->SetCameraHeight(camHeight);
		Editor::MarkSceneDirty();
	}
	float yawOffset = ctrl->GetModelYawOffset();
	if (ImGui::DragFloat("Model Yaw Offset (deg)", &yawOffset, 0.5f, -180.0f, 180.0f)) {
		ctrl->SetModelYawOffset(yawOffset);
		Editor::MarkSceneDirty();
	}

	auto clipRow = [ctrl](const char* label, const char* current, void(CharacterController::*setter)(const std::string&)) {
		char cbuf[256];
		strncpy(cbuf, current, sizeof(cbuf) - 1);
		cbuf[sizeof(cbuf) - 1] = '\0';
		if (ImGui::InputText(label, cbuf, sizeof(cbuf))) {
			(ctrl->*setter)(cbuf);
			Editor::MarkSceneDirty();
		}
	};
	clipRow("Idle Clip", ctrl->GetIdleClip().c_str(), &CharacterController::SetIdleClip);
	clipRow("Walk Clip", ctrl->GetWalkClip().c_str(), &CharacterController::SetWalkClip);
	clipRow("Run Clip", ctrl->GetRunClip().c_str(), &CharacterController::SetRunClip);
	clipRow("Jump Clip (air)", ctrl->GetJumpClip().c_str(), &CharacterController::SetJumpClip);
	ImGui::TextDisabled("Empty jump clip = no airborne anim. Crouch/interact/attack come with the data-driven action table.");
}

// SkyAtmosphere: TOD slider + sun binding + cloud/atmosphere params.
void RenderSkyAtmosphereBody(SceneNode* node, SkyAtmosphere* sky) {
	if (!sky) return;

	bool enabled = sky->GetEnabled();
	if (EditorUi::CheckboxRow("Enabled", &enabled)) {
		sky->SetEnabled(enabled);
		Editor::MarkSceneDirty();
	}

	float hours = sky->GetTimeHours();
	EditorUi::FieldRow("Time of Day", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::SliderFloat("##tod", &hours, 0.0f, 24.0f, "%.2f h")) {
			sky->SetTimeHours(hours);
			Editor::MarkSceneDirty();
		}
	});

	float dayLen = sky->GetDayLengthMinutes();
	EditorUi::FieldRow("Day Length (min)", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##daylen", &dayLen, 0.1f, 0.1f, 240.0f)) {
			sky->SetDayLengthMinutes(dayLen);
			Editor::MarkSceneDirty();
		}
	});

	bool autoAdv = sky->GetAutoAdvance();
	if (EditorUi::CheckboxRow("Auto Advance", &autoAdv)) {
		sky->SetAutoAdvance(autoAdv);
		Editor::MarkSceneDirty();
	}

	bool fromTod = sky->GetSunFromTod();
	if (EditorUi::CheckboxRow("Sun from TOD", &fromTod, "off: sky follows the light")) {
		sky->SetSunFromTod(fromTod);
		Editor::MarkSceneDirty();
	}

	char sunName[128];
	std::strncpy(sunName, sky->GetSunLightName().c_str(), sizeof(sunName) - 1);
	sunName[sizeof(sunName) - 1] = '\0';
	{
		// input + trailing label + Auto-Detect button, all inside the row width
		const float avail = ImGui::GetContentRegionAvail().x;
		const float spacing = ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().ItemInnerSpacing.x;
		const float labelW = ImGui::CalcTextSize("Sun Node").x;
		const float btnW = ImGui::CalcTextSize("Auto-Detect").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(std::max(80.0f, avail - labelW - btnW - spacing * 2.0f));
		if (ImGui::InputText("##sunnode", sunName, sizeof(sunName))) {
			sky->SetSunLightName(sunName);
			Editor::MarkSceneDirty();
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Sun Node");
		ImGui::SameLine();
		if (ImGui::Button("Auto-Detect")) {
			if (sky->AutoSelectSunLight())
				Editor::MarkSceneDirty();
		}
		EditorUi::HintTooltip("Bind the scene's first directional light as the sun");
	}

	// detail settings (clouds/moon/atmosphere coefficients) live in the
	// separate sky editor window
	if (node && EditorUi::ButtonRow("Open Sky Editor...")) {
		Editor::OpenSkyEditor(node->shared_from_this());
	}
}

// Terrain: heightmap asset, splat + 4 layers, chunk/LOD counts, rebuild.
void RenderTerrainBody(SceneNode* node, Terrain* terrain) {
	if (!terrain) return;

	// Heightmap asset row - same Change/jump/clear pattern as the mesh rows.
	const bool hasHm = !terrain->GetHeightmapName().empty();
	if (ImGui::Button("Change##heightmap"))
		ImGui::OpenPopup("TerrainHeightmapPicker");
	ImGui::SameLine();
	if (!hasHm) ImGui::BeginDisabled();
	if (ImGui::Button("->##heightmap"))
		Editor::SelectAssetInBrowser(typeid(Heightmap), terrain->GetHeightmapName());
	if (!hasHm) ImGui::EndDisabled();
	ImGui::SameLine();
	if (!hasHm) ImGui::BeginDisabled();
	if (ImGui::Button("x##heightmap")) {
		terrain->SetHeightmapName("");
		Editor::MarkSceneDirty();
	}
	if (!hasHm) ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted("Heightmap:");
	ImGui::SameLine();
	if (hasHm)
		ImGui::TextUnformatted(terrain->GetHeightmapName().c_str());
	else
		ImGui::TextDisabled("(none)");

	RenderAssetPickerModal("TerrainHeightmapPicker", "Pick Heightmap",
		typeid(Heightmap),
		[terrain](std::shared_ptr<void> p) {
			auto hm = std::static_pointer_cast<Heightmap>(p);
			terrain->SetHeightmapName(hm ? hm->GetName() : "");
			terrain->Rebuild();
			Editor::MarkSceneDirty();
		});

	if (terrain->HasHeights()) {
		ImGui::TextDisabled("%d x %d, %.0f x %.0f cm",
			terrain->GetResolution(), terrain->GetResolution(),
			terrain->GetWorldSizeX(), terrain->GetWorldSizeZ());
	} else {
		ImGui::TextDisabled("No heights loaded");
	}

	// layers / chunk+LOD / rebuild / probes live in the terrain editor window
	if (node && ImGui::Button("Open Terrain Editor...",
							  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		Editor::OpenTerrainEditor(node->shared_from_this());
	}
}

// OceanComponent: mode, wave asset, geometry, shading, foam.
void RenderOceanBody(SceneNode* node, OceanComponent* ocean) {
	if (!ocean) return;
	(void)node;

	int mode = (int)ocean->GetMode();
	EditorUi::FieldRow("Mode", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::Combo("##mode", &mode, "Finite (grid)\0Infinite (ring LOD)\0")) {
			ocean->SetMode((OceanComponent::Mode)mode);
			Editor::MarkSceneDirty();
		}
	});

	ImGui::TextDisabled("Wave Source: Baked asset");

	const char* srcNames[] = { "none (flat)", "baked asset" };
	int resolved = ocean->GetResolvedSource();
	int safeResolved = (resolved != 0) ? 1 : 0;
	ImGui::TextDisabled("Resolved: %s - %s",
		srcNames[safeResolved], ocean->GetResolvedReason().c_str());

	// Wave asset row: Change (picker) / -> (jump to browser) / x (unbind) +
	// name + a stats line - the Terrain heightmap-row precedent.
	{
		const bool hasWaves = !ocean->GetWaveAssetPath().empty();
		if (ImGui::Button("Change##waveasset"))
			ImGui::OpenPopup("OceanWavesPicker");
		ImGui::SameLine();
		if (!hasWaves) ImGui::BeginDisabled();
		if (ImGui::Button("->##waveasset"))
			Editor::SelectAssetInBrowser(typeid(OceanWaves), ocean->GetWaveAssetPath());
		if (!hasWaves) ImGui::EndDisabled();
		ImGui::SameLine();
		if (!hasWaves) ImGui::BeginDisabled();
		if (ImGui::Button("x##waveasset")) {
			ocean->SetWaveAssetPath("");
			Editor::MarkSceneDirty();
		}
		if (!hasWaves) ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextUnformatted("OceanWaves:");
		ImGui::SameLine();
		if (hasWaves)
			ImGui::TextUnformatted(ocean->GetWaveAssetPath().c_str());
		else
			ImGui::TextDisabled("(none)");

		RenderAssetPickerModal("OceanWavesPicker", "Pick OceanWaves (loaded)",
			typeid(OceanWaves),
			[ocean](std::shared_ptr<void> p) {
				auto waves = std::static_pointer_cast<OceanWaves>(p);
				ocean->SetWaveAssetPath(waves ? waves->GetFilePath() : "");
				Editor::MarkSceneDirty();
			});

		if (auto waves = ocean->GetWaves(); waves && waves->IsValid()) {
			if (waves->GetBandCount() >= 2)
				ImGui::TextWrapped("%d bands, %d frames x %.1fs, tiles %.0f/%.0f cm",
					waves->GetBandCount(), waves->GetFrameCount(), waves->GetLoopSeconds(),
					waves->GetBand(0).TileCm, waves->GetBand(1).TileCm);
			else
				ImGui::TextWrapped("%d band, %d frames x %.1fs, tile %.0f cm",
					waves->GetBandCount(), waves->GetFrameCount(), waves->GetLoopSeconds(),
					waves->GetBand(0).TileCm);
		} else {
			ImGui::TextDisabled("no waves resolved");
		}
	}

	float waterLevel = ocean->GetWaterLevel();
	EditorUi::FieldRow("Water Level (cm)", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##wl", &waterLevel, 1.0f, -100000.0f, 100000.0f)) {
			ocean->SetWaterLevel(waterLevel);
			Editor::MarkSceneDirty();
		}
	});

	// --- geometry ---------------------------------------------------------
	if (ocean->GetMode() == OceanComponent::Mode::Finite) {
		float size = ocean->GetFiniteSizeCm();
		EditorUi::FieldRow("Size (cm)", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragFloat("##fsize", &size, 10.0f, 100.0f, 1000000.0f)) {
				ocean->SetFiniteSizeCm(size);
				Editor::MarkSceneDirty();
			}
		});
		int res = ocean->GetFiniteResolution();
		EditorUi::FieldRow("Resolution", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragInt("##fres", &res, 1.0f, 8, 512)) {
				ocean->SetFiniteResolution(res);
				Editor::MarkSceneDirty();
			}
		});
	} else {
		float cell = ocean->GetRingCellSizeCm();
		EditorUi::FieldRow("Cell Size (cm)", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragFloat("##cell", &cell, 1.0f, 10.0f, 10000.0f)) {
				ocean->SetRingCellSizeCm(cell);
				Editor::MarkSceneDirty();
			}
		});
		int cells = ocean->GetRingCells();
		EditorUi::FieldRow("Center Cells", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragInt("##cells", &cells, 1.0f, 8, 256)) {
				ocean->SetRingCells(cells);
				Editor::MarkSceneDirty();
			}
		});
		int rings = ocean->GetRingCount();
		EditorUi::FieldRow("Ring Count", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragInt("##rings", &rings, 1.0f, 0, 5)) {
				ocean->SetRingCount(rings);
				Editor::MarkSceneDirty();
			}
		});
		float skirt = ocean->GetSkirtRadiusCm();
		EditorUi::FieldRow("Skirt Radius (cm)", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::DragFloat("##skirt", &skirt, 1000.0f, 10000.0f, 4000000.0f)) {
				ocean->SetSkirtRadiusCm(skirt);
				Editor::MarkSceneDirty();
			}
		});
		ImGui::TextDisabled("%u verts (budget 250k)", ocean->GetOceanVertexCount());
	}

	// --- shading ----------------------------------------------------------
	ImGui::SeparatorText("Shading");
	{
		Color absorb = ocean->GetAbsorbColor();
		float rgb[3] = { absorb.r, absorb.g, absorb.b };
		EditorUi::FieldRow("Absorb Color", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::ColorEdit3("##absorb", rgb)) {
				ocean->SetAbsorbColor(Color(rgb[0], rgb[1], rgb[2], 1.0f));
				Editor::MarkSceneDirty();
			}
		});
		Color scatter = ocean->GetScatterColor();
		float rgb2[3] = { scatter.r, scatter.g, scatter.b };
		EditorUi::FieldRow("Scatter Color", 220.0f, [&](float w) {
			ImGui::SetNextItemWidth(w);
			if (ImGui::ColorEdit3("##scatter", rgb2)) {
				ocean->SetScatterColor(Color(rgb2[0], rgb2[1], rgb2[2], 1.0f));
				Editor::MarkSceneDirty();
			}
		});
	}
	float roughness = ocean->GetRoughness();
	EditorUi::FieldRow("Roughness", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::SliderFloat("##rough", &roughness, 0.02f, 1.0f)) {
			ocean->SetRoughness(roughness);
			Editor::MarkSceneDirty();
		}
	});
	float nrmStr = ocean->GetNormalStrength();
	EditorUi::FieldRow("Normal Strength", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::SliderFloat("##nrmstr", &nrmStr, 0.0f, 3.0f)) {
			ocean->SetNormalStrength(nrmStr);
			Editor::MarkSceneDirty();
		}
	});
	float foam = ocean->GetFoamAmount();
	EditorUi::FieldRow("Foam Amount", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::SliderFloat("##foam", &foam, 0.0f, 3.0f)) {
			ocean->SetFoamAmount(foam);
			Editor::MarkSceneDirty();
		}
	});
	float shore = ocean->GetShoreFoamDepthCm();
	EditorUi::FieldRow("Shore Foam Depth (cm)", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##shore", &shore, 5.0f, 0.0f, 5000.0f)) {
			ocean->SetShoreFoamDepthCm(shore);
			Editor::MarkSceneDirty();
		}
	});
}

// BuoyancyComponent: float-point list editor + coefficients (change:
// add-fft-ocean). Forces tick in play mode only.
void RenderBuoyancyBody(SceneNode* node, BuoyancyComponent* buoyancy) {
	if (!buoyancy) return;

	// sibling BodySetup must exist and be dynamic
	auto body = node ? node->GetComponent<BodySetup>() : nullptr;
	if (!body) {
		ImGui::TextWrapped("Requires a dynamic BodySetup on the same node.");
	} else if (body->GetMotionType() != BodySetup::MotionType::Dynamic) {
		ImGui::TextWrapped("BodySetup is not dynamic - buoyancy will not act.");
	}

	ImGui::TextDisabled("Float points (node-local, cm):");
	for (unsigned int i = 0; i < buoyancy->GetFloatPointCount(); ++i) {
		ImGui::PushID(i);
		auto point = buoyancy->GetFloatPoint(i);
		float off[3] = { point.Offset.x, point.Offset.y, point.Offset.z };
		float radius = point.Radius;
		// offset group + radius + remove button share one row's width
		const float avail = ImGui::GetContentRegionAvail().x;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float btnW = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		const float radW = 70.0f;
		ImGui::SetNextItemWidth(std::max(120.0f, avail - btnW - radW - spacing * 2.0f));
		bool changed = ImGui::DragFloat3("##offset", off, 1.0f, -10000.0f, 10000.0f);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(radW);
		changed |= ImGui::DragFloat("##radius", &radius, 1.0f, 1.0f, 1000.0f, "r=%.0f");
		ImGui::SameLine();
		if (ImGui::Button("x")) {
			buoyancy->RemoveFloatPoint(i);
			Editor::MarkSceneDirty();
			ImGui::PopID();
			break;
		}
		if (changed) {
			buoyancy->SetFloatPoint(i, Vector4(off[0], off[1], off[2], 1.0f), radius);
			Editor::MarkSceneDirty();
		}
		ImGui::PopID();
	}
	if (EditorUi::ButtonRow("Add Float Point")) {
		buoyancy->AddFloatPoint(Vector4(0.0f, 0.0f, 0.0f, 1.0f), 25.0f);
		Editor::MarkSceneDirty();
	}

	float density = buoyancy->GetWaterDensity();
	EditorUi::FieldRow("Water Density", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##wd", &density, 0.05f, 0.01f, 100.0f)) {
			buoyancy->SetWaterDensity(density);
			Editor::MarkSceneDirty();
		}
	}, "1.0 = neutral at full submersion; ~2 floats half-submerged");

	float lin = buoyancy->GetLinearDrag();
	EditorUi::FieldRow("Linear Drag", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##ld", &lin, 0.05f, 0.0f, 20.0f)) {
			buoyancy->SetLinearDrag(lin);
			Editor::MarkSceneDirty();
		}
	});

	float ang = buoyancy->GetAngularDrag();
	EditorUi::FieldRow("Angular Drag", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##ad", &ang, 0.05f, 0.0f, 20.0f)) {
			buoyancy->SetAngularDrag(ang);
			Editor::MarkSceneDirty();
		}
	});

	float righting = buoyancy->GetRightingStrength();
	EditorUi::FieldRow("Righting Strength", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::DragFloat("##rs", &righting, 0.1f, 0.0f, 100.0f)) {
			buoyancy->SetRightingStrength(righting);
			Editor::MarkSceneDirty();
		}
	}, "must dominate the buoyancy pendulum or a capsized body stays inverted");

	char oceanName[128];
	std::strncpy(oceanName, buoyancy->GetOceanNodeName().c_str(), sizeof(oceanName) - 1);
	oceanName[sizeof(oceanName) - 1] = '\0';
	EditorUi::FieldRow("Ocean Node", 220.0f, [&](float w) {
		ImGui::SetNextItemWidth(w);
		if (ImGui::InputText("##oceanname", oceanName, sizeof(oceanName))) {
			buoyancy->SetOceanNodeName(oceanName);
			Editor::MarkSceneDirty();
		}
	});

	// Float-point markers: Debug views dropdown -> "Buoyancy Float Points".
	ImGui::TextWrapped("Forces tick in play mode only (PhysicsWorld pre-step).");
}

static const std::vector<ComponentEntry>& ComponentRenderTable() {
	static const std::vector<ComponentEntry> table = {
		{"Transform", typeid(Transform), false, [](SceneNode* n, Component* c) { RenderTransformBody(n, static_cast<Transform*>(c)); }},
		{"Light", typeid(Light), true, [](SceneNode* n, Component* c) { RenderLightBody(n, static_cast<Light*>(c)); }},
		{"Camera", typeid(Camera), true, [](SceneNode* n, Component* c) { RenderCameraBody(n, static_cast<Camera*>(c)); }},
		{"MeshRender", typeid(MeshRender), true, [](SceneNode* n, Component* c) { RenderMeshRenderBody(n, static_cast<MeshRender*>(c)); }},
		{"Animator", typeid(Animator), true, [](SceneNode* n, Component* c) { RenderAnimatorBody(n, static_cast<Animator*>(c)); }},
		{"ParticleRenderer", typeid(ParticleRenderer), true, [](SceneNode* n, Component* c) { RenderParticleRendererBody(n, static_cast<ParticleRenderer*>(c)); }},
		{"BodySetup", typeid(BodySetup), true, [](SceneNode* n, Component* c) { RenderBodySetupBody(n, static_cast<BodySetup*>(c)); }},
		{"FreeFlyController", typeid(FreeFlyController), true, [](SceneNode* n, Component* c) { RenderFreeFlyControllerBody(n, static_cast<FreeFlyController*>(c)); }},
		{"CharacterController", typeid(CharacterController), true, [](SceneNode* n, Component* c) { RenderCharacterControllerBody(n, static_cast<CharacterController*>(c)); }},
		{"SkyAtmosphere", typeid(SkyAtmosphere), true, [](SceneNode* n, Component* c) { RenderSkyAtmosphereBody(n, static_cast<SkyAtmosphere*>(c)); }},
		{"Terrain", typeid(Terrain), true, [](SceneNode* n, Component* c) { RenderTerrainBody(n, static_cast<Terrain*>(c)); }},
		{"OceanComponent", typeid(OceanComponent), true, [](SceneNode* n, Component* c) { RenderOceanBody(n, static_cast<OceanComponent*>(c)); }},
		{"BuoyancyComponent", typeid(BuoyancyComponent), true, [](SceneNode* n, Component* c) { RenderBuoyancyBody(n, static_cast<BuoyancyComponent*>(c)); }},
		{"InstancedMeshRender", typeid(InstancedMeshRender), true, [](SceneNode* n, Component* c) { RenderInstancedMeshRenderBody(n, static_cast<InstancedMeshRender*>(c)); }},
	};
	return table;
}

// Render one component as a top-level CollapsingHeader with a
// full-width delete button at the bottom (skipped for Transform).
void RenderComponentSection(SceneNode* node, const ComponentEntry& entry) {
	// The Transform section is redundant with the Node section above
	// (both show the same TRS) for nodes that carry a MeshRender.
	if (entry.type == typeid(Transform) && node->GetComponent<MeshRender>())
		return;
	auto comp = node->GetComponent(entry.type);
	if (!comp) return;

	ImGui::PushID(entry.name.c_str());
	// Body + delete button both inside `if (open)` so the delete
	// button hides when the section is collapsed.
	if (ImGui::CollapsingHeader(entry.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		entry.render(node, comp.get());
		if (entry.removable) {
			if (ImGui::Button(("Delete " + entry.name).c_str(),
							  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				node->RemoveComponent(entry.type);
				Editor::MarkSceneDirty();
			}
		}
	}
	ImGui::PopID();
}

// Full-width "+ Add Component" button. Lists every registry entry
// except Transform; skips entries the node already has.
void RenderAddComponentButton(SceneNode* node) {
	if (ImGui::Button("+ Add Component",
					  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		ImGui::OpenPopup("AddComponentPopup");
	}
	if (ImGui::BeginPopup("AddComponentPopup")) {
		for (const auto& entry : ComponentRenderTable()) {
			if (!entry.removable) continue;
			if (node->GetComponent(entry.type)) continue;
			if (ImGui::MenuItem(entry.name.c_str())) {
				auto it = SceneNode::ComponentRegistry.find(entry.name);
				if (it != SceneNode::ComponentRegistry.end()) {
					auto comp = it->second();
					node->AddComponent(comp);
					Editor::MarkSceneDirty();
				}
			}
		}
		ImGui::EndPopup();
	}
}
} // namespace

// Render a material texture row. The thumbnail itself is
// clickable -- clicking it opens the texture picker. For
// slots with no texture, render a pure black rect that is
// also clickable (opens the picker so the user can assign
// one).
void RenderMaterialTextureRow(Material* mat, const std::string& key) {
	if (!mat) return;
	auto tex = mat->GetTexture(key);

	// Clickable thumbnail area. We render an InvisibleButton
	// over the 48x48 thumbnail rect so clicking it opens the
	// texture picker. For null textures, we draw a black rect
	// first so the user sees a clickable target.
	ImVec2 thumb_min = ImGui::GetCursorScreenPos();
	if (tex) {
		ImGui::Image((ImTextureID)(intptr_t)tex->GetID(),
					 ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
	} else {
		// Pure black rect for empty slots.
		ImGui::GetWindowDrawList()->AddRectFilled(thumb_min,
												  ImVec2(thumb_min.x + 48, thumb_min.y + 48),
												  ImGui::GetColorU32(ImVec4(0, 0, 0, 1)));
		ImGui::Dummy(ImVec2(48, 48));
	}

	// Invisible button over the thumbnail to capture clicks.
	ImGui::SetCursorScreenPos(thumb_min);
	ImGui::InvisibleButton(("##tex_hit_" + key).c_str(), ImVec2(48, 48));
	if (ImGui::IsItemClicked(0)) {
		std::string popup = "TexturePicker_" + key;
		ImGui::OpenPopup(popup.c_str());
	}

	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::Text("%s", key.c_str());
	if (tex) {
		ImGui::Text("%s  %d x %d  %s  %s",
					tex->GetName().empty() ? "(unnamed)" : tex->GetName().c_str(),
					tex->GetWidth(), tex->GetHeight(),
					EnumUtil::TextureFormatToString(tex->GetFormat()).c_str(),
					tex->IsSRGB() ? "sRGB" : "linear");
	} else {
		ImGui::TextDisabled("(none)");
	}
	ImGui::EndGroup();

	// Texture picker modal (opened by clicking the thumbnail).
	std::string popup = "TexturePicker_" + key;
	std::string title = "Pick Texture (" + key + ")";
	RenderAssetPickerModal(popup.c_str(), title.c_str(), typeid(Texture),
						   [mat, key](std::shared_ptr<void> p) {
							   auto t = std::static_pointer_cast<Texture>(p);
							   mat->SetTexture(key, t);
						   });
}

void RenderNodePropertiesWindow(bool* open) {
	ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Node Properties", open)) {
		ImGui::End();
		return;
	}

	// Header: which scene file is being edited. Empty path -> "(no
	// scene)" hint. Non-native source ("[imported] tank.fbx") tells
	// the user Save will route to Save As. Hover for full path.
	{
		const std::string path = Editor::GetCurrentScenePath();
		std::string head;
		if (path.empty()) {
			head = "Scene: (none)";
		} else {
			std::filesystem::path p(path);
			const std::string base = p.filename().string();
			const std::string ext = p.extension().string();
			std::string ext_lower;
			ext_lower.reserve(ext.size());
			for (char c : ext) ext_lower.push_back((char)std::tolower((unsigned char)c));
			const bool native = (ext_lower == ".json" || ext_lower == ".bin");
			head = native ? ("Scene: " + base) : ("Scene: [imported] " + base);
		}
		ImGui::TextDisabled("%s", head.c_str());
		if (!path.empty() && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", path.c_str());
		}
		ImGui::Separator();
	}

	// Dangling-pointer walk: if the selected node was removed from the
	// active scene since selection, drop the stale pointer.
	if (g_SelectedSceneNode && Scene::Active) {
		if (!IsReachable(Scene::Active->GetRootNode(), g_SelectedSceneNode)) {
			SetSelectedSceneNode(nullptr);
		}
	} else if (g_SelectedSceneNode && !Scene::Active) {
		SetSelectedSceneNode(nullptr);
	}

	if (g_SelectedSceneNode == nullptr) {
		ImGui::TextDisabled("(no node selected)");
		ImGui::End();
		return;
	}

	SceneNode* node = g_SelectedSceneNode;
	RenderSceneNodeSection(node);

	// One section per component, in registration order. No
	// enclosing "Components" header -- each is a peer in the panel.
	for (const auto& entry : ComponentRenderTable()) {
		RenderComponentSection(node, entry);
	}

	RenderAddComponentButton(node);

	ImGui::End();
}
} // namespace Editor
} // namespace fury

#endif // WITH_EDITOR
