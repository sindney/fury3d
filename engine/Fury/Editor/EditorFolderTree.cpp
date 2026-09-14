#include "Fury/Editor/EditorFolderTree.h"

#include <cstring>
#include <set>
#include <vector>

#include "Fury/AnimationClip.h"
#include "Fury/EntityManager.h"
#include "Fury/EntityUtil.h"
#include "Fury/Heightmap.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/OceanWaves.h"
#include "Fury/ParticleSystem.h"
#include "Fury/Scene.h"
#include "Fury/Texture.h"
#include "Fury/Editor/Editor.h"
#include "ImGui/imgui.h"

namespace fury {
namespace Editor {
namespace {

// Legacy relative escape for engine resources seen from a project dir.
constexpr const char* kEngineEscape = "../../Resource/";

void RegisterPathFolders(std::set<std::string>& folders, const std::string& path) {
	if (path.empty()) return;
	std::string root, sub;
	SplitAssetPathRoot(path, root, sub);
	// every ancestor folder of the asset, root-inclusive
	folders.insert(root);
	for (std::string dir = PathDirname(sub); !dir.empty();) {
		const std::string d = dir.substr(0, dir.size() - 1); // strip trailing slash
		folders.insert(root + "/" + d);
		dir = PathDirname(d);
	}
}

std::string ParentFolder(const std::string& virtualFolder) {
	auto slash = virtualFolder.find_last_of('/');
	if (slash == std::string::npos) return {};
	return virtualFolder.substr(0, slash);
}

bool IsExpanded(std::unordered_map<std::string, bool>& expanded,
				const std::string& virtualFolder, bool isRoot) {
	auto it = expanded.find(virtualFolder);
	if (it != expanded.end()) return it->second;
	return isRoot; // roots default open
}

void RenderFolderNode(const std::string& folder,
					  const std::set<std::string>& folders,
					  std::string& selectedFolder,
					  std::unordered_map<std::string, bool>& expanded) {
	const bool isRoot = folder == kContentRoot || folder == kEngineRoot;
	const std::string label = isRoot ? (folder + "/") : PathBasename(folder);

	bool hasChildren = false;
	for (const auto& f : folders)
		if (f != folder && ParentFolder(f) == folder) { hasChildren = true; break; }

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
	if (selectedFolder == folder) flags |= ImGuiTreeNodeFlags_Selected;

	const bool open = IsExpanded(expanded, folder, isRoot);
	ImGui::SetNextItemOpen(open, ImGuiCond_Always);
	const bool nodeOpen = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", label.c_str());
	if (ImGui::IsItemToggledOpen())
		expanded[folder] = !open;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		selectedFolder = folder;
	if (isRoot && folder == kContentRoot && ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", ContentRootStatus().c_str());

	if (nodeOpen) {
		for (const auto& f : folders)
			if (f != folder && ParentFolder(f) == folder)
				RenderFolderNode(f, folders, selectedFolder, expanded);
		ImGui::TreePop();
	}
}

template<class T>
void CollectAssetPaths(const EntityManager::Ptr& em, std::set<std::string>& folders) {
	em->ForEach<T>([&](const std::shared_ptr<T>& a) {
		if (a) RegisterPathFolders(folders, a->GetPath());
		return true;
	});
}

} // namespace

void SplitAssetPathRoot(const std::string& path, std::string& root, std::string& subpath) {
	// The canonical engine prefix in EM paths ("Engine/Ocean/ocean.json").
	const std::string enginePrefix = "Engine/";
	if (path.compare(0, enginePrefix.size(), enginePrefix) == 0) {
		root = kEngineRoot;
		subpath = path.substr(enginePrefix.size());
		return;
	}
	if (path.compare(0, std::strlen(kEngineEscape), kEngineEscape) == 0) {
		root = kEngineRoot;
		subpath = path.substr(std::strlen(kEngineEscape));
		return;
	}
	root = kContentRoot;
	subpath = path;
}

std::string VirtualFolderOf(const std::string& path) {
	std::string root, sub;
	SplitAssetPathRoot(path, root, sub);
	const std::string dir = PathDirname(sub);
	if (dir.empty()) return root;
	return root + "/" + dir.substr(0, dir.size() - 1);
}

bool AssetPathInFolder(const std::string& path, const std::string& virtualFolder) {
	std::string root, sub;
	SplitAssetPathRoot(path, root, sub);
	std::string wantRoot = virtualFolder, wantSub;
	auto slash = virtualFolder.find('/');
	if (slash != std::string::npos) {
		wantRoot = virtualFolder.substr(0, slash);
		wantSub = virtualFolder.substr(slash + 1);
	}
	if (root != wantRoot) return false;
	if (wantSub.empty()) return true; // a root matches everything under it
	const std::string prefix = wantSub + "/";
	return sub.compare(0, prefix.size(), prefix) == 0;
}

std::string ContentRootStatus() {
	const std::string scenePath = Editor::GetCurrentScenePath();
	if (scenePath.empty()) return "(no scene open)";
	const std::string dir = PathDirname(scenePath);
	return dir.empty() ? std::string("./") : dir;
}

void RenderAssetFolderTree(std::string& selectedFolder,
						   std::unordered_map<std::string, bool>& expanded) {
	std::set<std::string> folders;
	if (Scene::Active) {
		if (auto em = Scene::Active->GetEntityManager()) {
			CollectAssetPaths<Mesh>(em, folders);
			CollectAssetPaths<Material>(em, folders);
			CollectAssetPaths<Texture>(em, folders);
			CollectAssetPaths<AnimationClip>(em, folders);
			CollectAssetPaths<ParticleSystem>(em, folders);
			CollectAssetPaths<Heightmap>(em, folders);
			CollectAssetPaths<OceanWaves>(em, folders);
		}
	}
	folders.insert(kContentRoot);
	folders.insert(kEngineRoot);

	if (selectedFolder.empty() ||
		(selectedFolder != kContentRoot && selectedFolder != kEngineRoot &&
		 folders.find(selectedFolder) == folders.end()))
		selectedFolder = kContentRoot;

	RenderFolderNode(kContentRoot, folders, selectedFolder, expanded);
	RenderFolderNode(kEngineRoot, folders, selectedFolder, expanded);
}

} // namespace Editor
} // namespace fury
