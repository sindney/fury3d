#include "Fury/Editor/EditorAssetPicker.h"

#include "Fury/AnimationClip.h"
#include "Fury/EntityManager.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/Scene.h"
#include "Fury/Texture.h"
#include "ImGui/imgui.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace fury {
namespace Editor {
namespace {
struct PickerEntry {
	std::string label;
	std::shared_ptr<void> ptr;
};

// Per-popup-id selection state so multiple pickers can be
// open simultaneously without aliasing. -1 means "nothing
// selected".
int& SelectedIndexFor(const char* popup_id) {
	static std::unordered_map<std::string, int> state;
	auto it = state.find(popup_id);
	if (it == state.end())
		it = state.emplace(popup_id, -1).first;
	return it->second;
}

void CollectMeshes(std::vector<PickerEntry>& out) {
	if (!Scene::Active) return;
	auto em = Scene::Active->GetEntityManager();
	if (!em) return;
	em->ForEach<Mesh>([&](const std::shared_ptr<Mesh>& m) {
		unsigned int totalVerts =
			static_cast<unsigned int>(m->Positions.Data.size() / 3);
		unsigned int totalIndices = 0;
		for (unsigned int i = 0; i < m->GetSubMeshCount(); ++i) {
			auto sm = m->GetSubMeshAt(i);
			if (sm) totalIndices +=
					static_cast<unsigned int>(sm->Indices.Data.size());
		}
		char buf[300];
		std::snprintf(buf, sizeof(buf), "%s    %u v  ·  %u t",
					  m->GetName().c_str(), totalVerts, totalIndices / 3);
		out.push_back({buf, std::static_pointer_cast<void>(m)});
		return true;
	});
}

void CollectMaterials(std::vector<PickerEntry>& out) {
	if (!Scene::Active) return;
	auto em = Scene::Active->GetEntityManager();
	if (!em) return;
	em->ForEach<Material>([&](const std::shared_ptr<Material>& m) {
		out.push_back({m->GetName(),
					   std::static_pointer_cast<void>(m)});
		return true;
	});
}

void CollectTextures(std::vector<PickerEntry>& out) {
	// Textures are now first-class assets registered in the
	// EntityManager (added by Scene::Load and GltfImporter). So we
	// just iterate em->ForEach<Texture>.
	if (!Scene::Active) return;
	auto em = Scene::Active->GetEntityManager();
	if (!em) return;
	em->ForEach<Texture>([&](const std::shared_ptr<Texture>& tex) {
		out.push_back({tex->GetName(), std::static_pointer_cast<void>(tex)});
		return true;
	});
}

void CollectAnimationClipsIntoEntries(std::vector<PickerEntry>& out) {
	for (const auto& c : CollectAnimationClips()) {
		// Show duration in seconds alongside the name so clips with
		// cryptic FBX-exported names (e.g. "James|Walk") are easier
		// to tell apart.
		char buf[300];
		std::snprintf(buf, sizeof(buf), "%s    %.2fs",
					  c->GetName().c_str(),
					  c->GetDuration() / c->GetTicksPerSecond());
		out.push_back({buf, std::static_pointer_cast<void>(c)});
	}
}

// Postprocess effects live in the process-global registry (not the
// active scene's EntityManager — see PostProcessRegistry::LoadFromDirectory),
// so the picker can't reuse the em->ForEach path. Iterate the registry
// directly and surface them through the same std::shared_ptr<void> shape
// the picker expects.
void CollectPostProcessEffects(std::vector<PickerEntry>& out) {
	for (auto &effect : PostProcessRegistry::GetAll()) {
		if (!effect) continue;
		out.push_back({effect->GetName(),
					   std::static_pointer_cast<void>(effect)});
	}
}

void CollectByType(std::type_index type,
				   std::vector<PickerEntry>& out) {
	if (type == typeid(Mesh))
		CollectMeshes(out);
	else if (type == typeid(Material))
		CollectMaterials(out);
	else if (type == typeid(Texture))
		CollectTextures(out);
	else if (type == typeid(AnimationClip))
		CollectAnimationClipsIntoEntries(out);
	else if (type == typeid(PostProcessEffect))
		CollectPostProcessEffects(out);
	// Unknown type: leave out empty — the OK button stays
	// disabled and the user can only Cancel.
}
} // namespace

std::vector<std::shared_ptr<AnimationClip>> CollectAnimationClips() {
	std::vector<std::shared_ptr<AnimationClip>> out;
	if (!Scene::Active) return out;
	auto em = Scene::Active->GetEntityManager();
	if (!em) return out;
	em->ForEach<AnimationClip>([&](const std::shared_ptr<AnimationClip>& c) {
		if (c) out.push_back(c);
		return true;
	});
	return out;
}

void RenderAssetPickerModal(const char* popup_id, const char* title,
							std::type_index type,
							std::function<void(std::shared_ptr<void>)> onPick) {
	int& selectedIndex = SelectedIndexFor(popup_id);

	if (ImGui::BeginPopupModal(popup_id, nullptr,
							   ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(title);
		ImGui::Separator();

		// "Set to None" button — calls onPick(nullptr) so the caller
		// can clear the slot. Visible for all types.
		if (ImGui::Button("Set to None")) {
			if (onPick)
				try {
					onPick(nullptr);
				} catch (...) {}
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();

		std::vector<PickerEntry> entries;
		CollectByType(type, entries);

		ImGui::BeginChild(("##picker_" + std::string(popup_id)).c_str(),
						  ImVec2(360, 240), true);
		for (int i = 0; i < (int)entries.size(); ++i) {
			ImGui::PushID(i);

			// Capture row top-left for hit rect + highlight.
			ImVec2 row_min = ImGui::GetCursorScreenPos();
			float row_w = ImGui::GetContentRegionAvail().x;
			float row_h = ImGui::GetTextLineHeightWithSpacing();
			if (type == typeid(Texture))
				row_h = std::max(row_h, 48.0f);

			bool selected = (selectedIndex == i);

			// Selection fill (behind content).
			if (selected) {
				ImGui::GetWindowDrawList()->AddRectFilled(
					row_min, ImVec2(row_min.x + row_w, row_min.y + row_h),
					ImGui::GetColorU32(ImGuiCol_Header));
			}

			// Render content: thumbnail (Texture only) + label.
			if (type == typeid(Texture)) {
				auto tex = std::static_pointer_cast<Texture>(entries[i].ptr);
				if (tex) {
					ImGui::Image((ImTextureID)(intptr_t)tex->GetID(),
								 ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
					ImGui::SameLine();
				}
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(entries[i].label.c_str());

			// Full-row InvisibleButton for click handling.
			ImGui::SetCursorScreenPos(row_min);
			ImGui::InvisibleButton("##row_hit", ImVec2(row_w, row_h));
			if (ImGui::IsItemClicked(0))
				selectedIndex = i;
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				if (onPick)
					try {
						onPick(entries[i].ptr);
					} catch (...) {}
				ImGui::CloseCurrentPopup();
			}

			// Selection border (on top).
			if (selected) {
				ImGui::GetWindowDrawList()->AddRect(
					row_min, ImVec2(row_min.x + row_w, row_min.y + row_h),
					ImGui::GetColorU32(ImGuiCol_HeaderActive),
					0.0f, 0, 2.0f);
			}

			// Advance cursor past the row.
			ImGui::SetCursorScreenPos(ImVec2(row_min.x, row_min.y + row_h));
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::Separator();
		const bool can_confirm =
			(selectedIndex >= 0 && selectedIndex < (int)entries.size());

		if (!can_confirm) ImGui::BeginDisabled();
		if (ImGui::Button("OK", ImVec2(120, 0))) {
			if (can_confirm && onPick) {
				try {
					onPick(entries[selectedIndex].ptr);
				} catch (...) {}
			}
			ImGui::CloseCurrentPopup();
		}
		if (!can_confirm) ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
} // namespace Editor
} // namespace fury
