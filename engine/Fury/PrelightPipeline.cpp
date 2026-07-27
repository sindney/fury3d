#include <array>
#include <cmath>
#include <unordered_map>

#include "Fury/Camera.h"
#include "Fury/Log.h"
#include "Fury/EnumUtil.h"
#include "Fury/Frustum.h"
#include "Fury/GLLoader.h"
#include "Fury/Gui.h"
#include "Fury/Light.h"
#include "Fury/MathUtil.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/Pass.h"
#include "Fury/Pipeline.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/PrelightPipeline.h"
#include "Fury/RenderSettings.h"
#include "Fury/RenderTarget.h"
#include "Fury/RenderQuery.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/SphereBounds.h"
#include "Fury/Texture.h"

#ifdef WITH_EDITOR
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

		// Seed HDR / CSM / chain from the scene's renderSettings;
		// HDR mode always tonemaps.
		if (Scene::Active && Scene::Active->GetRenderSettings())
			ApplyRenderSettings(*Scene::Active->GetRenderSettings());
		if (IsHDRMode())
			EnsureTonemapInChain();

		// Drop last-frame's per-light shadow map cache. The map is
		// populated by Draw{Dir,Point,Spot,Cascaded}LightShadowMap
		// during the per-pass draw loop below and read by the editor's
		// Profiler -> Shadows tab after Execute returns. Clearing here
		// ensures light pointers from the previous frame cannot leak
		// into the new frame.
		m_LastShadowTextures.clear();

		// find visible nodes
		RenderQuery::Ptr query = RenderQuery::Create();
		sceneManager->GetRenderQuery(m_CurrentCamera->GetComponent<Camera>()->GetFrustum(), query);
		query->Sort(m_CurrentCamera->GetWorldPosition());

		// draw passes

		Texture::Ptr finalBuffer = nullptr;
		unsigned int passCount = m_SortedPasses.size();

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
				for (const auto &unit : query->transparentUnits)
					DrawUnit(pass, unit);
			}
			else if (drawMode == DrawMode::QUAD)
			{
				pass->Bind();
				DrawQuad(pass);
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
			PipelineSwitch::EDITOR_GRID }, true))
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

		// post
		m_CurrentShader = nullptr;
		m_CurrentMateral = nullptr;
		m_CurrentMesh = nullptr;
	}

	void PrelightPipeline::DrawUnit(const std::shared_ptr<Pass> &pass, const RenderUnit &unit)
	{
		auto node = unit.node;
		auto material = unit.material;

		// LOD selection: refresh the active LOD from the camera's
		// screen-coverage of the model's AABB, then draw the picked
		// mesh. When the MeshRender has no LodGroup bound,
		// GetActiveMesh() returns the original unit.mesh and
		// UpdateActiveLod is a no-op — preserving the pre-LOD draw
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
			shader = pass->GetShader(mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH,
			material->GetTextureFlags());

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
			shader->BindMaterial(material);

		// glTF-standard skinning: skinned vertices reach world space via
		// Final = JᵢW * ibm (Joint::GetFinalMatrix), so the mesh node's
		// own world transform must NOT be applied on top — bind identity.
		if (mesh->IsSkinnedMesh())
			shader->BindMatrix(Matrix4::WORLD_MATRIX, Matrix4());
		else
			shader->BindMatrix(Matrix4::WORLD_MATRIX, node->GetWorldMatrix());

		if (meshChanged)
			shader->BindMesh(mesh);

		// Per-instance LOD debug tint. When the LOD_DEBUG_COLORS switch is
		// on, push the active LOD's deterministic color onto the shader so
		// the fragment can replace/tint its output. When the switch is off,
		// we still bind the uniform — but to vec4(0) — because OpenGL
		// program objects retain their last-set uniform values indefinitely,
		// so a "do nothing" here would leave the previous frame's green
		// baked in. The shader's `lod_debug_color.a > 0.0` gate treats
		// alpha = 0 as "no override" and passes the diffuse through.
#ifdef WITH_EDITOR
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
			glDrawElements(GL_TRIANGLES, subMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(subMesh->Indices.Data.size());
		}
		else
		{
			glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(mesh->Indices.Data.size());
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

		glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer
		if (castShadows)
			Texture::ReleaseTemporary(shadowData.first);
	}

	void PrelightPipeline::DrawDirLight(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto light = node->GetComponent<Light>();
		auto camPtr = m_CurrentCamera->GetComponent<Camera>();
		auto camPos = m_CurrentCamera->GetWorldPosition();
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
				shader->BindMatrices("shadow_matrix", cascadedShadowData.second.size(), &cascadedShadowData.second[0]);
				float base = camPtr->GetFar() - camPtr->GetNear();
				float average = base / 4.0f;
				shader->BindFloat("shadow_far", average, average * 2, average * 3, average * 4);
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

		glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer
		if (castShadows)
		{
			if (useCascaded)
				Texture::ReleaseTemporary(cascadedShadowData.first);
			else
				Texture::ReleaseTemporary(shadowData.first);
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

		glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer
		if (castShadows)
			Texture::ReleaseTemporary(shadowData.first);
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
		shader->BindInt("u_gamma_correct", m_RenderTarget != nullptr ? 1 : 0);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(mesh->Indices.Data.size());
	}

	void PrelightPipeline::RunPostProcessChain()
	{
		if (m_ActiveChain.empty()) return;
		if (m_CurrentCamera == nullptr) return;

		// Chain input: hdr_composite (HDR) or gbuffer_light (LDR).
		Texture::Ptr sourceTex = nullptr;
		if (IsHDRMode())
		{
			sourceTex = GetTextureByName("hdr_composite");
			if (!sourceTex) sourceTex = GetTextureByName("hdr_light");
			if (!sourceTex) sourceTex = GetTextureByName("gbuffer_light");
		}
		else
		{
			sourceTex = GetTextureByName("gbuffer_light");
			if (!sourceTex) sourceTex = GetTextureByName("hdr_light");
		}
		if (!sourceTex)
		{
			FURYW << "RunPostProcessChain: no lighting output texture "
					 "found (gbuffer_light/hdr_composite) — chain skipped";
			return;
		}

		// Ping-pong texture dims match the source. Chain effects
		// produce a same-size intermediate.
		const int W = sourceTex->GetWidth() > 0 ? sourceTex->GetWidth() : 1280;
		const int H = sourceTex->GetHeight() > 0 ? sourceTex->GetHeight() : 720;
		const TextureFormat fmt = sourceTex->GetFormat();

		// Ping-pong via the temp pool: fresh temp per intermediate
		// step, each read texture released after its consuming draw —
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
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glViewport(0, 0, W, H);
			}
		};

		for (size_t i = 0; i < m_ActiveChain.size(); ++i)
		{
			auto &effect = m_ActiveChain[i];
			if (!effect) continue;
			const bool isLast = (i + 1 == m_ActiveChain.size());

			// Compile the effect shader on first use (cached by path+defines).
			auto shader = GetShaderByName(effect->GetShaderPath());
			if (!shader)
			{
				shader = Shader::Create(effect->GetShaderPath(), ShaderType::OTHER);
				for (const auto &d : effect->GetShaderDefines())
					shader->AddDefine(d);
				if (!shader->LoadAndCompile(effect->GetShaderPath()))
				{
					FURYE << "RunPostProcessChain: failed to compile effect '"
						  << effect->GetName() << "' (" << effect->GetShaderPath() << ")";
					continue;
				}
				m_EntityManager->Add(shader);
			}

			// Final write goes to the screen/RT directly; earlier
			// steps write into a freshly-acquired temp via chainPass.
			int writeW, writeH;
			Texture::Ptr writeTex = nullptr;
			if (isLast)
			{
				bindScreenFBO();
				writeW = toRT ? m_RenderTarget->GetWidth() : W;
				writeH = toRT ? m_RenderTarget->GetHeight() : H;
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

			// u_rt_size: pixels (FXAA + CRT need it for fwidth /
			// scanline frequency).
			shader->BindFloat("u_rt_size", (float)writeW, (float)writeH);

			// Only the chain's final effect sRGB-encodes;
			// intermediates stay linear.
			shader->BindInt("u_gamma_correct", isLast ? 1 : 0);

			// Bind the running texture to every declared input sampler.
			for (const auto &inputName : effect->GetInputs())
				shader->BindTexture(inputName, readTex);

			// Apply the effect's declared default uniforms. The
			// editor can override these per-instance; here we
			// use the JSON-declared values.
			for (const auto &kv : effect->GetUniforms())
			{
				auto &u = kv.second;
				if (!u) continue;
				u->Bind(shader->GetProgram(), kv.first);
			}

			glDrawElements(GL_TRIANGLES, quad->Indices.Data.size(), GL_UNSIGNED_INT, 0);

			shader->UnBind();

			RenderUtil::Instance()->IncreaseDrawCall();
			RenderUtil::Instance()->IncreaseTriangleCount(quad->Indices.Data.size());

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
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
		glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
	}
}