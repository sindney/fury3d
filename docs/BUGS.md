# BUGS.md

Hard-to-track but typical engine bugs. Each entry is a compact pattern card:
what it looked like, what it really was, and the trap that made it expensive.
Add entries only after the root cause is proven.

## Layered-FBO re-attach silently redirects later draws to FBO 0 (2026-10-02)

- **Symptom:** HDR scenes render black terrain/water/vegetation; sky and clouds fine. LDR fine.
- **Root cause:** an FBO helper re-attached a texture layer (bind own FBO, attach, restore `GL_FRAMEBUFFER 0`). Every bind-once-set-layer-draw sequence (SkyAtmosphere AP-volume bake, CSM cascades, cube shadow faces) drew all layers into the DEFAULT framebuffer instead of the target texture. The AP volume stayed zeros; `col = col * ap.a + ap.rgb` then zeroed all opaque pixels.
- **Fix:** restore the previously bound framebuffer, never a hardcoded 0.
- **Trap:** no GL error anywhere, FBO reported complete, and sampled texture ids matched on both sides, so the state looks correct at every probe point. Confirm WHERE a draw lands by reading the draw FBO's attachment at draw time, not the texture object you think is bound.

## Pass clear only clears the attached layer of a layered attachment (2026-10-03)

- **Symptom:** deep CSM cascades over-shadow: near geometry lit, far geometry black. Map content "looks present" in previews.
- **Root cause:** `glClear` on an FBO with a layered (2D_ARRAY/3D/CUBE) attachment clears only the currently attached layer. Sibling layers keep UNDEFINED creation content: one build recycled 1.0-filled memory (lucky, lit), another recycled zeros (map depth 0 = everything shadowed in caster-free regions).
- **Fix:** when clearing a layered render target, iterate all layers/faces and clear each (GLRenderTargetSet::ClearAllLayers).
- **RHI port (2026-10-19):** the same bug reappeared after the RHI refactor; the fix now lives in engine/Fury/RHI/OpenGL/ (GLRenderTargetSet::ClearAllLayers + HasLayeredAttachments, hooked into GLCommandList::BeginRenderTargets so layered sets always clear all layers).
- **Trap:** undefined-content bugs are non-deterministic across processes, so the reference build can look correct while the new build is broken with identical code. Compare raw layer dumps (min/max/zero-count), not preview images.

## Non-determinism traps that fake rendering bugs

- **Wind sway time**: vegetation displaces by wall-clock engine time; two runs at different speeds reach frame N in different sway phases. Crown/shadow pixels legitimately differ between builds. Freeze wind time before pixel-comparing.
- **First-frame vs steady state**: lazily created GPU resources may not exist in frame 1's packet but exist from frame ~3. A one-shot probe captures frame-1 state and lies about later frames. Gate probes by call count.

## Metal: GenerateMipmaps silently drops blits between frames -> black albedo at distance (2026-10-19)

- **Symptom:** distant terrain/vegetation render black on Metal, CSM on AND off; looks exactly like a shadow bug, GL fine. Sharp mip 0->1 boundary (near texels sharp, far texels black).
- **Root cause:** RHIMetalDevice::GenerateMipmaps no-ops when m_CommandBuffer == nullptr. Texture uploads drain as render-thread jobs BETWEEN frames (RenderThread::ThreadMain runs jobs before m_Executor exists), so every runtime-loaded mipped texture kept mips 1-4 zeroed; albedo samples black at distance.
- **Fix:** one-shot command buffer from the queue + own autorelease pool when no frame buffer exists; guard mipmapLevelCount <= 1 (cloud-noise cache load hit a Metal validation abort there). engine/Fury/RHI/Metal/RHIMetalDevice.cpp, GenerateMipmaps.
- **Trap:** any Metal device method needing a command buffer must handle the between-frames case. Blit has the same latent issue; its callers are all in-frame today.

## Metal: CLAMP_TO_BORDER sampler hardcodes black border -> shadow footprint renders dark (2026-10-19)

- **Symptom:** geometry beyond the shadow map's footprint renders dark on Metal. CSM on: dark band at mid-distance. CSM off: everything past the single map black.
- **Root cause:** MetalResources.cpp ApplySampler hardcoded MTL::SamplerBorderColorOpaqueBlack for CLAMP_TO_BORDER. The engine's shadow maps use BorderColor white, so out-of-range depth reads 1.0 = lit on GL but 0.0 = shadowed on Metal.
- **Fix:** snap sampler.BorderColor to the nearest of Metal's three border colors (white -> OpaqueWhite). engine/Fury/RHI/Metal/MetalResources.cpp, MetalTexture::ApplySampler.
- **Trap:** Metal offers exactly three border colors; check every engine BorderColor value maps to the intended one, and remember GL's arbitrary float border has no Metal equivalent.

## Metal: sticky per-attachment blend state bakes into later PSOs -> RmlUi text blocks, world hidden (2026-10-19)

- **Symptom:** RmlUi renders wrong on Metal: button labels solid dark blocks, other text per-pixel dithered, and the whole 3D world disappears behind menus. GL fine.
- **Root cause:** the ocean pass in PrelightPipeline.cpp (SetBlendEnabled calls ~line 2398/2618) leaves per-attachment blend state dirty at its "restore" (m_BlendEnabled = {false,false,true,true}). Metal bakes per-attachment blend-enable INTO the PSO at creation (blendOn = BlendEnabled(mode) && m_BlendEnabled[i]), so every later UI PSO compiled with blending DISABLED: the fullscreen semi-transparent backdrop became opaque (covered the world), and text wrote vColor*coverage with no dst term (dark AA edges, pure black gaps). GL is immune because GLCommandList::SetBlendMode re-enables blending wholesale (glEnable(GL_BLEND) covers all draw buffers).
- **Fix:** MetalCommandList::SetRasterState and SetBlendMode reset m_BlendEnabled to all-true (GL parity); the ocean's per-buffer opt-out still works since it re-applies right after. Files: engine/Fury/RHI/Metal/MetalCommandList.cpp (SetRasterState/SetBlendMode); trigger in engine/Fury/PrelightPipeline.cpp ocean pass.
- **Trap:** any sticky render-state leak is invisible until a later PSO bakes it. State that GL treats as global must be reset at the same points on backends that bake it into pipeline objects.

## Metal: shadow projection kept GL's [-1,1] depth -> clipped casters, acne band (2026-10-20)

- **Symptom:** on Metal, a band of dense dark acne across the mid-distance ground (rows 300-480 of a 720p frame: 42%-67% down), sharp shadows still correct, disappears with shadowing off. GL clean.
- **Root cause:** Metal clips z to [0,1]; GL uses [-1,1]. The light shadow projections (the CSM ortho from `GetCropMatrix`, the spot/point projections) were always built in GL's convention, so on Metal the near half of the casters was clipped away and the stored depth lived in a different space than the shader's lookup: the receiver compared `shadowCoord.z` (raw clip z) against a map written as a remapped depth, and the receiver-side bias was half its GL magnitude -> borderline comparisons -> per-pixel acne. The shader-side offset matrix was already convention-aware (z-identity for [0,1] backends), so the projection was the half that never got converted.
- **Fix:** `Matrix4::DepthConvention` (engine/Fury/Matrix4.cpp) returns a projection in the active backend's NDC depth convention; the light shadow projections go through it. GL is unchanged (the remap is a no-op there). engine/Fury/Pipeline.cpp (GetCropMatrix, point cube), engine/Fury/PrelightPipeline.cpp (spot).
- **Trap:** a convention fix on ONE side of a pair is worse than none - the shadow map's render and its lookup must agree. When a backend's clip volume differs, every projection that feeds a depth target needs converting, and the depth-based bias constants are in the *target's* units, so they change scale with the conversion.

## Metal: polygon offset died with the encoder (CSM cascades 1-3 unbiased) (2026-10-20)

- **Symptom:** deep CSM cascades over-shadow / acne on Metal only; cascade 0 fine.
- **Root cause:** Metal keeps the depth bias on the render command encoder, and the encoder is rebuilt whenever the pass switches attachment layers (the CSM cascade loop). `MetalCommandList::SetDepthBias` wrote to the encoder that existed at call time, so every rebuilt encoder started with a zero bias - and the bias set right after `Bind()` only survived while the encoder was not rebuilt. GL's `glPolygonOffset` is global state and never had this problem.
- **Fix:** the bias is held on the command list and re-applied to every encoder it creates (engine/Fury/RHI/Metal/MetalCommandList.cpp).
- **Trap:** GL treats render state as global; backends that bind it to a command encoder lose it on every encoder rebuild. Any state a rebuild can drop must be re-applied at encoder creation, not only at set time.

## Tooling: --gpu-dump is the first stop for any rendering bug (2026-10-19)

- `--gpu-dump <dir> [--gpu-dump-frame N]` (engine/Fury/GpuDump.cpp, hooked in Engine.cpp) writes backbuffer.png (final composite incl. UI), dumps UI font-atlas/document textures via GameUI::CollectUITextures, reads depth textures as grayscale, and Metal Readback handles RGBA16Unorm and TEXTURE_3D (slice arg must be 0 with region.z).
- Fast isolation: /tmp probe scripts, METAL_DEVICE_WRAPPER_TYPE=1, MTL_SHADER_VALIDATION=1.
- **Do not diff the two backends' texture dumps against each other:** GL's 2D-array readback (engine/Fury/RHI/OpenGL/GLResources.cpp) fills only one of the layer slots, and the depth dumps use different conversions per backend, so the two images are not the same quantity. The backbuffers ARE comparable - use the near-black pixel fraction per row band as the metric.
- Headless repro for the shadow band: from examples/, `FURY_TOD_HOURS=13 FURY_WIND_TIME=0 ./fury ../tests/lua/scenes/grass_island_check.lua --rhi=metal --gpu-dump <dir> --gpu-dump-frame 30`.

## Metal: a 32-bit texture id truncated the MTLTexture pointer -> crash on the first UI image (2026-10-22)

- **Symptom:** the editor on Metal crashes (EXC_BAD_ACCESS) roughly 45s after the scene loads, the moment the Content Browser draws its first mesh thumbnail. Stack: `objc_retain` -> `-[AGXG16XFamilyRenderContext setFragmentTexture:atIndex:]` -> `ImGui_ImplMetal_RenderDrawData` -> `MetalCommandList::DrawUiOverlay`. OpenGL is clean.
- **Root cause:** a GPU resource identity is 32 bits on OpenGL (a GL name) but a 64-bit POINTER on Metal (the `MTL::Texture*` itself). `Editor::GetMeshThumbnail()` still returned `unsigned int` - the GL-era type - while its value comes from `DisplayTextureId()`, a `uintptr_t` holding the RHI native handle. The top 32 bits were dropped, and ImGui's Metal backend casts the id straight back into an `id<MTLTexture>`, so `objc_retain` ran on an unmapped address. GL is immune: a GL name really is 32 bits, and a stale name binds nothing instead of faulting. The crash lands late because the thumbnail id is 0 until the first thumbnail render job publishes, so the bad bind only happens once a thumbnail actually exists.
- **Fix:** every engine-level accessor that exposes a resource identity is now `uintptr_t`: `Texture::GetID()` widened and `GetUiTextureId()` folded into it, and the GL-era `Shader::GetProgram()`, `RenderTarget::GetFBO()` and `ArrayBuffer<T>::GetID()` deleted (the OpenGL backend keeps its own correctly-typed ones). The one remaining 64->32 narrowing, `GameUIRenderer::Replay`'s `GLBindInterleavedGeometry` call, now runs only on the OpenGL backend and takes its ids from `GLBuffer::GetID()` instead of casting a native handle.
- **Trap:** an id is not a number. Wherever a resource identity crosses a layer - UI toolkit, Lua, editor cache, file - its type must be at least as wide as the widest backend's handle. A 32-bit holder is a latent crash on every non-GL backend and looks perfectly fine on GL, so the bug hides until the second backend lands. Narrowing belongs inside the backend, where the API demands it.

## Metal: GLSL uniform initializers are dropped -> an unbound uniform reads 0 (2026-10-22)

- **Symptom:** on Metal a band of distant terrain renders black (the same band is lit on GL): rows 534-539 of a 720p frame, i.e. the far cascade range. The whole-image Metal-vs-GL diff was mean 2.41 with 4.8% of the pixels differing. CSM off: the two backends match exactly.
- **Root cause:** the Metal path compiles GLSL through Slang -> SPIR-V -> SPIRV-Cross -> MSL, and a GLSL uniform initializer (`uniform float bias = 0.002;`) has no representation in that chain. Slang packs the loose uniforms into a `globalParams` UBO, SPIR-V has no default value for a UBO member, so the emitted MSL declares `float bias0;` with no initializer. On Metal the loose uniforms live in a CPU-side shadow buffer that is zero-initialized (`m_GlobalsShadow[stage].assign(blockSize, 0)`, engine/Fury/RHI/Metal/MetalCommandList.cpp) and only the uniforms the engine binds are ever written, so an unbound uniform reads 0 instead of its GLSL default. The CSM receiver-side depth bias was such a uniform (nothing in the engine ever binds `bias`), so Metal ran the shadow comparison with zero bias: terrain that is its own caster compared equal-depth and lost the comparison, and the far terrain went black. GL is immune - the driver applies the GLSL initializer.
- **Fix:** `bias` is a shader constant now (examples/Resource/Shader/Lambert/SunLight.glsl) and `u_shadow_floor` is bound from C++ (engine/Fury/ParticleRenderer.cpp). The OpenGL output is bit-identical to before (the uniform default was the value in use).
- **Trap:** a shader constant and a uniform default are not the same thing once a backend emulates uniforms on the CPU. Anything a shader relies on must be set by the engine, not by the source's initializer - a GLSL initializer is a GL-only feature and dies silently in the cross-compile. 94 uniforms in examples/Resource/Shader carry initializers; the engine and the post-process JSONs set 93 of them by name, the rest are the exposure.
