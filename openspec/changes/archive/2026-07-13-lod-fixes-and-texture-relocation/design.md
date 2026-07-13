## Context

Three defects were traced to specific code during investigation of the "generate LODs → save `scene_lod.bin` → reopen" workflow:

- **LOD selection** — `MeshRender::UpdateActiveLod` (`engine/Fury/MeshRender.cpp`, selection loop ~L330–339) tests `base->GetLodThreshold(i) >= coverage`. Since `Mesh::GetLodThreshold(0)` is fixed at `1.0` (`Mesh.cpp:572-578`) and `coverage` is clamped to `<= 1.0` (~L325), iteration 0 is always true → `m_ActiveLod` is permanently `0`. The overlay reads the same value via `render->GetActiveLod()` (`PrelightPipeline.cpp:280`), so the debug color/poly count never change. A secondary issue: coverage is built from the model-space AABB (`Mesh::GetAABB`, `Mesh.cpp:541-543`) at ~L298–312 without applying the owning node's world matrix — an acknowledged in-source limitation.
- **Textures 0×0** — `.bin`/`.json` never embed pixels; they store a path. `Scene::Path` (`Scene.cpp:13-17`) prepends the scene working directory to the stored path. A saved scene keeps a multi-segment relative path (`Resource/Scene/wheels.jpg`); reopening via `Importer.LoadScene` sets the working dir to the scene file's folder (`LuaBindings.cpp:668-700`), producing `Resource/Scene/Resource/Scene/wheels.jpg`. `Texture::CreateFromImage` fails to find it and leaves the default 0×0 (`Texture.cpp:94-155,209-245`; `Texture.h:59-62`). Memory-backed textures are already relocated to siblings by `FileUtil::ExtractMemoryBackedTextures` (`FileUtil.cpp:185-272`), but file-backed textures are not.
- **LOD gen too aggressive** — `MeshSimplifier.cpp:242` hard-codes `const bool use_sloppy = true`, threaded into the submesh helper (`:376`, used at `:112` and `:306`). The border-preserving `meshopt_simplify` branch is dead code. The dialog (`EditorAssetWindows.cpp:527-589`, `LODOpts` struct ~L538, fields ~L550-558, call site ~L567) has no method control.

## Goals / Non-Goals

**Goals:**
- Make the runtime active-LOD respond to camera zoom, using world-space coverage, so the LOD-debug color and triangle count change.
- Make saved scenes portable: file-backed textures reload at full resolution regardless of where the scene file is opened from.
- Give artists a method choice in the LOD-gen dialog and default to a less-aggressive method.

**Non-Goals:**
- Embedding decoded pixel data into `.bin` (the format stays path-based).
- Adding a new simplification library — only expose the meshoptimizer entry points already vendored.
- Reworking `Scene::Path` resolution semantics beyond what the texture relocation requires (no absolute-path handling changes).
- Changing the LOD chain serialization format or the `mesh-lod` data model.

## Decisions

**1. Fix the LOD predicate in place; add world-space coverage.**
Replace `base->GetLodThreshold(i) >= coverage` with `coverage >= base->GetLodThreshold(i)` and keep the "first match wins, else deepest" walk (matches the existing `mesh-lod` scenarios). For coverage, transform the model-space AABB by the MeshRender's owning `SceneNode` world matrix (or use `SceneNode::GetWorldAABB`, `SceneNode.h:107`) before projecting. *Alternative considered:* rewriting the whole coverage metric (screen-space bounding-rect area) — rejected as larger scope; the projected-sphere/AABB metric is fine once the world transform and comparison are correct.

**2. Relocate file-backed textures on save, reusing the memory-backed extraction path.**
Extend the existing extraction step in `FileUtil.cpp` so that after handling memory-backed textures, it also handles file-backed ones: resolve `m_FilePath` against the current `Scene::Path`, copy the source image to `dirname(output)/basename(m_FilePath)` (skip if byte-equal already present), and rewrite `m_FilePath` to the bare filename. This makes the saved scene self-contained and removes the double-prepend on reload. *Alternatives considered:* (a) fix only `Scene::Path` to detect and strip a redundant prefix — brittle, doesn't make scenes portable across directories; (b) store absolute paths — breaks portability entirely. Copy-to-sibling matches the memory-backed behavior already specified in `embedded-textures` and keeps one consistent on-disk layout.

**3. Add a `Method` enum to `MeshSimplifyOptions` and dispatch on it.**
Add `enum class Method { Quadric, Sloppy, QuadricLegacy }` to `MeshSimplifier.h`, default `Quadric`. Replace the hard-coded `use_sloppy` at `MeshSimplifier.cpp:242` with dispatch on `opts.method`; the previously-dead `meshopt_simplify` branch becomes live and receives `meshopt_SimplifyLockBorder` for `Quadric` (when `lock_borders`) and no lock for `QuadricLegacy`. Add an ImGui `Combo` "Method" to the dialog and copy the selection into the options struct before `SimplifyMesh`. *Alternative considered:* a boolean "aggressive" toggle — rejected; an enum is extensible and maps cleanly to the three meshopt entry points.

## Risks / Trade-offs

- **Changing the LOD-gen default to Quadric may fail to reduce meshes with many UV seams** (the original reason sloppy was hard-coded) → Sloppy remains available in the dropdown; `target_error` still tunes reduction, and border-locking is only applied when `lock_borders` is set.
- **World-space coverage change could alter LOD switch points for every scene** → intended behavior; the switch points now reflect actual on-screen size. Existing thresholds are unchanged, so ordering is preserved.
- **Texture relocation writes sibling image files next to the scene on every save** → guarded by a byte-equality check to skip redundant copies; a missing source logs a warning and does not fail the save, preserving today's "save still succeeds" behavior.
- **Filename collisions** (two textures with the same basename, different bytes) → resolved with a numeric suffix, mirroring the memory-backed collision rule.

## Migration Plan

- Pure code fix; no data migration. Existing `.bin`/`.json` scenes load unchanged. The first time an old scene is re-saved, its file-backed texture paths are rewritten to bare filenames and the images are copied next to the scene — a one-way improvement, and old multi-segment paths still load if the sibling copy is skipped.
- Rollback: revert the four engine files; saved scenes written with bare filenames still load because `Scene::Path` + bare filename resolves against the scene directory regardless.

## Open Questions

- Should the LOD-gen dialog persist the last-used method across sessions, or reset to the Quadric default each open? (Spec currently mandates default-to-Quadric on open; per-popup persistence is optional.)
- For `QuadricLegacy`, prefer `meshopt_simplify` without the lock flag versus a distinct legacy entry point if one is exposed by the vendored meshoptimizer — to be confirmed against the header at implementation time.
