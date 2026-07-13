**Style:** keep comments compact — one-liners, or no comment when the code / API name is self-expressive. Don't restate what the code does in prose; match the surrounding comment density.

## 1. Mesh editor flat listings

- [x] 1.1 In `engine/Fury/Editor/EditorAssetWindows.cpp`, replace the `ImGui::TreeNode("LOD Thresholds")` block (around line 467) with a flat `BeginTable` rendering one row per interior LOD (LOD 1 .. N−1) with columns `Index`, `Threshold`. The deepest LOD row renders read-only `0.000` text instead of a slider. Remove the `TreeNode` / `TreePop` calls.
- [x] 1.2 In the same file, replace the `ImGui::TreeNode("Submeshes")` block plus the inner `ImGui::TreeNode("Submesh", ...)` loop (around lines 595–611) with a flat `BeginTable` rendering header `Index | Name | Indices | Triangles` and one data row per submesh. Remove all `TreeNode` / `TreePop` calls.
- [x] 1.3 Build the editor (`make editor` or equivalent) and confirm the LOD thresholds + submeshes tables render with no collapse arrows when opening a multi-LOD / multi-submesh asset.

## 2. LOD preview override

- [x] 2.1 In `engine/Fury/Editor/EditorAssetWindows.cpp`, add `int preview_lod_override = -1;` to the `OrbitState` struct (around lines 61–76). `-1` = Auto; `[0, GetLodCount())` = override.
- [x] 2.2 In the LOD dropdown block (around lines 438–458), prepend an `Auto` entry as the default selection. Map the selection to `os.preview_lod_override = -1` (Auto) or `os.preview_lod_override = i` (LOD i).
- [x] 2.3 In `RenderMeshPreview` (around lines 706–734 and 902–997), pick `display_mesh`: when `os.preview_lod_override == -1`, use the existing runtime-driven selection; otherwise use `mesh->GetLodMesh(os.preview_lod_override)`. Make sure the metadata stats (vertex / index counts) reflect the picked mesh.
- [x] 2.4 Build, open a mesh editor on a multi-LOD asset, pick `LOD 2`, close and reopen the window, and confirm `LOD 2` is still selected and the preview renders that LOD.

## 3. LOD preview render fix (LOD meshes have empty parent Indices)

- [x] 3.1 In `engine/Fury/Mesh.cpp`, change `Mesh::UpdateBuffer`'s post-update dirty-flag computation so that meshes with submeshes only consider `Positions.GetDirty()`, not the parent `Indices.GetDirty()` (which stays dirty forever when empty). Without this fix `Shader::BindMesh` / `BindSubMesh` early-return on LOD meshes and the preview renders nothing.

## 4. Verification

- [x] 4.1 Run `make editor` (or equivalent) and confirm a clean build.
- [x] 4.2 Smoke-test: open a 4-LOD multi-submesh asset in the Mesh editor, verify flat tables render, verify LOD preview switches, verify LOD preview now renders geometry (not just AABB).
- [x] 4.3 Save a scene containing a multi-LOD mesh, reload it, and confirm the asset file format is unchanged (`Mesh::Save` / `Load` round-trip is byte-equivalent for the LOD chain fields).
- [x] 4.4 Archive the change (`/opsx:archive`) once all tasks above are checked.

> **Trackpad gesture input — not supported.** The bundled ImGui (1.91-ish) does not define `ImGuiMouseSource_Touchpad` and the bundled `imgui_impl_sfml3` backend only emits `ImGuiMouseSource_Mouse`, so a touchpad gesture cannot be distinguished from a physical mouse inside the editor. The original proposal called this out as a risk and accepted the fallback path (gestures disabled, mouse still works). The touchpad/gesture sub-feature is removed from this change — the bundled ImGui/SFML sources are not patched.