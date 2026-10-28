# add-rhi-layer — design

## Context

Today the renderer is raw OpenGL with no abstraction: 28 files under `engine/Fury` make ~778 direct `gl*` calls. The concentration is five files — `Shader.cpp` (145), `PrelightPipeline.cpp` (111), `GameUIRenderer.cpp` (86), `Texture.cpp` (75), `Pass.cpp` (44) — plus a long tail of editor readback/thumbnail code. Coupling is type-level: `EnumUtil` maps engine enums straight to GL constants, and GL object IDs are stored as `unsigned int` (`Texture::m_ID`, `Pass::m_FrameBuffer`, `Shader::m_Program`, `RenderTarget::m_FBO`).

Shaders are 30 GLSL files (~4200 lines, `#version 330`, some 430 permutations) in `examples/Resource/Shader/`, one file holding all stages split by injected defines (`VERTEX_SHADER`/`FRAGMENT_SHADER`), with a custom `#include` preprocessor and JSON-declared permutation defines (~79–96 shader entries per pipeline JSON). Uniforms are all loose (name-based `glGetUniformLocation`), no UBOs, no reflection. Non-portable details exist: default-initialized uniforms (`uniform float u_time = 0.0;`), SSBO permutations, shadow comparison samplers, `gl_FragDepth` writes, `sampler3D`/`sampler2DArray`. No compute/tessellation/geometry shaders in shipped shaders.

Windowing is SFML 3 (`examples/main.cpp` negotiates a GL core context 4.6→3.3). Rendering already runs on a dedicated render thread behind the `FramePacket` snapshot + `FrameExecutor` model (`RenderThread.cpp`), with `DispatchGL` funneling off-thread GL work — so the frame boundary is clean and an RHI slots in behind `Pipeline::ExecutePacket`. Cook/package (`CookPipeline`, `DdcStore`, `PakFile`, `furye-cli`) is texture-only today, DDC keys are `SHA1("<typeTag>|<settings>|<sourceSha1>")` with typeTag `"TEXTURE"`, and the `ktx` CLI subprocess is the tool-vendoring precedent. Third-party deps are submodules via `add_subdirectory` (SFML, Jolt, RmlUi) or vendored prebuilt binaries (ktx, FBX2glTF).

macOS GL is frozen at 4.1 and deprecated; there is no Vulkan/Metal code anywhere (a stale ANGLE-as-RHI note in `docs/ARCHITECTURE.md` is historical only).

## Goals / Non-Goals

**Goals:**

- One backend-agnostic RHI API that all engine/editor rendering targets; GL calls leave engine code entirely.
- Three backends: OpenGLRHI (behavior-preserving port of current GL), VulkanRHI (Windows/Linux, macOS via MoltenVK), MetalRHI (macOS).
- One shader source: existing GLSL set cross-compiled via Slang to SPIR-V (Vulkan) and MSL (Metal), GLSL passthrough for GL; permutation model (JSON defines) preserved.
- Startup-only backend selection: `--rhi=` CLI arg, editor option, game option; per-platform support matrix; basic probe + fallback to OpenGL on failure.
- Cook packs shaders for all supported RHIs by default (`--rhi` restricts); shader DDC under `SHADER_` keys with a master version key; texture keys renamed to `TEX_`.
- OpenGLRHI keeps full parity: the existing Lua test suite and screenshots pass unchanged under it.

**Non-Goals:**

- Runtime RHI switching (selection is startup-only; changing backend = restart).
- DirectX 11/12 backends, ray tracing, mesh shaders, GPU-driven rewrite, render-graph architecture.
- Adopting compute in shipped shaders (RHI exposes dispatch, but no shader ports).
- Multi-threaded command recording; the single render thread model stays.
- Mobile/web backends.

## Decisions

### D1. Mid-level RHI API, immediate command lists, global device

One `RHIDevice` global (UE's `GDynamicRHI` shape) created at engine init from the selected backend. Resource types as opaque ref-counted handles (`RHITextureRef`, `RHIBufferRef`, `RHISamplerRef`, `RHIShaderModuleRef`, `RHIGraphicsPipelineRef`, `RHIRenderTargetSetRef`) replacing the `unsigned int` GL IDs. A `RHIGraphicsPipeline` PSO bundles shader stages + blend/depth-stencil/raster state + vertex layout — this matches how `Pass` + `Shader` + `EnumUtil` state is already applied as a unit before draws. Commands go through an `RHICommandList` (setPipeline, setUniform(name, value), setTexture(name, tex, sampler), setVertex/IndexBuffer, draw[Indexed][Instanced], blit, dispatch, beginReadback) recorded and executed on the render thread. Engine enums (`CompareMode`, `BlendMode`, `TextureFormat`, `CullMode`) become the canonical RHI enums; `EnumUtil`'s GL mapping moves inside OpenGLRHI.

- Alternatives: (a) low-level Vulkan-style API — too much churn for 778 call sites; (b) full UE RDG render graph — overkill, and the pass structure already lives in pipeline JSON; (c) ANGLE-style GL-over-everything — translation, not abstraction (the stale ARCHITECTURE.md plan).

### D2. SFML stays the windowing layer; per-backend surface creation

OpenGLRHI: unchanged SFML GL context negotiation (`CreateWindowNegotiated`). VulkanRHI/MetalRHI: construct `sf::WindowBase` (no GL context — `sf::Window` always creates one) and create the surface per backend: SFML's Vulkan helpers (`sf::Vulkan::getGraphicsRequiredInstanceExtensions` + `WindowBase::createVulkanSurface`) for Vulkan; on macOS, attach a `CAMetalLayer` to the `NSWindow` from `getNativeHandle()` for Metal (ObjC++ TU, `Engine_dpi_mac.mm` precedent). Input/event handling lives on `WindowBase`, so `InputUtil` is unaffected. Present is owned by `RHISwapchain` (GL backend wraps `window.display()` + vsync; Vulkan FIFO; Metal nextDrawable).

### D3. Loose-uniform model preserved, emulated via reflection on non-GL backends

The C++ name-based uniform API (`Shader::BindFloat("name", v)`, ~all call sites) and GLSL source style stay. For Vulkan/Metal, Slang reflection emits a per-program name → (constant-buffer offset | texture binding) map, stored in a `.refl` sidecar next to the compiled blob. The backend keeps a CPU shadow of the program's implicit constant buffer (`$Globals`), writes via the reflection map, and uploads/binds descriptors per draw (with a descriptor cache keyed by pipeline + bound texture set). OpenGLRHI keeps `glGetUniformLocation`.

- Alternatives: (a) rewrite all 30 shaders with explicit UBOs and refactor every C++ bind site — large churn, breaks the location-cached draw-command cache contract; (b) push constants — not in GL 3.3 core, would fork the shader source.

### D4. Slang as the cross-compiler, vendored prebuilt, behind a pluggable compiler interface

Per the project directive, Slang compiles the GLSL set → SPIR-V + MSL (+reflection). `libslang` is used in-process (editor and cook both need it) from vendored prebuilt binaries + headers — the `ktx` vendoring precedent, since a from-source Slang submodule build is heavy. All access goes through a small `IShaderCompiler` interface; if Slang's GLSL front-end chokes on our dialect, a glslang + SPIRV-Cross implementation drops in behind the same interface without touching specs or tasks. MSL is shipped as **source text** and compiled on-device (`newLibraryWithSource:`, OS-cached) — no offline `.metallib`, so cooking Metal shaders works on any host. SPIR-V ships binary; GL gets GLSL source passthrough (today's runtime compile path unchanged).

- Alternatives: (a) glslang + SPIRV-Cross primary — mature, but the directive is Slang; kept as contingency behind `IShaderCompiler`; (b) rewrite shaders in the Slang language — loses the GL source path and forks shader authorship.

### D5. Shader portability pass on the GLSL set (prerequisite gate)

Minimal source fixes so the set is strict-portable: remove default uniform initializers (non-standard), keep the injected-stage-define + `#include` model (Slang receives the same preprocessed translation units the GL path builds), pin `binding =` on SSBO blocks (already at 2), verify shadow comparison samplers and `gl_FragDepth` survive. Gate: a `shaderc` tool mode (furye-cli subcommand) must compile all pipeline-JSON permutations for all three targets before backend bring-up tasks start.

### D6. DDC keyspace: `TEX_`/`SHADER_` prefixes + master shader version

`DdcStore::MakeKey` typeTag becomes namespaced: `TEX_` for textures (renamed from `TEXTURE` — one-time recook, entries are content-addressed cache) and `SHADER_` for shaders. Shader settings string = backend + define set + compiler version + `FURY_SHADER_DDC_MASTER_VERSION` constant; bumping that constant flushes only the shader keyspace. Layout mirrors textures: `DDC/<hex[0:2]>/<hex[2:4]>/<hash>.{spv,msl,glsl,refl}`.

### D7. Cook discovers shaders from pipeline JSONs and emits per-backend blobs

Cook already maps a scene → pipeline JSON (`renderSettings`) → shader entries + define permutations; it gains shader enumeration over that set (plus the engine default pipeline) and cross-compiles each permutation for every backend in scope. `furye-cli cook --rhi=opengl,vulkan,metal` (comma list) restricts scope; default is all backends supported on the package target platform. The cook manifest records per-shader per-backend blob paths; pak packs them; at runtime the active RHI loads its blob, OpenGL falling back to source compile when a blob is absent (editor iteration unchanged). Cook stays headless — no RHI device needed (Slang is a library call).

### D8. Selection chain and fallback

Resolution order (first wins): `--rhi=` CLI arg > `FURY_RHI` env > Lua `Engine.run { rhi = "..." }` > editor ini (`g_RhiIni`, the `g_RenderThreadIni` imgui.ini pattern) > platform default (`opengl`). Platform matrix: Windows `opengl,vulkan`; macOS `opengl,vulkan,metal`; Linux `opengl,vulkan`. Headless CLI fast paths (`render-mesh`, `gui`, `cook`) ignore selection (GL-only or no device). Fallback is deliberately basic: after device + swapchain creation, run a probe (create one texture, compile one trivial pipeline, clear + present one frame); any failure logs the error and retries with `opengl`. Editor/game UI list only the platform's supported backends and show a "restart to apply" hint. The island game's options menu persists the choice to its user config; the launcher reads it at next start.

### D9. Render-thread boundary unchanged

RHI calls are confined to the existing render thread exactly as GL is today: `DispatchGL` generalizes to `DispatchRHI` (same job queue), the thread-guard macro follows, and `FramePacket`/`FrameExecutor` are untouched. The Vulkan backend records per-frame `VkCommandBuffer`s inside its command-list implementation; Metal maps one command list over `MTLRenderCommandEncoder`s per pass. Tracy GPU zones become per-backend: GL as today (documented unreliable on Apple), Vulkan via Tracy's VkContext, Metal CPU-only zones (known limitation).

### D10. UI renderers per backend

ImGui: keep vendored `imgui_impl_opengl3`, add upstream `imgui_impl_vulkan` + `imgui_impl_metal` (same vendored-backends precedent), all behind the existing game-thread snapshot / render-thread replay split; the custom `imgui_impl_sfml3` platform layer is backend-agnostic and stays. RmlUi: `GameUIRenderer` is reimplemented over the RHI (its raw GL program/VAO at `GameUIRenderer.cpp:67+` becomes an RHI pipeline + buffers) — `Rml::RenderInterface` is already an abstraction point.

### D11. Readbacks via RHI

Editor picking, screenshots, thumbnails, and Cli headless shots go through `RHIReadback` (fence + stall is fine at these rates): GL keeps `glReadPixels`, Vulkan/Metal stage through a readback buffer.

### D12. Clip-space conventions per backend

Projection construction (MathUtil) takes NDC-z and viewport-Y conventions from RHI caps: GL `[-1,1]` z, Vulkan/Metal `[0,1]` z, with the Y-flip handled in projection (not viewport) so `gl_FragDepth` view-linear writes and the PreZ EQUAL depth-equality requirement hold identically per backend. CSM/spot shadow sampling conventions verified per backend in the shadow spec tests.

### D13. Dependency vending

- Slang: prebuilt binaries (win/mac/linux) + headers under `engine/ThirdParty/slang/` (ktx precedent).
- Vulkan: `Vulkan-Headers` + `volk` as submodules (no-fork precedent), runtime-loaded; macOS gets a vendored prebuilt MoltenVK (`libMoltenVK.dylib` + ICD json) staged next to binaries like the ktx dylib — absence at runtime just means vulkan fails probing and falls back.
- Metal: system frameworks, `.mm` translation units gated on `PLATFORM_MACOS` (`Engine_dpi_mac.mm` precedent).
- CMake: `FURY_RHI_VULKAN` (default ON), `FURY_RHI_METAL` (default ON on Apple); OpenGL always built. New `engine/Fury/RHI/` sources are added to the explicit source list (no globbing in this repo).

### D14. Sequencing and parity gates

Leaf-first migration so the tree keeps working under OpenGLRHI at every step: (1) RHI core + enums + handles, (2) OpenGLRHI extraction of `EnumUtil`/`Texture`/`Shader`/`Pass`/`RenderUtil`/buffers, (3) pipeline migration (`Pipeline`, `PrelightPipeline`, sky, ocean, particles, vegetation, postfx), (4) UI + editor readbacks, (5) Slang toolchain + portability pass (gate G1: all permutations compile for 3 targets), (6) shader DDC + cook/pak, (7) VulkanRHI, (8) MetalRHI, (9) selection/fallback/UX, (10) validation. Gate G2 after step 4: full Lua suite + key screenshots byte-similar under OpenGLRHI. The concurrent vegetation-popping fix lands first; this change rebases over it and does not touch `InstancedMeshStreamer`/`PrelightPipeline` vegetation paths until then.

### D15. Keep generated globals, but make reflection the engine contract

Rewriting every shader and bind site around explicit UBOs is not justified before Vulkan. The generated per-stage block is already the UBO Vulkan needs; the fragile part was unvalidated compiler-owned metadata and lost defaults. The `.refl` v2 sidecar therefore carries the block binding/size, member ranges, and source-parsed default bytes. Compilation rejects unexpected blocks/ranges, Metal seeds shadows from the defaults, and deferred handles are deduplicated. The DDC master version invalidates the whole schema when it changes.

`IShaderCompiler` uses in-process libslang through the session compile-request API plus in-process SPIRV-Cross. No source/SPIR-V temp files or subprocesses are involved. The full 414-target gate passes, and all 210 artifacts in a fresh Metal DDC are byte-identical to the previous driver, so cache keys and runtime loading remain unchanged.

### D16. Pak discovery includes runtime-created assets

Pipeline JSON alone is not a complete shader inventory. Cook also discovers postprocess effects and runtime-created terrain, particle, and atmosphere shaders, plus sky component textures. Declared-path aliases preserve case-sensitive pak lookup. Directory-backed registries enumerate mounted pak keys through `AssetBackend`, so postprocess registration and tonemapping do not disappear in packaged runs.

## Risks / Trade-offs

- Slang's GLSL front-end may reject parts of our dialect (uniform initializers, `#version 330` semantics, comparison samplers) → portability pass first; `IShaderCompiler` pluggability with glslang+SPIRV-Cross contingency; gate G1 blocks backend work until green.
- Loose-uniform emulation costs per-draw descriptor writes on Vulkan → descriptor cache + dynamic offsets; acceptable at this engine's draw-call scale (draw-command cache already caps per-frame binds).
- ~778 call sites is a large mechanical migration → phased leaf-first plan with parity gates; GL behavior is the spec for OpenGLRHI.
- MoltenVK surface/device quirks on macOS → Metal is the primary macOS backend; vulkan-on-mac is probe + fallback territory, and absence of the ICD is handled gracefully.
- MSL source compile at load adds startup cost in packaged Metal games → OS shader cache + parallel compile; revisit offline metallib later if profiling demands it (needs Xcode toolchain on the cook host — rejected for now).
- Pak size grows ~3x for shaders → `--rhi` restrict arg; blobs are small next to textures regardless.
- Vegetation-pop fix collision in `PrelightPipeline.cpp`/`InstancedMeshStreamer.cpp` → sequencing rule in D14; rebase, don't parallel-edit.
- `sf::WindowBase` (no GL context) path is new for vulkan/metal and may miss SFML conveniences (`setVerticalSyncEnabled`, `display`) → swapchain owns vsync/present per backend; input parity covered by existing `InputUtil` tests.

## Migration Plan

1. Land the concurrent vegetation-pop fix first; rebase this work over it.
2. Merge in phases per D14, each phase green under OpenGLRHI (suite + screenshots) before the next. No flag day: OpenGLRHI is the only backend until phase 7, and stays the fallback forever.
3. DDC keyspace change (`TEXTURE` → `TEX_`) orphans old entries — harmless one-time recook; document in changelog.
4. Rollback at any point: `--rhi=opengl` (or omit) runs exactly today's stack.

## Open Questions

- Exact Slang release pin (needs the GLSL front-end at a version that accepts our dialect — resolve at D5 gate).
- MoltenVK acquisition: vendor a pinned prebuilt vs. require the Vulkan SDK at runtime (lean: vendored prebuilt, like ktx).
- Packaged-game RHI config location (user config dir vs. next to pak) — decide with the island game options-menu task.
- Whether `furye` exposes a live "recompile shaders for current RHI" button or relies on DDC invalidation alone.
