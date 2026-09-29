#if WITH_EDITOR

#include "Fury/Editor/EditorSceneNodePicker.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Fury/AnimationPlayer.h"
#include "Fury/BodySetup.h"
#include "Fury/BuoyancyComponent.h"
#include "Fury/Camera.h"
#include "Fury/CharacterController.h"
#include "Fury/Component.h"
#include "Fury/InstancedMeshRender.h"
#include "Fury/Light.h"
#include "Fury/MeshRender.h"
#include "Fury/OceanComponent.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/PlayerController.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/Terrain.h"
#include "Fury/Transform.h"
#include "ImGui/imgui.h"

namespace fury {
namespace Editor {
namespace {

struct CompInfo {
	std::type_index tid;
	const char* name;
};

// Known component types the picker can match against / label in the details
// pane. SceneNode::GetComponent<T> is template-only, so we enumerate the set
// explicitly. `typeid(void)` is the "no filter" sentinel.
static const CompInfo kKnownComps[] = {
	{typeid(Transform),           "Transform"},
	{typeid(Camera),              "Camera"},
	{typeid(Light),               "Light"},
	{typeid(MeshRender),          "MeshRender"},
	{typeid(InstancedMeshRender), "InstancedMeshRender"},
	{typeid(Animator),            "Animator"},
	{typeid(ParticleRenderer),    "ParticleRenderer"},
	{typeid(BodySetup),           "BodySetup"},
	{typeid(FreeFlyController),   "FreeFlyController"},
	{typeid(CharacterController), "CharacterController"},
	{typeid(SkyAtmosphere),       "SkyAtmosphere"},
	{typeid(Terrain),             "Terrain"},
	{typeid(OceanComponent),      "OceanComponent"},
	{typeid(BuoyancyComponent),   "BuoyancyComponent"},
};

bool HasComp(const SceneNode& node, std::type_index tid) {
	if (tid == typeid(Transform))           return node.GetComponent<Transform>()           != nullptr;
	if (tid == typeid(Camera))              return node.GetComponent<Camera>()              != nullptr;
	if (tid == typeid(Light))               return node.GetComponent<Light>()               != nullptr;
	if (tid == typeid(MeshRender))          return node.GetComponent<MeshRender>()          != nullptr;
	if (tid == typeid(InstancedMeshRender)) return node.GetComponent<InstancedMeshRender>() != nullptr;
	if (tid == typeid(Animator))            return node.GetComponent<Animator>()            != nullptr;
	if (tid == typeid(ParticleRenderer))    return node.GetComponent<ParticleRenderer>()    != nullptr;
	if (tid == typeid(BodySetup))           return node.GetComponent<BodySetup>()           != nullptr;
	if (tid == typeid(FreeFlyController))   return node.GetComponent<FreeFlyController>()   != nullptr;
	if (tid == typeid(CharacterController)) return node.GetComponent<CharacterController>() != nullptr;
	if (tid == typeid(SkyAtmosphere))       return node.GetComponent<SkyAtmosphere>()       != nullptr;
	if (tid == typeid(Terrain))             return node.GetComponent<Terrain>()             != nullptr;
	if (tid == typeid(OceanComponent))      return node.GetComponent<OceanComponent>()      != nullptr;
	if (tid == typeid(BuoyancyComponent))   return node.GetComponent<BuoyancyComponent>()   != nullptr;
	return false;
}

bool NodeMatchesType(const SceneNode& node, std::type_index req) {
	if (req == typeid(void)) return true;
	for (const auto& c : kKnownComps)
		if (c.tid == req) return HasComp(node, req);
	return false;
}

void CollectMatchingDescendants(const std::shared_ptr<SceneNode>& node,
								 std::type_index req,
								 std::vector<std::shared_ptr<SceneNode>>& out) {
	if (!node) return;
	if (NodeMatchesType(*node, req))
		out.push_back(node);
	for (unsigned int i = 0; i < node->GetChildCount(); ++i)
		CollectMatchingDescendants(node->GetChildAt(i), req, out);
}

// Per-popup-id state. Distinct pickers (sun / ocean / camera) MUST use
// distinct popup ids so this map doesn't alias.
struct PickerState {
	int selectedIndex = -1; // index into the flat selectable list (-1 = none)
	char searchBuf[128] = {};
	std::unordered_map<SceneNode*, bool> expanded;
};

PickerState& StateFor(const char* popup_id) {
	static std::unordered_map<std::string, PickerState> state;
	return state[popup_id];
}

// Linear walk returning "/Root/.../Name".
std::string BuildPath(const std::shared_ptr<SceneNode>& node) {
	std::vector<std::string> parts;
	auto cur = node;
	while (cur) {
		parts.push_back(cur->GetName().empty() ? std::string("(unnamed)") : cur->GetName());
		cur = cur->GetParent();
	}
	std::string out;
	for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
		if (!out.empty()) out += "/";
		out += *it;
	}
	return out;
}

std::string ComponentListString(const std::shared_ptr<SceneNode>& node) {
	if (!node) return {};
	std::string out;
	for (const auto& c : kKnownComps) {
		if (!HasComp(*node, c.tid)) continue;
		if (!out.empty()) out += ", ";
		out += c.name;
	}
	return out;
}

std::string Lower(const std::string& s) {
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
				   [](unsigned char c) { return std::tolower(c); });
	return out;
}

bool ContainsIC(const std::string& haystack, const std::string& needleLower) {
	if (needleLower.empty()) return true;
	auto it = std::search(haystack.begin(), haystack.end(),
						  needleLower.begin(), needleLower.end(),
						  [](char a, char b) {
							  return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
						  });
	return it != haystack.end();
}

bool NodeOrDescendantMatches(const std::shared_ptr<SceneNode>& node,
							  const std::unordered_set<SceneNode*>& matches) {
	if (!node) return false;
	if (matches.count(node.get())) return true;
	for (unsigned int i = 0; i < node->GetChildCount(); ++i)
		if (NodeOrDescendantMatches(node->GetChildAt(i), matches)) return true;
	return false;
}

struct RenderCtx {
	const std::unordered_set<SceneNode*>* matches;
	const std::vector<std::shared_ptr<SceneNode>>* selectable;
	const std::unordered_map<int, int>* selectableIndex; // node raw ptr -> index in selectable
	std::string searchLower;
	PickerState* state;
	int depth;
};

bool NodePassesSearch(const std::shared_ptr<SceneNode>& node, const std::string& lower) {
	if (lower.empty()) return true;
	return Lower(node->GetName()).find(lower) != std::string::npos;
}

// Render a tree row and recurse. Skips rows that fail the search filter
// (and have no descendant that matches). Auto-expands ancestors of any
// matching row when the search is active.
void DrawTreeRow(const std::shared_ptr<SceneNode>& node, RenderCtx& ctx) {
	if (!node) return;

	bool isMatch = ctx.matches->count(node.get()) != 0;
	bool hasMatchingDescendant = NodeOrDescendantMatches(node, *ctx.matches);

	// Search filter: drop rows that don't match by name and have no
	// matching descendant. (When search is empty, every row passes.)
	if (!ctx.searchLower.empty() &&
		!NodePassesSearch(node, ctx.searchLower) &&
		!hasMatchingDescendant) {
		return;
	}

	// A row has visible children if at least one child (recursively)
	// contains a match. Recurse without drawing if so.
	bool hasVisibleChildren = false;
	if (hasMatchingDescendant) {
		for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
			auto c = node->GetChildAt(i);
			if (!c) continue;
			// Quick check: skip child if it and its descendants have no match.
			if (NodeOrDescendantMatches(c, *ctx.matches))
				hasVisibleChildren = true;
		}
	}

	std::string label = node->GetName().empty() ? "(unnamed)" : node->GetName();
	std::string hiddenId = std::string("##sn_") + std::to_string((intptr_t)node.get());

	ImGui::PushID(node.get());

	int flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (!hasVisibleChildren) flags |= ImGuiTreeNodeFlags_Leaf;
	auto sit = ctx.selectableIndex->find((intptr_t)node.get());
	if (isMatch && sit != ctx.selectableIndex->end()) {
		if (sit->second == ctx.state->selectedIndex)
			flags |= ImGuiTreeNodeFlags_Selected;
	}

	// Auto-expand: top level by default; ancestors of matches when filtering.
	auto expIt = ctx.state->expanded.find(node.get());
	bool defaultOpen = (ctx.depth <= 1);
	bool autoExpand = !ctx.searchLower.empty() && hasMatchingDescendant;
	if (expIt == ctx.state->expanded.end())
		ctx.state->expanded[node.get()] = defaultOpen || autoExpand;
	else if (autoExpand)
		ctx.state->expanded[node.get()] = true;

	bool nodeOpen = ImGui::TreeNodeEx(hiddenId.c_str(), flags, "%s", label.c_str());
	ctx.state->expanded[node.get()] = nodeOpen;

	// Click selects (only for matching nodes).
	if (ImGui::IsItemClicked(0) && isMatch && sit != ctx.selectableIndex->end())
		ctx.state->selectedIndex = sit->second;

	// TreeNode/TreePop must be balanced regardless of nodeOpen (ImGui pushes
	// an ID for every TreeNode and pops it only via TreePop).
	if (nodeOpen && hasVisibleChildren) {
		for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
			auto c = node->GetChildAt(i);
			if (!c) continue;
			if (NodeOrDescendantMatches(c, *ctx.matches))
				DrawTreeRow(c, ctx);
		}
	}
	ImGui::TreePop();

	ImGui::PopID();
}

} // namespace

void RenderSceneNodePickerModal(const char* popup_id, const char* title,
								 std::type_index requiredComponent,
								 std::function<void(std::shared_ptr<SceneNode>)> onPick) {
	PickerState& state = StateFor(popup_id);

	ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::BeginPopupModal(popup_id, nullptr)) return;

	ImGui::TextUnformatted(title);
	ImGui::Separator();

	// "Set to None" -- clears the slot, closes the popup.
	if (ImGui::Button("Set to None")) {
		if (onPick) {
			try { onPick(nullptr); } catch (...) {}
		}
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(220.0f);
	ImGui::InputTextWithHint("##sn_search", "Search...", state.searchBuf, sizeof(state.searchBuf));

	ImGui::Separator();

	// Collect strict-self-match nodes from the active scene (root excluded).
	std::vector<std::shared_ptr<SceneNode>> matches;
	if (Scene::Active) {
		auto root = Scene::Active->GetRootNode();
		if (root) {
			for (unsigned int i = 0; i < root->GetChildCount(); ++i)
				CollectMatchingDescendants(root->GetChildAt(i), requiredComponent, matches);
		}
	}
	std::unordered_set<SceneNode*> matchSet;
	matchSet.reserve(matches.size());
	for (const auto& n : matches) matchSet.insert(n.get());

	// Build the selectable list = matches passing the search filter.
	std::string searchLower = Lower(state.searchBuf);
	std::vector<std::shared_ptr<SceneNode>> selectable;
	selectable.reserve(matches.size());
	for (const auto& n : matches) {
		if (!searchLower.empty() && !ContainsIC(n->GetName(), searchLower)) continue;
		selectable.push_back(n);
	}

	// Map SceneNode* -> index in `selectable` (for click-time selection).
	std::unordered_map<int, int> selectableIndex;
	selectableIndex.reserve(selectable.size());
	for (int i = 0; i < (int)selectable.size(); ++i)
		selectableIndex.emplace((intptr_t)selectable[i].get(), i);

	if (state.selectedIndex >= (int)selectable.size())
		state.selectedIndex = selectable.empty() ? -1 : 0;

	// Two-pane layout: tree on left, details on right.
	ImGui::BeginChild((std::string("##snpick_tree_") + popup_id).c_str(),
					  ImVec2(360, 280), true);
	if (Scene::Active) {
		auto root = Scene::Active->GetRootNode();
		if (root) {
			RenderCtx ctx;
			ctx.matches = &matchSet;
			ctx.selectable = &selectable;
			ctx.selectableIndex = &selectableIndex;
			ctx.searchLower = searchLower;
			ctx.state = &state;
			ctx.depth = 0;
			// Don't render the SceneRoot itself.
			for (unsigned int i = 0; i < root->GetChildCount(); ++i) {
				auto child = root->GetChildAt(i);
				if (!child) continue;
				if (!NodeOrDescendantMatches(child, matchSet)) continue;
				RenderCtx childCtx = ctx;
				childCtx.depth = 1;
				DrawTreeRow(child, childCtx);
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild((std::string("##snpick_details_") + popup_id).c_str(),
					  ImVec2(200, 280), true);
	std::shared_ptr<SceneNode> selNode;
	if (state.selectedIndex >= 0 && state.selectedIndex < (int)selectable.size())
		selNode = selectable[state.selectedIndex];
	if (selNode) {
		ImGui::Text("Name");
		ImGui::TextWrapped("%s", selNode->GetName().c_str());
		ImGui::Separator();
		ImGui::Text("Path");
		ImGui::TextWrapped("%s", BuildPath(selNode).c_str());
		ImGui::Separator();
		ImGui::Text("Components");
		std::string comps = ComponentListString(selNode);
		if (comps.empty()) ImGui::TextDisabled("(none)");
		else ImGui::TextWrapped("%s", comps.c_str());
	} else {
		ImGui::TextDisabled("Select a node to see details.");
	}
	ImGui::EndChild();

	ImGui::Separator();

	// Keyboard nav over the selectable list.
	if (!selectable.empty()) {
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			if (state.selectedIndex > 0) state.selectedIndex--;
		} else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			if (state.selectedIndex < (int)selectable.size() - 1)
				state.selectedIndex++;
		} else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
			state.selectedIndex = 0;
		} else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
			state.selectedIndex = (int)selectable.size() - 1;
		} else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
			if (state.selectedIndex >= 0)
				state.expanded[selectable[state.selectedIndex].get()] = false;
		} else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
			if (state.selectedIndex >= 0)
				state.expanded[selectable[state.selectedIndex].get()] = true;
		} else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
			if (onPick) {
				try { onPick(selectable[state.selectedIndex]); } catch (...) {}
			}
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
	}

	const bool can_confirm = (state.selectedIndex >= 0 &&
							  state.selectedIndex < (int)selectable.size());
	if (!can_confirm) ImGui::BeginDisabled();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		if (can_confirm && onPick) {
			try { onPick(selectable[state.selectedIndex]); } catch (...) {}
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

void RenderSceneNodePickerModal(const char* popup_id, const char* title,
								 std::function<void(std::shared_ptr<SceneNode>)> onPick) {
	RenderSceneNodePickerModal(popup_id, title, typeid(void), std::move(onPick));
}

} // namespace Editor
} // namespace fury

#endif // WITH_EDITOR
