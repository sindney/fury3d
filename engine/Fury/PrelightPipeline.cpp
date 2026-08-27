#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

// Debug-build picker trace machinery: rejection strings cost a
// snprintf per gate, so they exist only in FURY_BUILD_DEBUG builds.
#ifdef FURY_BUILD_DEBUG
#define FURY_SHADOW_REJECTF(...) do { \
	char rejBuf[256]; \
	std::snprintf(rejBuf, sizeof(rejBuf), __VA_ARGS__); \
	rejected.push_back(rejBuf); } while (0)
#else
#define FURY_SHADOW_REJECTF(...)
#endif

#include "Fury/Camera.h"
#include "Fury/Log.h"
#include "Fury/EnumUtil.h"
#include "Fury/Frustum.h"
#include "Fury/GLLoader.h"
#include "Fury/Gui.h"
#include "Fury/InputUtil.h"
#include "Fury/Light.h"
#include "Fury/MathUtil.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/Pass.h"
#include "Fury/Pipeline.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/PrelightPipeline.h"
#include "Fury/RenderSettings.h"
#include "Fury/RenderTarget.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/OceanComponent.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/OceanWaves.h"
#include "Fury/RenderQuery.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/SphereBounds.h"
#include "Fury/Texture.h"

#if WITH_EDITOR
#include "Fury/Editor/EditorDebug.h"
#endif

namespace fury
{
	PrelightPipeline::Ptr PrelightPipeline::Create(const std::string &name)
	{
		return std::make_shared<PrelightPipeline>(name);
	}

	PrelightPipeline::PrelightPipeline(const std::string &name)
		: Pipeline(name)
	{
		m_TypeIndex = typeid(PrelightPipeline);
		SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, true);
	}

	bool PrelightPipeline::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Pipeline::Load(wrapper, false))
			return false;

		bool boolValue = IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP);
		if (LoadMemberValue(wrapper, "cascaded_shadow_map", boolValue))
			SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, boolValue);
		else
			SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, true);

		return true;
	}

	void PrelightPipeline::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Pipeline::Save(wrapper, false);

		SaveKey(wrapper, "cascaded_shadow_map");
		SaveValue(wrapper, IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP));

		if (object)
			EndObject(wrapper);
	}

	void PrelightPipeline::Execute(const std::shared_ptr<SceneManager> &sceneManager)
	{
		ASSERT_MSG(m_CurrentCamera != nullptr, "PrelightPipeline.m_CurrentCamera not found!");

		// pre
		m_CurrentShader = nullptr;
		m_CurrentMateral = nullptr;
		m_CurrentMesh = nullptr;
		SortPassByIndex();

		// Seed HDR / CSM / chain from the scene's renderSettings.
		// ApplyRenderSettings owns chain order (canonical stages)
		// and auto-injects the tonemap when HDR is on.
		if (Scene::Active && Scene::Active->GetRenderSettings())
			ApplyRenderSettings(*Scene::Active->GetRenderSettings());

		// Drop last-frame's per-light shadow map cache. The map is
		// populated by Draw{Dir,Point,Spot,Cascaded}LightShadowMap
		// during the per-pass draw loop below and read by the editor's
		// Profiler -> Shadows tab after Execute returns. Clearing here
		// ensures light pointers from the previous frame cannot leak
		// into the new frame.
		m_LastShadowTextures.clear();
		m_LastShadowMatrices.clear();

		// find visible nodes
		RenderQuery::Ptr query = RenderQuery::Create();
		sceneManager->GetRenderQuery(m_CurrentCamera->GetComponent<Camera>()->GetFrustum(), query);
		query->Sort(m_CurrentCamera->GetWorldPosition());

		// draw passes

		Texture::Ptr finalBuffer = nullptr;
		unsigned int passCount = static_cast<int>(m_SortedPasses.size());

		// The chain replaces the LAST quad pass (the screen write)
		// but keeps intermediate quad passes (e.g. pass_combine)
		// running so their outputs stay readable.
		const bool chainReplacesFinal =
			!m_ActiveChain.empty() &&
			(!IsHDRMode() || HasHDRComposite());

		for (unsigned int i = 0; i < passCount; i++)
		{
			auto passName = m_SortedPasses[i];
			auto pass = m_EntityManager->Get<Pass>(passName);

			auto drawMode = pass->GetDrawMode();

			m_CurrentShader = pass->GetFirstShader();

			if (m_CurrentCamera == nullptr)
				continue;

			// Skip only the last quad pass (the screen write);
			// earlier quad passes must still produce their outputs
			// for the chain to read.
			bool isLastQuadPass = (chainReplacesFinal &&
				drawMode == DrawMode::QUAD &&
				i == passCount - 1);
			if (isLastQuadPass)
				continue;

			// enable gamma correction on last pass (legacy path;
			// the chain encodes itself)
			bool isFinalLegacyPass = !chainReplacesFinal && (i == passCount - 1);
			if (isFinalLegacyPass)
				glEnable(GL_FRAMEBUFFER_SRGB);

			if (drawMode == DrawMode::OPAQUE)
			{
				pass->Bind();
				for (const auto &unit : query->opaqueUnits)
					DrawUnit(pass, unit);
			}
			else if (drawMode == DrawMode::TRANSPARENT)
			{
				pass->Bind();

				// Back-to-front (RenderQuery::Sort) alpha-blended base:
				// ambient + emissive + albedo*alpha compositing.
				for (const auto &unit : query->transparentUnits)
					DrawUnit(pass, unit);

				// CPU-driven billboard particles. Drawn after
				// transparent mesh units so they participate in
				// back-to-front sort order. Particles are emissive
				// in v1 (no light sampling) -- they go between the
				// base pass and the additive light pass, NOT into
				// the additive loop (which would double-bright
				// the smoke/fire).
				if (!query->particleNodes.empty())
				{
					// Camera axes (world space) for billboard baking --
					// columns 0/1 of the camera's world matrix.
					Vector4 camRight(1.0f, 0.0f, 0.0f, 0.0f);
					Vector4 camUp(0.0f, 1.0f, 0.0f, 0.0f);
					if (m_CurrentCamera)
					{
						Matrix4 cw = m_CurrentCamera->GetWorldMatrix();
						camRight = Vector4(cw.Raw[0], cw.Raw[1], cw.Raw[2], 0.0f);
						camUp = Vector4(cw.Raw[4], cw.Raw[5], cw.Raw[6], 0.0f);
					}

					// Picks THE dominant shadow source per emitter (one
					// per particle draw; mesh transparents evaluate every
					// light in the additive loop instead). Highest score
					// wins; dir uses CSM when the switch is on, else
					// dir-single.
					auto pickShadowSourceFor =
						[&](const Vector4 &emitterPos,
							ParticleShadowInfo &info)
					{
						// Score = light arriving at the emitter: raw
						// intensity for the directional (NO distance term --
						// intensity/dist2 reads the sun node's parked
						// position; a near-origin dim sun outscored every
						// local light and the spot path never ran),
						// intensity x linear falloff for point/spot.
						struct Candidate {
							SceneNode *node;
							LightType type;
							float score;
						};
						std::vector<Candidate> cands;
#ifdef FURY_BUILD_DEBUG
						std::vector<std::string> rejected;
#endif
						for (const auto &lightNode : query->lightNodes)
						{
							auto light = lightNode->GetComponent<Light>();
							if (!light || !light->GetCastShadows()) { FURY_SHADOW_REJECTF("noCast: %s", lightNode->GetName().c_str()); continue; }
							auto it = m_LastShadowTextures.find(lightNode.get());
							if (it == m_LastShadowTextures.end() || !it->second) { FURY_SHADOW_REJECTF("noShadowTex: %s", lightNode->GetName().c_str()); continue; }
							info.anyCaster = true;
							auto wp = lightNode->GetWorldPosition();
							float dx = wp.x - emitterPos.x;
							float dy = wp.y - emitterPos.y;
							float dz = wp.z - emitterPos.z;
							float dist2 = dx * dx + dy * dy + dz * dz;

							// Influence gate: only lights that REACH
							// this emitter may be ranked -- Particle.glsl
							// reads an out-of-range source as fully lit.
							if (light->GetType() == LightType::POINT)
							{
								float r = light->GetEffectiveRadius();
								if (dist2 > r * r) { FURY_SHADOW_REJECTF("radius P: %s", lightNode->GetName().c_str()); continue; }
							}
							else if (light->GetType() == LightType::SPOT)
							{
								float r = light->GetEffectiveRadius();
								if (dist2 > r * r)
								{
									FURY_SHADOW_REJECTF("radius S r=%.2f: %s",
										r, lightNode->GetName().c_str());
									continue;
								}
								// Emitter must lie within the outer cone;
								// dx is emitter->light so the inside test
								// flips sign: cosTheta < -cosOuter.
								float dist = std::sqrt(dist2) + 1e-4f;
								auto lightFwd = lightNode->GetWorldMatrix()
									.Multiply(Vector4(0, -1, 0, 0)).Normalized();
								float cosTheta = (dx * lightFwd.x + dy * lightFwd.y + dz * lightFwd.z) / dist;
								float cosOuter = std::cos(light->GetOutterAngle() * 0.5f);
								if (cosTheta >= -cosOuter)
								{
									FURY_SHADOW_REJECTF(
										"cone S cosT=%.4f cosOuter=%.4f outAngle=%.2f fwd=(%.3f,%.3f,%.3f): %s",
										cosTheta, cosOuter, light->GetOutterAngle(),
										lightFwd.x, lightFwd.y, lightFwd.z,
										lightNode->GetName().c_str());
									continue;
								}
							}

							float score;
							if (light->GetType() == LightType::DIRECTIONAL)
								score = light->GetIntensity();
							else
								score = light->GetIntensity()
									* std::max(0.0f, 1.0f - std::sqrt(dist2)
										/ light->GetEffectiveRadius());
							cands.push_back({lightNode.get(), light->GetType(), score});
						}
						std::sort(cands.begin(), cands.end(),
							[](const Candidate &a, const Candidate &b)
							{ return a.score > b.score; });

						auto mit_helper = [&](SceneNode *node)
							-> const Pipeline::ShadowData *
						{
							auto mit = m_LastShadowMatrices.find(node);
							return (mit != m_LastShadowMatrices.end())
								? &mit->second : nullptr;
						};
						auto populateFrom = [&](SceneNode *node, LightType t)
						{
							info.texture = m_LastShadowTextures[node];
							auto *sd = mit_helper(node);
							// Cached matrices map camera-view -> shadow
							// UV (deferred convention); particles feed
							// world pos, so append the camera's
							// invert-world to chain world->view->shadow UV.
							const Matrix4 viewFromWorld = m_CurrentCamera
								? m_CurrentCamera->GetInvertWorldMatrix()
								: Matrix4();
							if (t == LightType::POINT)
							{
								auto light = node->GetComponent<Light>();
								auto wp = node->GetWorldPosition();
								info.type = 1;
								info.lightPos = Vector4(wp.x, wp.y, wp.z, 0);
								info.lightRadius = light->GetEffectiveRadius();
							}
							else if (t == LightType::SPOT)
							{
								auto light = node->GetComponent<Light>();
								auto wp = node->GetWorldPosition();
								info.type = 4;
								info.matrix = sd ? (sd->single * viewFromWorld) : Matrix4();
								// Cone-test inputs: Particle.glsl is
								// emissive, so the cone falloff lives in
								// the shadow factor.
								info.lightPos = Vector4(wp.x, wp.y, wp.z, 0);
								info.lightDir = node->GetWorldMatrix()
									.Multiply(Vector4(0, -1, 0, 0)).Normalized();
								info.coneHalfInner = light->GetInnerAngle() * 0.5f;
								info.coneHalfOuter = light->GetOutterAngle() * 0.5f;
							}
							else if (t == LightType::DIRECTIONAL)
							{
								if (IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP)
									&& sd && !sd->csm.empty())
								{
									info.type = 3;
									for (int i = 0; i < 4; i++)
										info.csmMatrices[i] = sd->csm[i] * viewFromWorld;
									info.shadowFar = sd->shadowFar;
								}
								else
								{
									info.type = 2;
									info.matrix = sd ? (sd->single * viewFromWorld) : Matrix4();
								}
							}
						};

						SceneNode *pickedNode = nullptr;
						LightType pickedType = LightType::POINT;
						if (!cands.empty())
						{
							pickedNode = cands.front().node;
							pickedType = cands.front().type;
							populateFrom(pickedNode, pickedType);
						}

#ifdef FURY_BUILD_DEBUG
						// FURY_SHADOW_DEBUG=1 dumps the pick + rejection
						// reasons (one block per emitter per frame --
						// headless debugging only).
						static const bool kShadowDbg =
							std::getenv("FURY_SHADOW_DEBUG") != nullptr;
						if (kShadowDbg)
						{
							std::fprintf(stderr,
								"[shadowpick] emitter(%.0f,%.0f,%.0f) -> %s type=%d\n",
								emitterPos.x, emitterPos.y, emitterPos.z,
								pickedNode ? pickedNode->GetName().c_str() : "(none)",
								info.type);
							if (pickedNode && pickedType == LightType::SPOT)
							{
								auto l = pickedNode->GetComponent<Light>();
								std::fprintf(stderr,
									"    spot inner=%.4f outter=%.4f rad (%.1f/%.1f deg)\n",
									l->GetInnerAngle(), l->GetOutterAngle(),
									l->GetInnerAngle() * 57.2958f, l->GetOutterAngle() * 57.2958f);
							}
							for (const auto &r : rejected)
								std::fprintf(stderr, "    reject %s\n", r.c_str());
						}
#endif
					};

					GLint prevSrc, prevDst;
					glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrc);
					glGetIntegerv(GL_BLEND_DST_RGB, &prevDst);
					for (const auto &node : query->particleNodes)
					{
						auto pr = node->GetComponent<ParticleRenderer>();
						if (!pr) continue;
						// Sync mesh to live pool, then issue draw
						// with the configured blend mode.
						pr->UpdateMesh(camRight, camUp);
						// Mirror the renderer's blend mode onto GL
						// state. Particle renderers don't bind the
						// pipeline's Pass blend -- each emitter has
						// its own ALPHA/ADDITIVE choice.
						if (pr->GetBlendMode() == ParticleBlend::ADDITIVE)
							glBlendFunc(GL_ONE, GL_ONE);
						else
							glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						// Depth-tested, NOT depth-written (the
						// transparent pass convention).
						glEnable(GL_DEPTH_TEST);
						glDepthMask(GL_FALSE);
						// Per-renderer shadow selection -- pick the
						// dominant casting light for THIS emitter.
						ParticleShadowInfo shadowInfo;
						auto wp = node->GetWorldPosition();
						pickShadowSourceFor(wp, shadowInfo);
						pr->Draw(m_CurrentCamera, &shadowInfo);
						glDepthMask(GL_TRUE);
					}
					glBlendFunc(prevSrc, prevDst);
				}

				// One additive (ONE, ONE) draw per light so
				// transparents pick up direct lighting without light arrays. The forward shader premultiplies diffuse by alpha and leaves specular full-strength (glass highlights); per-light occlusion between transparents is ignored -- documented approximation.
				if (!query->lightNodes.empty())
				{
					// Additive over the pass's declared alpha blend: restore exactly when the loop exits so the deviation doesn't leak.
					GLint prevSrc, prevDst;
					glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrc);
					glGetIntegerv(GL_BLEND_DST_RGB, &prevDst);
					glBlendFunc(GL_ONE, GL_ONE);
					for (const auto &lightNode : query->lightNodes)
					{
						if (lightNode->GetComponent<Light>() == nullptr)
							continue;
						for (const auto &unit : query->transparentUnits)
							DrawUnit(pass, unit, lightNode);
					}
					glBlendFunc(prevSrc, prevDst);
				}
			}
			else if (drawMode == DrawMode::QUAD)
			{
				pass->Bind();
				DrawQuad(pass);
			}
			else if (drawMode == DrawMode::SKY)
			{
				// DrawSky owns the bind: LUT updates render into their
				// own FBOs first, then pass_sky binds (no clear -- it
				// would wipe hdr_composite).
				DrawSky(pass);
			}
			else if (drawMode == DrawMode::OCEAN)
			{
				// Same owned-bind pattern as DrawSky (needs a pre-pass
				// depth copy for shore foam before the pass binds).
				DrawOcean(pass, query);
			}
			else if (drawMode == DrawMode::LIGHT)
			{
				pass->Bind(true);

				for (const auto &node : query->lightNodes)
				{
					if (auto ptr = node->GetComponent<Light>())
					{
						if (ptr->GetType() == LightType::DIRECTIONAL)
							DrawDirLight(sceneManager, pass, node);
						else if (ptr->GetType() == LightType::POINT)
							DrawPointLight(sceneManager, pass, node);
						else
							DrawSpotLight(sceneManager, pass, node);
					}
				}
			}

			pass->UnBind();

			if (isFinalLegacyPass)
				glDisable(GL_FRAMEBUFFER_SRGB);

			if (m_CurrentShader != nullptr)
				m_CurrentShader->UnBind();

			m_CurrentShader = nullptr;
			m_CurrentMateral = nullptr;
			m_CurrentMesh = nullptr;
		}

		// Run the postprocess chain if one is active. The chain's
		// final write hits the default framebuffer (or the editor's
		// RenderTarget if set) with sRGB encode baked into the
		// shader's u_gamma_correct uniform (since the FBOs we bind
		// are non-sRGB).
		if (chainReplacesFinal)
			RunPostProcessChain();

		// draw debug
		if (IsSwitchOn({ PipelineSwitch::CUSTOM_BOUNDS, PipelineSwitch::LIGHT_BOUNDS,
			PipelineSwitch::MESH_BOUNDS, PipelineSwitch::OCTREE_BOUNDS,
			PipelineSwitch::EDITOR_GRID }, true) ||
			(PhysicsWorld::Exists() && PhysicsWorld::Instance()->HasBuoyancyDebugDraw()))
		{
			// When an offscreen RenderTarget is set, the final composite
			// pass rendered into it (see Pass::Bind). The last pass's
			// UnBind rebound framebuffer 0, so re-bind the RT here so the
			// debug overlays composite over the scene inside the viewport
			// image. Restore framebuffer 0 afterward so the caller (and
			// Gui::Render) draw to the default framebuffer.
			GLint prev_fbo = 0;
			GLint prev_vp[4] = { 0, 0, 0, 0 };
			if (m_RenderTarget != nullptr && m_RenderTarget->IsAllocated())
			{
				glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
				glGetIntegerv(GL_VIEWPORT, prev_vp);
				glBindFramebuffer(GL_FRAMEBUFFER, m_RenderTarget->GetFBO());
				glViewport(0, 0, m_RenderTarget->GetWidth(), m_RenderTarget->GetHeight());
			}

			DrawDebug(query);

			if (m_RenderTarget != nullptr && m_RenderTarget->IsAllocated())
			{
				glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
				glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
			}
		}

		// Buffer debug view (viewport toolbar "View SSAO/SSR"): runs
		// the effect's DEBUG_VIEW variant into the "debug_view"
		// texture; the editor presents it in place of the scene.
		if (IsSwitchOn(PipelineSwitch::SSAO_VIEW))
			DrawEffectDebugView("SSAO");
		else if (IsSwitchOn(PipelineSwitch::SSR_VIEW))
			DrawEffectDebugView("SSR");
		else
			SetDebugViewTexture(nullptr);

		// post
		m_CurrentShader = nullptr;
		m_CurrentMateral = nullptr;
		m_CurrentMesh = nullptr;

		// Now that every pass (transparent shadow-receive included) is
		// done sampling this frame's shadow maps, return them to the
		// temporary pool.
		for (auto &tex : m_FrameShadowTemps)
			Texture::ReleaseTemporary(tex);
		m_FrameShadowTemps.clear();
	}

	void PrelightPipeline::DrawUnit(const std::shared_ptr<Pass> &pass, const RenderUnit &unit,
		const std::shared_ptr<SceneNode> &lightNode)
	{
		auto node = unit.node;
		auto material = unit.material;

		// LOD selection: refresh the active LOD from the camera's
		// screen-coverage of the model's AABB, then draw the picked
		// mesh. When the MeshRender has no LodGroup bound,
		// GetActiveMesh() returns the original unit.mesh and
		// UpdateActiveLod is a no-op -- preserving the pre-LOD draw
		// path exactly.
		auto render = node->GetComponent<MeshRender>();
		if (render) render->UpdateActiveLod(m_CurrentCamera);
		auto mesh = unit.mesh;
		if (render)
		{
			auto active = render->GetActiveMesh();
			if (active) mesh = active;
		}

		auto shader = material->GetShaderForPass(pass->GetRenderIndex());

		if (shader == nullptr)
		{
			// MASK materials request the ALPHA_TEST shader variant so
			// the discard branch compiles only where it's needed.
			unsigned int textureFlags = material->GetTextureFlags();
			if (material->GetAlphaMode() == AlphaMode::MASK)
				textureFlags |= (unsigned int)ShaderTexture::ALPHA_TEST;
			// Shadow-receive variant when this draw's light casts
			// (transparent additive loop) -- the shadow samplers/compares
			// compile only into the *_shadow_shader variants.
			if (lightNode)
				if (auto light = lightNode->GetComponent<Light>())
					if (light->GetCastShadows())
						textureFlags |= (unsigned int)ShaderTexture::SHADOW;
			shader = pass->GetShader(mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH,
				textureFlags);

			// Fall back in steps: first without the shadow bit, then
			// without alpha-test (passes that never declare them).
			if (shader == nullptr && (textureFlags & (unsigned int)ShaderTexture::SHADOW))
				shader = pass->GetShader(mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH,
					textureFlags & ~(unsigned int)ShaderTexture::SHADOW);
			if (shader == nullptr)
				shader = pass->GetShader(mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH,
					material->GetTextureFlags());
		}

		if (shader == nullptr)
		{
			FURYW << "Failed to draw " << node->GetName() << ", shader not found!";
			return;
		}

		bool materialChanged = material != m_CurrentMateral;
		m_CurrentMateral = material;

		bool meshChanged = mesh != m_CurrentMesh;
		m_CurrentMesh = mesh;

		bool shaderChanged = materialChanged || shader != m_CurrentShader;
		m_CurrentShader = shader;

		if (shaderChanged)
		{
			materialChanged = meshChanged = true;

			shader->Bind();
			shader->BindCamera(m_CurrentCamera);

			for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
			{
				auto ptr = pass->GetTextureAt(i, true);
				shader->BindTexture(ptr->GetName(), ptr);
			}
		}

		if (materialChanged)
		{
			shader->BindMaterial(material);

			// Alpha test: only MASK materials get a cutoff >= 0;
			// everything else disables the discard branch via -1.
			// (Programs retain stale uniforms, so bind every time the
			// material changes.)
			shader->BindFloat("u_alpha_cutoff",
				material->GetAlphaMode() == AlphaMode::MASK ? material->GetAlphaCutoff() : -1.0f);
		}

		// Forward transparent shading: u_light_type 0 = ambient/emissive
		// base, 1/2/3 = directional/point/spot additive contribution.
		if (pass->GetDrawMode() == DrawMode::TRANSPARENT)
		{
			int lightType = 0;
			if (lightNode != nullptr)
			{
				if (auto light = lightNode->GetComponent<Light>())
				{
					lightType = (int)light->GetType() + 1;
					shader->BindLight(lightNode);

					// Shadow-receive for this light's direct
					// contribution (matches the deferred path's
					// behavior for opaques). Only the *_shadow_shader
					// variants declare the samplers; on any other
					// shader this block must not even bind dummies.
					if (shader->GetTextureFlags() & (unsigned int)ShaderTexture::SHADOW)
					{
						int shadowType = 0;
						Texture::Ptr shadowTex2D, shadowCube, shadowTexCSM;
						if (light->GetCastShadows())
						{
							auto it = m_LastShadowTextures.find(lightNode.get());
							if (it != m_LastShadowTextures.end() && it->second)
							{
								if (light->GetType() == LightType::POINT)
								{
									shadowType = 1;
									shadowCube = it->second;
									Matrix4 camWorld = m_CurrentCamera->GetWorldMatrix();
									shader->BindMatrix("shadow_matrix", &camWorld.Raw[0]);
								}
								else if (light->GetType() == LightType::DIRECTIONAL &&
									!IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP))
								{
									auto mit = m_LastShadowMatrices.find(lightNode.get());
									if (mit != m_LastShadowMatrices.end())
									{
										shadowType = 2;
										shadowTex2D = it->second;
										shader->BindMatrix("shadow_matrix", &mit->second.single.Raw[0]);
									}
								}
								else if (light->GetType() == LightType::DIRECTIONAL)
								{
									// CSM (CASCADED_SHADOW_MAP on)
									auto mit = m_LastShadowMatrices.find(lightNode.get());
									if (mit != m_LastShadowMatrices.end() && !mit->second.csm.empty())
									{
										shadowType = 3;
										shadowTexCSM = it->second;
										shader->BindMatrices("shadow_matrix_csm", (int)mit->second.csm.size(), &mit->second.csm[0]);
										shader->BindFloat("shadow_far",
											mit->second.shadowFar.x, mit->second.shadowFar.y,
											mit->second.shadowFar.z, mit->second.shadowFar.w);
									}
								}
								else if (light->GetType() == LightType::SPOT)
								{
									auto mit = m_LastShadowMatrices.find(lightNode.get());
									if (mit != m_LastShadowMatrices.end())
									{
										shadowType = 4;
										shadowTex2D = it->second;
										shader->BindMatrix("shadow_matrix", &mit->second.single.Raw[0]);
									}
								}
							}
						}
						shader->BindTexture("shadow_map", shadowTex2D ? shadowTex2D : GetDummyTexture2D());
						shader->BindTexture("shadow_buffer", shadowCube ? shadowCube : GetDummyCubeTexture());
						shader->BindTexture("shadow_buffer_csm", shadowTexCSM ? shadowTexCSM : GetDummyTexture2DArray());
						shader->BindInt("u_shadow_type", shadowType);
					}
				}
			}
			shader->BindInt("u_light_type", lightType);
		}

		// glTF-standard skinning: skinned vertices reach world space via
		// Final = J_i W * ibm (Joint::GetFinalMatrix), so the mesh node's
		// own world transform must NOT be applied on top -- bind identity.
		if (mesh->IsSkinnedMesh())
			shader->BindMatrix(Matrix4::WORLD_MATRIX, Matrix4());
		else
			shader->BindMatrix(Matrix4::WORLD_MATRIX, node->GetWorldMatrix());

		if (meshChanged)
			shader->BindMesh(mesh);

		// Per-instance LOD debug tint. When the LOD_DEBUG_COLORS switch is
		// on, push the active LOD's deterministic color onto the shader so
		// the fragment can replace/tint its output. When the switch is off,
		// we still bind the uniform -- but to vec4(0) -- because OpenGL
		// program objects retain their last-set uniform values indefinitely,
		// so a "do nothing" here would leave the previous frame's green
		// baked in. The shader's `lod_debug_color.a > 0.0` gate treats
		// alpha = 0 as "no override" and passes the diffuse through.
#if WITH_EDITOR
		if (m_Switches.test((size_t)PipelineSwitch::LOD_DEBUG_COLORS))
		{
			Color lodColor = GetLodDebugColor(render->GetActiveLod());
			shader->BindFloat("lod_debug_color", lodColor.r, lodColor.g, lodColor.b, lodColor.a);
		}
		else
		{
			shader->BindFloat("lod_debug_color", 0.0f, 0.0f, 0.0f, 0.0f);
		}
#endif

		if (mesh->GetSubMeshCount() > 0)
		{
			auto subMesh = mesh->GetSubMeshAt(unit.subMesh);
			shader->BindSubMesh(mesh, unit.subMesh);
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(subMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(subMesh->Indices.Data.size()));
		}
		else
		{
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(mesh->Indices.Data.size()));
		}

		//shader->UnBind();

		// TODO: Maybe subMeshCount ?
		if (mesh->IsSkinnedMesh())
			RenderUtil::Instance()->IncreaseSkinnedMeshCount();
		else
			RenderUtil::Instance()->IncreaseMeshCount();

		RenderUtil::Instance()->IncreaseDrawCall();
	}

	void PrelightPipeline::DrawPointLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto light = node->GetComponent<Light>();
		auto camPtr = m_CurrentCamera->GetComponent<Camera>();
		auto camPos = m_CurrentCamera->GetWorldPosition();
		auto mesh = light->GetMesh();
		auto worldMatrix = node->GetWorldMatrix();

		Shader::Ptr shader = nullptr;
		bool castShadows = light->GetCastShadows();

		// find correct shader.
		shader = GetShaderByName(castShadows ? "pointlight_shadow_shader" : "pointlight_shader");
		if (shader == nullptr)
		{
			FURYW << "Shader for light " << node->GetName() << " not found!";
			return;
		}

		// draw shadowMap if we castShadows.
		std::pair<Texture::Ptr, Matrix4> shadowData;
		if (castShadows)
			shadowData = DrawPointLightShadowMap(sceneManager, pass, node);

		// ready to draw light volumn
		pass->Bind(false);

		// change depthTest && face culling state.
		{
			float camNear = (camPtr->GetFrustum().GetCurrentCorners()[0] - camPos).Length();
			if (SphereBounds(node->GetWorldPosition(), light->GetEffectiveRadius() + camNear).IsInsideFast(camPos))
			{
				glDisable(GL_DEPTH_TEST);
				glCullFace(GL_FRONT);
			}
			else
			{
				glEnable(GL_DEPTH_TEST);
				glCullFace(GL_BACK);
			}

			worldMatrix.AppendScale(Vector4(light->GetRadius(), 0.0f));
		}

		shader->Bind();

		shader->BindCamera(m_CurrentCamera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

		if (castShadows && shadowData.first != nullptr)
		{
			shader->BindTexture("shadow_buffer", shadowData.first);
			shader->BindMatrix("shadow_matrix", &shadowData.second.Raw[0]);
		}

		shader->BindLight(node);
		shader->BindMesh(mesh);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer (released at end of Execute --
		// the transparent pass samples it for shadow-receiving)
		if (castShadows)
			m_FrameShadowTemps.push_back(shadowData.first);
	}

	void PrelightPipeline::DrawDirLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto light = node->GetComponent<Light>();
		auto camPtr = m_CurrentCamera->GetComponent<Camera>();
		auto mesh = light->GetMesh();
		auto worldMatrix = node->GetWorldMatrix();

		Shader::Ptr shader = nullptr;
		bool castShadows = light->GetCastShadows();
		bool useCascaded = IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP);

		// find correct shader.
		shader = GetShaderByName(castShadows ?
			(useCascaded ? "dirlight_csm_shader" : "dirlight_shadow_shader") : "dirlight_shader");
		if (shader == nullptr)
		{
			FURYW << "Shader for light " << node->GetName() << " not found!";
			return;
		}

		// draw shadowMap if we castShadows.
		std::pair<Texture::Ptr, std::vector<Matrix4>> cascadedShadowData;
		std::pair<Texture::Ptr, Matrix4> shadowData;
		if (castShadows)
		{
			if (useCascaded)
				cascadedShadowData = DrawCascadedShadowMap(sceneManager, pass, node);
			else
				shadowData = DrawDirLightShadowMap(sceneManager, pass, node);
		}

		// ready to draw light volumn
		pass->Bind(false);

		// change depthTest && face culling state.
		glEnable(GL_DEPTH_TEST);
		glCullFace(GL_BACK);

		shader->Bind();

		shader->BindCamera(m_CurrentCamera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

		if (castShadows)
		{
			if (useCascaded && cascadedShadowData.first != nullptr)
			{
				shader->BindTexture("shadow_buffer", cascadedShadowData.first);
				// for cacasded shadow maps
				shader->BindMatrices("shadow_matrix", static_cast<int>(cascadedShadowData.second.size()), &cascadedShadowData.second[0]);
				// split distances from the same source the map render used
				float splits[4];
				if (Scene::Active && Scene::Active->GetRenderSettings())
					Scene::Active->GetRenderSettings()->ComputeCsmSplits(camPtr->GetNear(), camPtr->GetFar(), splits);
				else
					for (int i = 0; i < 4; i++)
						splits[i] = camPtr->GetNear() + (camPtr->GetFar() - camPtr->GetNear()) * (i + 1) / 4.0f;
				shader->BindFloat("shadow_far", splits[0], splits[1], splits[2], splits[3]);
			}
			else if (shadowData.first != nullptr)
			{
				shader->BindTexture("shadow_buffer", shadowData.first);
				shader->BindMatrix("shadow_matrix", &shadowData.second.Raw[0]);
			}
		}

		shader->BindLight(node);
		shader->BindMesh(mesh);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer (released at end of Execute);
		// cache the matrix data for the transparent pass's
		// shadow-receive path. CSM: the 4 cascade matrices +
		// shadow_far (linear-quarter-far split, matches the
		// deferred SunLight.glsl CSM block).
		if (castShadows)
		{
			if (useCascaded && cascadedShadowData.first != nullptr)
			{
				m_FrameShadowTemps.push_back(cascadedShadowData.first);
				ShadowData data;
				data.csm = cascadedShadowData.second;
				auto camPtr2 = m_CurrentCamera->GetComponent<Camera>();
				float base = camPtr2->GetFar() - camPtr2->GetNear();
				float avg = base / 4.0f;
				data.shadowFar = Vector4(-avg, -avg * 2, -avg * 3, -avg * 4);
				m_LastShadowMatrices[node.get()] = std::move(data);
			}
			else if (shadowData.first != nullptr)
			{
				m_FrameShadowTemps.push_back(shadowData.first);
				ShadowData data;
				data.single = shadowData.second;
				m_LastShadowMatrices[node.get()] = std::move(data);
			}
		}
	}

	void PrelightPipeline::DrawSpotLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto light = node->GetComponent<Light>();
		auto camPtr = m_CurrentCamera->GetComponent<Camera>();
		auto camPos = m_CurrentCamera->GetWorldPosition();
		auto mesh = light->GetMesh();
		auto worldMatrix = node->GetWorldMatrix();

		Shader::Ptr shader = nullptr;
		bool castShadows = light->GetCastShadows();

		// find correct shader.
		shader = GetShaderByName(castShadows ? "spotlight_shadow_shader" : "spotlight_shader");
		if (shader == nullptr)
		{
			FURYW << "Shader for light " << node->GetName() << " not found!";
			return;
		}

		// draw shadowMap if we castShadows.
		std::pair<Texture::Ptr, Matrix4> shadowData;
		if (castShadows)
			shadowData = DrawSpotLightShadowMap(sceneManager, pass, node);

		// ready to draw light volumn
		pass->Bind(false);

		// change depthTest && face culling state.
		{
			auto coneCenter = node->GetWorldPosition();
			auto coneDir = worldMatrix.Multiply(Vector4(0, -1, 0, 0)).Normalized();

			float camNear = (camPtr->GetFrustum().GetCurrentCorners()[0] - camPos).Length();
			float theta = light->GetOutterAngle() * 0.5f;
			float height = light->GetEffectiveRadius();
			float extra = camNear / std::sin(theta);

			coneCenter = coneCenter - coneDir * extra;
			height += camNear + extra;

			if (MathUtil::PointInCone(coneCenter, coneDir, height, theta, camPos))
			{
				glDisable(GL_DEPTH_TEST);
				glCullFace(GL_FRONT);
			}
			else
			{
				glEnable(GL_DEPTH_TEST);
				glCullFace(GL_BACK);
			}
		}

		shader->Bind();

		shader->BindCamera(m_CurrentCamera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

		if (castShadows && shadowData.first != nullptr)
		{
			shader->BindTexture("shadow_buffer", shadowData.first);
			shader->BindMatrix("shadow_matrix", &shadowData.second.Raw[0]);
		}

		shader->BindLight(node);
		shader->BindMesh(mesh);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer (released at end of Execute);
		// cache the spot view->shadow UV matrix for the transparent
		// pass + particle block.
		if (castShadows && shadowData.first != nullptr)
		{
			m_FrameShadowTemps.push_back(shadowData.first);
			ShadowData data;
			data.single = shadowData.second;
			m_LastShadowMatrices[node.get()] = std::move(data);
		}
	}

	void PrelightPipeline::DrawQuad(const std::shared_ptr<Pass> &pass)
	{
		auto shader = m_CurrentShader;
		auto mesh = MeshUtil::GetUnitQuad();

		if (shader == nullptr)
		{
			FURYW << "Failed to draw full screen quad, shader not found!";
			return;
		}

		shader->Bind();

		shader->BindMesh(mesh);
		shader->BindCamera(m_CurrentCamera);

		// When rendering into the editor's offscreen viewport RT (a
		// non-sRGB RGBA8 FBO), GL_FRAMEBUFFER_SRGB is a no-op, so the
		// lambert shader gamma-encodes its output itself to keep the
		// viewport from rendering too dark. The default-framebuffer path
		// leaves this 0 and lets GL_FRAMEBUFFER_SRGB do the encoding.
		// Only screen-bound passes (no output textures) encode --
		// intermediate composites (e.g. LDR pass_combine -> ldr_composite)
		// must stay linear.
		shader->BindInt("u_gamma_correct",
			(m_RenderTarget != nullptr && pass->GetTextureCount(false) == 0) ? 1 : 0);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		// Aerial-perspective bindings for the combine pass (no-ops on
		// shaders without these uniforms). Sampler always bound: dummy 3D
		// when no sky is active.
		if (auto sky = IsHDRMode() ? SkyAtmosphere::GetActive() : nullptr;
			sky != nullptr && sky->GetEnabled() && sky->GetCameraVolume() != nullptr)
		{
			shader->BindInt("u_atmosphere_enabled", 1);
			shader->BindFloat("u_ap_range", sky->GetApRangeKm());
			shader->BindTexture("u_ap_volume", sky->GetCameraVolume());
			// small sky-ambient lift while a sky drives the scene: keeps
			// away-facing slopes from crushing to pure black (no IBL)
			shader->BindFloat("u_ambient", 0.03f + 0.05f * sky->GetDaylight());
		}
		else
		{
			shader->BindInt("u_atmosphere_enabled", 0);
			shader->BindTexture("u_ap_volume", GetDummyTexture3D());
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(mesh->Indices.Data.size()));
	}

	void PrelightPipeline::DrawSky(const std::shared_ptr<Pass> &pass)
	{
		// HDR-only feature; no sky -> no draw, hdr_composite untouched.
		if (!IsHDRMode())
			return;
		auto sky = SkyAtmosphere::GetActive();
		if (sky == nullptr || !sky->GetEnabled())
			return;
		auto shader = m_CurrentShader != nullptr ? m_CurrentShader : pass->GetFirstShader();
		if (shader == nullptr || m_CurrentCamera == nullptr)
			return;

		// LUT/volume/cloud renders bind their own FBOs, so this runs before
		// the pass bind.
		sky->EnsureLuts(m_CurrentCamera);

		pass->Bind(false);   // never clear: hdr_composite holds the scene
		shader->Bind();

		auto mesh = MeshUtil::GetUnitQuad();
		shader->BindMesh(mesh);
		shader->BindCamera(m_CurrentCamera);
		sky->BindAtmosphereUniforms(shader);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		shader->BindTexture("u_skyview_lut", sky->GetSkyViewLut());
		shader->BindTexture("u_transmittance_lut", sky->GetTransmittanceLut());
		shader->BindTexture("u_cloud_tex", sky->GetCloudsEnabled()
			? sky->GetCloudTarget() : GetDummyTexture2D());
		shader->BindTexture("u_moon_tex", sky->GetMoonTexture()
			? sky->GetMoonTexture() : GetDummyTexture2D());

		shader->BindFloat("u_sun_ang_cos", cosf(sky->GetSunAngularRadius()));
		shader->BindFloat("u_sun_disc_intensity", sky->GetSunDiscIntensity());
		shader->BindFloat("u_moon_dir", sky->GetMoonDirection().x, sky->GetMoonDirection().y, sky->GetMoonDirection().z);
		shader->BindFloat("u_moon_ang_cos", cosf(sky->GetMoonAngularRadius()));
		shader->BindFloat("u_moon_frame_scale", 1.0f / tanf(sky->GetMoonAngularRadius()));
		shader->BindFloat("u_moon_intensity", sky->GetMoonIntensity());
		shader->BindInt("u_moon_enabled", sky->GetMoonEnabled() && sky->GetMoonTexture() ? 1 : 0);
		shader->BindInt("u_clouds_enabled", sky->GetCloudsEnabled() ? 1 : 0);

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
	}

	void PrelightPipeline::DrawOcean(const std::shared_ptr<Pass> &pass, const std::shared_ptr<RenderQuery> &query)
	{
		// HDR-only feature (same constraint as the sky pass).
		if (!IsHDRMode())
			return;
		if (m_CurrentCamera == nullptr || query == nullptr || query->oceanNodes.empty())
			return;
		auto shader = m_CurrentShader != nullptr ? m_CurrentShader : pass->GetFirstShader();
		if (shader == nullptr)
			return;

		// Pre-pass depth copy: shore foam samples the opaque scene depth,
		// which is also this pass's depth attachment (sampling the bound
		// attachment would be a feedback loop). Depth copies go through
		// glBlitFramebuffer (RenderUtil::Blit is a color draw).
		Texture::Ptr depthCopy;
		auto depthSrc = GetTextureByName("gbuffer_depth");
		if (depthSrc)
		{
			depthCopy = Texture::GetTemporary(depthSrc->GetWidth(), depthSrc->GetHeight(),
				1, depthSrc->GetFormat(), TextureType::TEXTURE_2D);

			static unsigned int s_DepthCopyFbos[2] = { 0, 0 };
			if (s_DepthCopyFbos[0] == 0)
				glGenFramebuffers(2, s_DepthCopyFbos);
			int w = depthSrc->GetWidth();
			int h = depthSrc->GetHeight();
			glBindFramebuffer(GL_READ_FRAMEBUFFER, s_DepthCopyFbos[0]);
			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthSrc->GetID(), 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_DepthCopyFbos[1]);
			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthCopy->GetID(), 0);
			glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}

		pass->Bind(false); // never clear: hdr_composite holds the lit scene
		shader->Bind();
		shader->BindCamera(m_CurrentCamera);

		// per-buffer blend: color0 (hdr_composite) alpha-blends; color1
		// (gbuffer_normal) must stay replace or normals get smeared.
		// glEnablei/glDisablei are GL 3.0 core; per-buffer blend FUNC
		// (glBlendFunci) is 4.0-only, so the func stays global and
		// buffer 1 simply keeps blending disabled.
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnablei(GL_BLEND, 0);
		glDisablei(GL_BLEND, 1);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		// note: the GetDummyTexture* helpers are free functions in
		// namespace fury (RenderUtil.cpp), not RenderUtil members
		if (depthCopy)
			shader->BindTexture("u_scene_depth", depthCopy);
		else
			shader->BindTexture("u_scene_depth", GetDummyTexture2D());
		shader->BindFloat("u_rt_size", (float)pass->GetTextureAt(0, false)->GetWidth(),
			(float)pass->GetTextureAt(0, false)->GetHeight());

		// dominant directional light for the sun term (BindLight uniforms)
		SceneNode::Ptr sunNode;
		for (const auto &lightNode : query->lightNodes)
		{
			auto light = lightNode->GetComponent<Light>();
			if (light && light->GetType() == LightType::DIRECTIONAL)
			{
				sunNode = lightNode;
				break;
			}
		}
		if (sunNode)
		{
			shader->BindLight(sunNode);
			shader->BindInt("u_light_valid", 1);
		}
		else
		{
			shader->BindInt("u_light_valid", 0);
		}

		// CSM shadow-receive from the frame's cached sun map (transparent
		// pass reads the same caches)
		int shadowType = 0;
		if (sunNode)
		{
			auto texIt = m_LastShadowTextures.find(sunNode.get());
			auto matIt = m_LastShadowMatrices.find(sunNode.get());
			if (texIt != m_LastShadowTextures.end() && texIt->second &&
				matIt != m_LastShadowMatrices.end() && matIt->second.csm.size() == 4)
			{
				shader->BindTexture("shadow_buffer_csm", texIt->second);
				shader->BindMatrices("shadow_matrix_csm", 4, matIt->second.csm.data());
				shader->BindFloat("shadow_far", matIt->second.shadowFar.x,
					matIt->second.shadowFar.y, matIt->second.shadowFar.z,
					matIt->second.shadowFar.w);
				shadowType = 3;
			}
		}
		if (shadowType == 0)
			shader->BindTexture("shadow_buffer_csm", GetDummyTexture2DArray());
		shader->BindInt("u_shadow_type", shadowType);

		// aerial perspective volume (same source as PbrCombine)
		auto sky = SkyAtmosphere::GetActive();
		if (sky && sky->GetEnabled() && sky->GetCameraVolume())
		{
			shader->BindTexture("u_ap_volume", sky->GetCameraVolume());
			shader->BindFloat("u_ap_range", sky->GetApRangeKm());
			shader->BindInt("u_atmosphere_enabled", 1);
			// sky-view LUT: true sky color for reflections + the fog
			// convergence target (rendered fresh in pass_sky above)
			shader->BindTexture("u_skyview_lut", sky->GetSkyViewLut());
			shader->BindFloat("u_bottom_radius", sky->GetBottomRadiusKm());
			shader->BindFloat("u_view_height", sky->GetViewHeightKm());
			// moonlight: the sun light dims to zero at night, but the water
			// should keep a cool moon glint (diffuse + a capped spec path)
			Vector4 moonDir = sky->GetMoonDirection();
			shader->BindFloat("u_moon_dir", moonDir.x, moonDir.y, moonDir.z);
			shader->BindFloat("u_moon_intensity", sky->GetMoonEnabled()
				? sky->GetMoonIntensity() : 0.0f);
		}
		else
		{
			shader->BindTexture("u_ap_volume", GetDummyTexture3D());
			shader->BindInt("u_atmosphere_enabled", 0);
			shader->BindTexture("u_skyview_lut", GetDummyTexture2D());
			shader->BindFloat("u_bottom_radius", 6360.0f);
			shader->BindFloat("u_view_height", 0.0f);
			shader->BindFloat("u_moon_dir", 0.0f, -1.0f, 0.0f);
			shader->BindFloat("u_moon_intensity", 0.0f);
		}

		Vector4 camPos = m_CurrentCamera->GetWorldPosition();
		// camera-radial band fades are centered on the camera (VS)
		shader->BindFloat("u_cam_xz", camPos.x, camPos.z);
		for (const auto &node : query->oceanNodes)
		{
			auto ocean = node->GetComponent<OceanComponent>();
			if (!ocean)
				continue;

			ocean->UpdateCameraFollow(camPos);
			auto waves = ocean->GetWaves();
			bool valid = waves && waves->IsValid();

			Vector4 nodePos = node->GetWorldPosition();
			shader->BindFloat("u_water_level", nodePos.y + ocean->GetWaterLevel());
			shader->BindFloat("u_time", ocean->GetWaveTime());
			shader->BindInt("u_waves_valid", valid ? 1 : 0);

			if (valid)
			{
				// roles by tile (bands are sorted tile-ascending at load):
				// swell = largest, chop = smallest when a 3rd cascade exists,
				// ripple = the one in between. The chop band contributes
				// normals + foam only (no vertex displacement).
				const int bandCount = waves->GetBandCount();
				const auto &swellBand = waves->GetBand(bandCount - 1);
				const auto &rippleBand = waves->GetBand(bandCount >= 3 ? 1 : 0);
				const bool hasChop = bandCount >= 3;
				const auto &chopBand = waves->GetBand(0);

				shader->BindFloat("u_loop_seconds", waves->GetLoopSeconds());
				shader->BindFloat("u_frames", (float)waves->GetFrameCount());
				shader->BindFloat("u_swell_tile", swellBand.TileCm);
				shader->BindFloat("u_ripple_tile", rippleBand.TileCm);
				shader->BindTexture("u_disp_swell", swellBand.DispTexture);
				shader->BindTexture("u_disp_ripple", rippleBand.DispTexture);
				shader->BindTexture("u_nrm_swell", swellBand.NrmTexture);
				shader->BindTexture("u_nrm_ripple", rippleBand.NrmTexture);
				shader->BindInt("u_chop_valid", hasChop ? 1 : 0);
				if (hasChop)
				{
					shader->BindFloat("u_chop_tile", chopBand.TileCm);
					shader->BindTexture("u_nrm_chop", chopBand.NrmTexture);
				}
				else
				{
					shader->BindFloat("u_chop_tile", 300.0f);
					shader->BindTexture("u_nrm_chop", GetDummyTexture2DArray());
				}
			}
			else
			{
				shader->BindFloat("u_loop_seconds", 12.0f);
				shader->BindFloat("u_frames", 32.0f);
				shader->BindFloat("u_swell_tile", 10000.0f);
				shader->BindFloat("u_ripple_tile", 800.0f);
				shader->BindTexture("u_disp_swell", GetDummyTexture2DArray());
				shader->BindTexture("u_disp_ripple", GetDummyTexture2DArray());
				shader->BindTexture("u_nrm_swell", GetDummyTexture2DArray());
				shader->BindTexture("u_nrm_ripple", GetDummyTexture2DArray());
				shader->BindInt("u_chop_valid", 0);
				shader->BindFloat("u_chop_tile", 300.0f);
				shader->BindTexture("u_nrm_chop", GetDummyTexture2DArray());
			}

			Color absorb = ocean->GetAbsorbColor();
			Color scatter = ocean->GetScatterColor();
			shader->BindFloat("u_absorb_color", absorb.r, absorb.g, absorb.b);
			shader->BindFloat("u_scatter_color", scatter.r, scatter.g, scatter.b);
			shader->BindFloat("u_roughness", ocean->GetRoughness());
			// always the true roughness: the SSAO water gate reads the same
			// gbuffer alpha as SSR (SSR's own roughness < 0.95 gate is
			// unaffected), so SSR-off water must not write 1.0 here
			shader->BindFloat("u_ssr_roughness", ocean->GetRoughness());
			shader->BindFloat("u_normal_strength", ocean->GetNormalStrength());
			shader->BindFloat("u_foam_amount", ocean->GetFoamAmount());
			shader->BindFloat("u_shore_foam_depth", ocean->GetShoreFoamDepthCm());
			shader->BindFloat("u_wind_speed", ocean->GetWindSpeed());
			shader->BindInt("u_debug_view", (int)ocean->GetDebugView());
			shader->BindFloat("u_disp_debug_scale", 0.02f);

			// camera-radial band fades: ranges derive from the ring radii;
			// the legacy per-piece uniforms survive as multipliers (1 = on)
			const Vector4 &fadeRanges = ocean->GetFadeRanges();
			shader->BindFloat("u_fade_ranges", fadeRanges.x, fadeRanges.y,
				fadeRanges.z, fadeRanges.w);
			shader->BindFloat("u_swell_fade", 1.0f);
			shader->BindFloat("u_ripple_fade", 1.0f);

			// distance fog converges far water to the sky horizon color;
			// fully fogged at the skirt radius so the far edge never reads.
			// The start follows the last ring's outer radius (never below
			// 1 km): with more rings the detailed band reaches further, and
			// a fixed start left a hard fog band against it.
			float fogStart = std::max(100000.0f, fadeRanges.w);
			float fogEnd = std::max(ocean->GetSkirtRadiusCm(), fogStart * 1.01f);
			shader->BindFloat("u_fog_start", fogStart);
			shader->BindFloat("u_fog_end", fogEnd);

			// debug view 3: ring-LOD wireframe via polygon mode (no CPU
			// line lists; restored right after this ocean's draws)
			bool wireframe = ocean->GetDebugView() == 3;
			if (wireframe)
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

			if (ocean->GetMode() == OceanComponent::Mode::Finite)
			{
				auto mesh = ocean->GetFiniteMesh();
				if (!mesh)
					continue;
				shader->BindFloat("u_world_origin", nodePos.x, nodePos.y, nodePos.z);
				shader->BindFloat("u_y_offset", 0.0f);
				shader->BindFloat("u_debug_color", 0.2f, 0.9f, 0.9f);
				mesh->UpdateBuffer();
				shader->BindMesh(mesh);
				glDrawElements(GL_TRIANGLES, (GLsizei)mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
				RenderUtil::Instance()->IncreaseDrawCall();
			}
			else
			{
				// per-piece wireframe colors: center white, rings cycle, skirt gray
				static const float kPieceColors[][3] = {
					{ 1.0f, 1.0f, 1.0f }, { 0.95f, 0.6f, 0.2f }, { 0.3f, 0.9f, 0.4f },
					{ 0.35f, 0.6f, 1.0f }, { 0.9f, 0.4f, 0.9f }, { 0.5f, 0.5f, 0.5f }
				};
				int pieceIndex = 0;
				for (const auto &piece : ocean->GetRingPieces())
				{
					if (!piece.MeshPtr)
						continue;
					const float *pc = kPieceColors[std::min<int>(piece.IsSkirt ? 5 : pieceIndex, 5)];
					shader->BindFloat("u_world_origin", piece.Origin.x, piece.Origin.y, piece.Origin.z);
					shader->BindFloat("u_y_offset", piece.YOffset);
					shader->BindFloat("u_debug_color", pc[0], pc[1], pc[2]);
					piece.MeshPtr->UpdateBuffer();
					shader->BindMesh(piece.MeshPtr);
					glDrawElements(GL_TRIANGLES, (GLsizei)piece.MeshPtr->Indices.Data.size(), GL_UNSIGNED_INT, 0);
					RenderUtil::Instance()->IncreaseDrawCall();
					++pieceIndex;
				}
			}

			if (wireframe)
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}

		// restore: blend/depth state leaves with the pass
		glDisablei(GL_BLEND, 0);
		glDepthMask(GL_TRUE);
		shader->UnBind();

		if (depthCopy)
			Texture::ReleaseTemporary(depthCopy);
	}

	std::shared_ptr<Texture> PrelightPipeline::GetLightingOutputTexture() const
	{
		// hdr_composite (HDR) or ldr_composite (LDR), with fallbacks
		// for legacy pipelines that predate the composite textures.
		Texture::Ptr sourceTex = nullptr;
		if (IsHDRMode())
		{
			sourceTex = GetTextureByName("hdr_composite");
			if (!sourceTex) sourceTex = GetTextureByName("hdr_light");
			if (!sourceTex) sourceTex = GetTextureByName("gbuffer_light");
		}
		else
		{
			sourceTex = GetTextureByName("ldr_composite");
			if (!sourceTex) sourceTex = GetTextureByName("gbuffer_light");
			if (!sourceTex) sourceTex = GetTextureByName("hdr_light");
		}
		return sourceTex;
	}

	void PrelightPipeline::RunPostProcessChain()
	{
		if (m_ActiveChain.empty()) return;
		if (m_CurrentCamera == nullptr) return;

		// Chain input: the pipeline's lighting output texture.
		Texture::Ptr sourceTex = GetLightingOutputTexture();
		if (!sourceTex)
		{
			FURYW << "RunPostProcessChain: no lighting output texture "
					 "found (gbuffer_light/hdr_composite) -- chain skipped";
			return;
		}

		// Ping-pong texture dims match the source. Chain effects
		// produce a same-size intermediate.
		const int W = sourceTex->GetWidth() > 0 ? sourceTex->GetWidth() : 1280;
		const int H = sourceTex->GetHeight() > 0 ? sourceTex->GetHeight() : 720;
		const TextureFormat fmt = sourceTex->GetFormat();

		// Ping-pong via the temp pool: fresh temp per intermediate
		// step, each read texture released after its consuming draw --
		// read and write never alias, nothing leaks.
		Texture::Ptr readTex = sourceTex;

		auto quad = MeshUtil::GetUnitQuad();
		const bool toRT = (m_RenderTarget != nullptr && m_RenderTarget->IsAllocated());

		// A throwaway Pass we configure once to host each temp
		// texture as an FBO color attachment. Pass owns the FBO +
		// viewport wiring so we don't have to repeat the GL calls
		// per effect.
		auto chainPass = Pass::Create("ChainFBO");
		chainPass->SetBlendMode(BlendMode::REPLACE);
		chainPass->SetClearMode(ClearMode::COLOR);
		chainPass->SetClearColor(Color(0, 0, 0, 1));

		// Final-write target: editor offscreen RT or default FB. We
		// bind one of these manually as the FBO color attachment
		// for the last effect.
		GLint prev_fbo = 0;
		GLint prev_vp[4] = {0, 0, 0, 0};
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
		glGetIntegerv(GL_VIEWPORT, prev_vp);

		auto bindScreenFBO = [&]()
		{
			if (toRT)
			{
				glBindFramebuffer(GL_FRAMEBUFFER, m_RenderTarget->GetFBO());
				glViewport(0, 0, m_RenderTarget->GetWidth(), m_RenderTarget->GetHeight());
			}
			else
			{
				// Final blit: track the live window, not the source's 1280x720.
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				int winW, winH;
				InputUtil::Instance()->GetWindowSize(winW, winH);
				glViewport(0, 0, winW > 0 ? winW : W, winH > 0 ? winH : H);
			}
		};

		for (size_t i = 0; i < m_ActiveChain.size(); ++i)
		{
			auto &effect = m_ActiveChain[i];
			if (!effect) continue;
			const bool isLast = (i + 1 == m_ActiveChain.size());

			// Compile shader on first use (cached by path+mode); LDR variants get the `LDR` define via a `|ldr`-suffixed key so effects can branch on HDR-only data -- e.g. SSR falls back to u_ldr_roughness when Lambert packs no roughness in normal.a.
			std::string shaderKey = effect->GetShaderPath();
			if (!IsHDRMode())
				shaderKey += "|ldr";
			auto shader = GetShaderByName(shaderKey);
			if (!shader)
			{
				shader = Shader::Create(shaderKey, ShaderType::OTHER);
				for (const auto &d : effect->GetShaderDefines())
					shader->AddDefine(d);
				if (!IsHDRMode())
					shader->AddDefine("LDR");
				if (!shader->LoadAndCompile(effect->GetShaderPath()))
				{
					FURYE << "RunPostProcessChain: failed to compile effect '"
						  << effect->GetName() << "' (" << effect->GetShaderPath() << ")";
					continue;
				}
				m_EntityManager->Add(shader);
			}

			// Resolve inputs before acquiring the write target so a
			// missing reserved texture skips the effect without leaking
			// a temp. `$`-prefixed names are reserved pipeline textures
			// bound read-only ($scene = previous chain output,
			// $gbuffer_* = G-buffer, $hdr_light/$ldr_composite =
			// lighting targets); plain names keep the legacy behavior
			// (previous output). Sampler uniform = name without `$`.
			std::vector<std::pair<std::string, Texture::Ptr>> resolvedInputs;
			bool missingInput = false;
			for (const auto &inputName : effect->GetInputs())
			{
				if (!inputName.empty() && inputName[0] == '$')
				{
					// $-prefix resolves against named textures (or the current frame); bare names reuse the stage output.
					Texture::Ptr reserved = (inputName == "$scene")
						? readTex : GetTextureByName(inputName.substr(1));
					if (!reserved)
					{
						FURYW << "RunPostProcessChain: effect '" << effect->GetName()
							  << "' input '" << inputName << "' not found -- effect skipped";
						missingInput = true;
						break;
					}
					resolvedInputs.emplace_back(inputName.substr(1), reserved);
				}
				else
				{
					resolvedInputs.emplace_back(inputName, readTex);
				}
			}
			if (missingInput)
				continue;

			// Final write goes to the screen/RT directly; earlier
			// steps write into a freshly-acquired temp via chainPass.
			int writeW, writeH;
			Texture::Ptr writeTex = nullptr;
			if (isLast)
			{
				bindScreenFBO();
				if (toRT)
				{
					writeW = m_RenderTarget->GetWidth();
					writeH = m_RenderTarget->GetHeight();
				}
				else
				{
					// u_rt_size mirrors the live viewport (FXAA/CRT need it).
					int winW, winH;
					InputUtil::Instance()->GetWindowSize(winW, winH);
					writeW = winW > 0 ? winW : W;
					writeH = winH > 0 ? winH : H;
				}
			}
			else
			{
				writeTex = Texture::GetTemporary(W, H, 0, fmt, TextureType::TEXTURE_2D);
				chainPass->RemoveAllTextures();
				chainPass->AddTexture(writeTex, false);
				chainPass->Bind(false);
				writeW = W;
				writeH = H;
			}

			shader->Bind();
			shader->BindMesh(quad);
			shader->BindCamera(m_CurrentCamera);

			// Chain draws are full-screen REPLACES -- never inherit
			// pipeline state. Without this, the screen-bound final
			// effect runs with whatever the last pass left behind:
			// pass_transparent exits with GL_BLEND + glBlendFunc(ONE,
			// ONE) from its additive light loop, so a single-effect
			// chain (e.g. [ACES]) ADDED its output into the render
			// target every frame -- the "disable FXAA -> progressive
			// overexposure" bug. Multi-effect chains only looked clean
			// because the intermediate chainPass->Bind(REPLACE) reset
			// the blend state before the final draw.
			glDisable(GL_BLEND);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			glDisable(GL_CULL_FACE);

			// u_rt_size: pixels (FXAA + CRT need it for fwidth /
			// scanline frequency).
			shader->BindFloat("u_rt_size", (float)writeW, (float)writeH);

			// Only the final chain effect sRGB-encodes; intermediates stay linear.
			shader->BindInt("u_gamma_correct", isLast ? 1 : 0);

			for (const auto &kv : resolvedInputs)
				shader->BindTexture(kv.first, kv.second);

			// Apply the effect's declared default uniforms, then this
			// entry's per-instance overrides (only names the effect
			// declares; unknown override names are ignored).
			for (const auto &kv : effect->GetUniforms())
			{
				auto &u = kv.second;
				if (!u) continue;
				u->Bind(shader->GetProgram(), kv.first);
			}
			if (i < m_ActiveChainOverrides.size())
			{
				for (const auto &kv : m_ActiveChainOverrides[i])
				{
					if (!kv.second) continue;
					if (effect->GetUniforms().find(kv.first) == effect->GetUniforms().end())
						continue;
					kv.second->Bind(shader->GetProgram(), kv.first);
				}
			}

			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quad->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			shader->UnBind();

			RenderUtil::Instance()->IncreaseDrawCall();
			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(quad->Indices.Data.size()));

			if (!isLast)
				chainPass->UnBind();

			// Release the previous read texture if it was a temp
			// (sourceTex is owned by the pipeline entity manager;
			// don't release it). On the final step writeTex is
			// nullptr, so readTex becomes null and nothing dangles.
			if (readTex != sourceTex)
				Texture::ReleaseTemporary(readTex);

			readTex = writeTex;
		}

		// Restore default FB + viewport so subsequent draws (editor
		// debug overlay, GUI) render against the original target.
		// Render state goes back to the engine's boring defaults
		// (depth on, blend off) -- DrawDebug and ImGui set their own
		// state on top of this.
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
		glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}

	void PrelightPipeline::DrawEffectDebugView(const std::string &effectName)
	{
		if (m_CurrentCamera == nullptr) return;

		auto effect = PostProcessRegistry::Get(effectName);
		if (!effect)
		{
			SetDebugViewTexture(nullptr);
			return;
		}

		Texture::Ptr sourceTex = GetLightingOutputTexture();
		if (!sourceTex)
		{
			SetDebugViewTexture(nullptr);
			return;
		}

		// (Re)allocate the debug texture at the composite's size. The
		// entity manager owns it by name so the Profiler's GBuffer tab
		// finds it like any pipeline texture.
		const int W = sourceTex->GetWidth() > 0 ? sourceTex->GetWidth() : 1280;
		const int H = sourceTex->GetHeight() > 0 ? sourceTex->GetHeight() : 720;
		auto debugTex = GetTextureByName("debug_view");
		if (!debugTex || debugTex->GetWidth() != W || debugTex->GetHeight() != H)
		{
			debugTex = Texture::Create("debug_view");
			debugTex->CreateEmpty(W, H, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			m_EntityManager->Add(debugTex);
		}

		// DEBUG_VIEW variant of the effect shader (own cache key).
		std::string shaderKey = effect->GetShaderPath() + "|debug";
		if (!IsHDRMode()) shaderKey += "|ldr";
		auto shader = GetShaderByName(shaderKey);
		if (!shader)
		{
			shader = Shader::Create(shaderKey, ShaderType::OTHER);
			for (const auto &d : effect->GetShaderDefines())
				shader->AddDefine(d);
			shader->AddDefine("DEBUG_VIEW");
			if (!IsHDRMode())
				shader->AddDefine("LDR");
			if (!shader->LoadAndCompile(effect->GetShaderPath()))
			{
				FURYE << "DrawEffectDebugView: failed to compile debug view for '"
					  << effect->GetName() << "'";
				SetDebugViewTexture(nullptr);
				return;
			}
			m_EntityManager->Add(shader);
		}

		// Resolve inputs exactly like the chain runner ($-prefixed
		// reserved names; $scene / plain names = lighting output).
		std::vector<std::pair<std::string, Texture::Ptr>> resolvedInputs;
		for (const auto &inputName : effect->GetInputs())
		{
			if (!inputName.empty() && inputName[0] == '$')
			{
				Texture::Ptr reserved = (inputName == "$scene")
					? sourceTex : GetTextureByName(inputName.substr(1));
				if (!reserved)
				{
					FURYW << "DrawEffectDebugView: effect '" << effect->GetName()
						  << "' input '" << inputName << "' not found -- view skipped";
					SetDebugViewTexture(nullptr);
					return;
				}
				resolvedInputs.emplace_back(inputName.substr(1), reserved);
			}
			else
			{
				resolvedInputs.emplace_back(inputName, sourceTex);
			}
		}

		// Render into the debug texture. Same state rules as the
		// chain: fullscreen replace, never inherit pass state.
		GLint prev_fbo = 0;
		GLint prev_vp[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
		glGetIntegerv(GL_VIEWPORT, prev_vp);

		auto debugPass = Pass::Create("DebugViewFBO");
		debugPass->SetBlendMode(BlendMode::REPLACE);
		debugPass->SetClearMode(ClearMode::COLOR);
		debugPass->AddTexture(debugTex, false);
		debugPass->Bind(false);

		auto quad = MeshUtil::GetUnitQuad();
		shader->Bind();
		shader->BindMesh(quad);
		shader->BindCamera(m_CurrentCamera);
		shader->BindFloat("u_rt_size", (float)W, (float)H);
		// The debug texture is displayed as-is by ImGui (non-sRGB):
		// encode here so the view matches the viewport's brightness.
		shader->BindInt("u_gamma_correct", 1);

		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);

		for (const auto &kv : resolvedInputs)
			shader->BindTexture(kv.first, kv.second);

		// Descriptor defaults, then the scene entry's overrides (so
		// the Edit dialog tunes the debug view live).
		for (const auto &kv : effect->GetUniforms())
		{
			auto &u = kv.second;
			if (!u) continue;
			u->Bind(shader->GetProgram(), kv.first);
		}
		if (Scene::Active && Scene::Active->GetRenderSettings())
		{
			for (const auto &entry : Scene::Active->GetRenderSettings()->GetChain())
			{
				if (entry.effectName != effectName) continue;
				for (const auto &kv : entry.uniformOverrides)
				{
					if (!kv.second) continue;
					if (effect->GetUniforms().find(kv.first) == effect->GetUniforms().end())
						continue;
					kv.second->Bind(shader->GetProgram(), kv.first);
				}
				break;
			}
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quad->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();
		debugPass->UnBind();

		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
		glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(quad->Indices.Data.size()));

		SetDebugViewTexture(debugTex);
	}
}