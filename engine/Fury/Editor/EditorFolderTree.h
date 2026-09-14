#ifndef _FURY_EDITOR_FOLDER_TREE_H_
#define _FURY_EDITOR_FOLDER_TREE_H_

#include <string>
#include <unordered_map>

namespace fury {
namespace Editor {

// Virtual tree roots: every EM asset path maps under exactly one.
// "Engine/..." (and the legacy "../../Resource/..." escape) land under
// kEngineRoot, everything else under kContentRoot.
inline constexpr const char* kContentRoot = "content";
inline constexpr const char* kEngineRoot = "engine";

// Splits an asset path into (root, subpath): root is kContentRoot or
// kEngineRoot; subpath is the path relative to that root.
void SplitAssetPathRoot(const std::string& path, std::string& root, std::string& subpath);

// Virtual folder of an asset path ("content/TerrainIsland", "engine",
// ...). Returns the root itself when the asset sits directly under it.
std::string VirtualFolderOf(const std::string& path);

// True when an asset path lives under the virtual folder (recursive,
// separator-bounded). A root matches every asset under it.
bool AssetPathInFolder(const std::string& path, const std::string& virtualFolder);

// Status string for the content root: the scene's directory, or
// "(no scene open)".
std::string ContentRootStatus();

// Draws the folder tree derived from the active scene's EM (folders
// with at least one registered asset, plus both roots). selectedFolder
// is read/written. expanded maps virtual folder path -> open state and
// is owned by the caller (persisted via imgui.ini); roots default open.
void RenderAssetFolderTree(std::string& selectedFolder,
						   std::unordered_map<std::string, bool>& expanded);

} // namespace Editor
} // namespace fury

#endif // _FURY_EDITOR_FOLDER_TREE_H_
