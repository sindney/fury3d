#include <algorithm>
#include <sstream>

#include "Fury/BoxBounds.h"
#include "Fury/BuoyancyComponent.h"
#include "Fury/Camera.h"
#include "Fury/Log.h"
#include "Fury/Light.h"
#include "Fury/EnumUtil.h"
#include "Fury/EntityManager.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/RenderSettings.h"
#include "Fury/Uniform.h"
#include "Fury/Scene.h"
#include "Fury/FileUtil.h"
#include "Fury/Frustum.h"
#include "Fury/GLLoader.h"
#include "Fury/Material.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/Pipeline.h"
#include "Fury/Pass.h"
#include "Fury/RenderTarget.h"
#include "Fury/RenderUtil.h"
#include "Fury/RenderQuery.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/OcTree.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/Shader.h"
#include "Fury/SphereBounds.h"
#include "Fury/Texture.h"

namespace fury
{
	Pipeline::Ptr Pipeline::Active = nullptr;

	Pipeline::Pipeline(const std::string &name) : Entity(name)
	{
		m_TypeIndex = typeid(Pipeline);

		m_SharedPass = Pass::Create("SharedPass");

		m_OffsetMatrix = Matrix4({
			0.5, 0.0, 0.0, 0.0,
			0.0, 0.5, 0.0, 0.0,
			0.0, 0.0, 0.5, 0.0,
			0.5, 0.5, 0.5, 1.0
		});

		m_Switches.reset();

		m_EntityManager = EntityManager::Create();
	}

	Pipeline::~Pipeline()
	{
		FURYD << "Pipeline " << m_Name << " destoried!";
	}

	bool Pipeline::Load(const void* wrapper, bool object)
	{
		std::string str;

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		// A pipeline JSON is a complete definition: drop previously
		// loaded render entities before parsing.
		m_EntityManager->RemoveAll<Texture>();
		m_EntityManager->RemoveAll<Shader>();
		m_EntityManager->RemoveAll<Pass>();
		m_SortedPasses.clear();

		if (!LoadArray(wrapper, "textures", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				FURYE << "Texture param 'name' not found!";
				return false;
			}

			auto texture = Texture::Create(str);
			if (!texture->Load(node))
				return false;

			m_EntityManager->Add(texture);

			return true;
		}))
		{
			FURYE << "Error reading texture array!";
			return false;
		}

		if (!LoadArray(wrapper, "shaders", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				FURYE << "Shader param 'name' not found!";
				return false;
			}

			auto shader = Shader::Create(str, ShaderType::OTHER);
			if (!shader->Load(node))
				return false;

			m_EntityManager->Add(shader);

			return true;
		}))
		{
			FURYE << "Error reading shader array!";
			return false;
		}

		if (!LoadArray(wrapper, "passes", [&](const void* node) -> bool
		{
			if (!LoadMemberValue(node, "name", str))
			{
				FURYE << "Pass param 'name' not found!";
				return false;
			}

			auto pass = Pass::Create(str);
			if (!pass->Load(node))
				return false;

			m_EntityManager->Add(pass);

			return true;
		}))
		{
			FURYE << "Error reading pass array!";
			return false;
		}

		return true;
	}

	void Pipeline::Save(void* wrapper, bool object)
	{
		std::vector<std::string> strs;

		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "textures");
		SaveArray<Texture>(wrapper, m_EntityManager, [&](const std::shared_ptr<Texture> &ptr)
		{
			ptr->Save(wrapper);
		});

		SaveKey(wrapper, "shaders");
		SaveArray<Shader>(wrapper, m_EntityManager, [&](const std::shared_ptr<Shader> &ptr)
		{
			ptr->Save(wrapper);
		});

		SaveKey(wrapper, "passes");
		SaveArray<Pass>(wrapper, m_EntityManager, [&](const std::shared_ptr<Pass> &ptr)
		{
			ptr->Save(wrapper);
		});

		if (object)
			EndObject(wrapper);
	}

	std::shared_ptr<EntityManager> Pipeline::GetEntityManager() const
	{
		return m_EntityManager;
	}

	void Pipeline::SetSwitch(PipelineSwitch key, bool value)
	{
		m_Switches.set((unsigned int)key, value);
	}

	bool Pipeline::IsSwitchOn(PipelineSwitch key)
	{
		return m_Switches.test((unsigned int)key);
	}

	bool Pipeline::IsSwitchOn(std::initializer_list<PipelineSwitch> list, bool any)
	{
		for (auto key : list)
		{
			bool value = IsSwitchOn(key);
			if (any)
			{
				if (value)
					return true;
			}
			else
			{
				if (!value)
					return false;
			}
		}
		return any ? false : true;
	}

	void Pipeline::SetHDRMode(bool value)
	{
		m_HDRMode = value;
	}

	bool Pipeline::IsHDRMode() const
	{
		return m_HDRMode;
	}

	void Pipeline::SetActiveChain(const std::vector<std::shared_ptr<PostProcessEffect>> &chain)
	{
		m_ActiveChain = chain;
	}

	const std::vector<std::shared_ptr<PostProcessEffect>> &Pipeline::GetActiveChain() const
	{
		return m_ActiveChain;
	}

	void Pipeline::ApplyRenderSettings(const RenderSettings &settings)
	{
		m_HDRMode = settings.IsHDR();
		SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, settings.IsCascadedShadowMap());

		// Chain resolution. Entry order in the scene file is IGNORED
		// by design: effects run in the engine-owned canonical order
		// (stage, order, name -- see PostProcessRegistry::GetSortedAll),
		// users only toggle them on/off. Tonemapping is not a user
		// choice either: exactly one tonemap-stage effect runs in HDR
		// (auto-injected when missing, duplicates dropped) and none in
		// LDR (stripped). Duplicate names collapse (first enabled
		// wins) so old scenes saved with dupes can't double-apply.
		std::vector<std::shared_ptr<PostProcessEffect>> resolved;
		std::vector<std::unordered_map<std::string, std::shared_ptr<UniformBase>>> overrides;
		std::unordered_map<std::string, bool> seen;
		bool haveTonemap = false;
		for (const auto &entry : settings.GetChain())
		{
			auto effect = PostProcessRegistry::Get(entry.effectName);
			if (!effect)
			{
				if (entry.enabled)
					FURYW << "Pipeline::ApplyRenderSettings: postprocess effect '"
						  << entry.effectName
						  << "' is not registered; skipping (scene still loads)";
				continue;
			}
			if (seen.count(entry.effectName))
				continue;
			seen[entry.effectName] = true;

			if (effect->GetStage() == PostProcessStage::TONEMAP)
			{
				// Tonemap follows the HDR switch, not the entry's
				// enabled flag: keep it in HDR even when the saved
				// entry is disabled, drop it in LDR even when enabled.
				if (!m_HDRMode)
					continue;
				if (haveTonemap)
				{
					FURYW << "Pipeline::ApplyRenderSettings: multiple tonemap-stage "
						 "effects in chain; keeping the first, dropping '"
					  << entry.effectName << "'";
					continue;
				}
				haveTonemap = true;
				resolved.push_back(effect);
				overrides.push_back(entry.uniformOverrides);
				continue;
			}

			if (!entry.enabled)
				continue;
			resolved.push_back(effect);
			overrides.push_back(entry.uniformOverrides);
		}

		// HDR without a tonemap entry: inject ACES so HDR output is
		// always tonemapped. Uniform overrides from a saved (disabled)
		// ACES entry ride along, so exposure edits survive toggles.
		if (m_HDRMode && !haveTonemap)
		{
			if (auto aces = PostProcessRegistry::Get("ACES"))
			{
				resolved.push_back(aces);
				std::unordered_map<std::string, std::shared_ptr<UniformBase>> acesOverrides;
				for (const auto &entry : settings.GetChain())
				{
					if (entry.effectName == "ACES")
					{
						acesOverrides = entry.uniformOverrides;
						break;
					}
				}
				overrides.push_back(std::move(acesOverrides));
			}
			else
			{
				static bool warnedOnce = false;
				if (!warnedOnce)
				{
					FURYW << "Pipeline::ApplyRenderSettings: HDR is on but no ACES "
						 "effect is registered -- output will not be tonemapped!";
					warnedOnce = true;
				}
			}
		}

		// Canonical order: (stage, order, name). Overrides stay
		// index-aligned with their effect.
		std::vector<size_t> order(resolved.size());
		for (size_t i = 0; i < order.size(); ++i) order[i] = i;
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
		{
			auto ea = resolved[a], eb = resolved[b];
			if (ea->GetStage() != eb->GetStage())
				return ea->GetStage() < eb->GetStage();
			if (ea->GetOrder() != eb->GetOrder())
				return ea->GetOrder() < eb->GetOrder();
			return ea->GetName() < eb->GetName();
		});

		m_ActiveChain.clear();
		m_ActiveChainOverrides.clear();
		for (size_t i : order)
		{
			m_ActiveChain.push_back(resolved[i]);
			m_ActiveChainOverrides.push_back(std::move(overrides[i]));
		}
	}

	bool Pipeline::HasHDRComposite() const
	{
		// The HDR pipeline JSON declares a `hdr_composite` rgba16f
		// target that holds lighting * diffuse before tonemapping.
		// Its presence is what tells the chain "this pipeline can
		// produce a valid HDR composite for the postprocess pass".
		return GetTextureByName("hdr_composite") != nullptr;
	}

	void Pipeline::SortPassByIndex()
	{
		using DataPair = std::pair<unsigned int, std::string>;

		std::vector<DataPair> wrapper;
		wrapper.reserve(m_EntityManager->Count<Pass>());

		m_EntityManager->ForEach<Pass>([&](const Pass::Ptr &ptr) -> bool
		{
			wrapper.push_back(std::make_pair(ptr->GetRenderIndex(), ptr->GetName()));
			return true;
		});

		std::sort(wrapper.begin(), wrapper.end(), [](const DataPair &a, const DataPair &b)
		{
			return a.first < b.first;
		});

		m_SortedPasses.erase(m_SortedPasses.begin(), m_SortedPasses.end());
		m_SortedPasses.reserve(wrapper.size());
		for (auto pair : wrapper)
			m_SortedPasses.push_back(pair.second);
	}

	void Pipeline::ClearDebugCollidables()
	{
		m_DebugBoxBounds.clear();
		m_DebugFrustum.clear();
	}

	void Pipeline::AddDebugCollidable(const BoxBounds &bounds)
	{
		m_DebugBoxBounds.push_back(bounds);
	}

	void Pipeline::AddDebugCollidable(const Frustum &bounds)
	{
		m_DebugFrustum.push_back(bounds);
	}

	std::shared_ptr<Pass> Pipeline::GetPassByName(const std::string &name)
	{
		return m_EntityManager->Get<Pass>(name);
	}

	std::shared_ptr<Texture> Pipeline::GetTextureByName(const std::string &name) const
	{
		return m_EntityManager->Get<Texture>(name);
	}

	std::shared_ptr<Shader> Pipeline::GetShaderByName(const std::string &name)
	{
		return m_EntityManager->Get<Shader>(name);
	}

	std::shared_ptr<Texture> Pipeline::GetLastShadowTexture(const SceneNode &lightNode) const
	{
		auto it = m_LastShadowTextures.find(const_cast<SceneNode*>(&lightNode));
		if (it == m_LastShadowTextures.end())
			return nullptr;
		return it->second;
	}

	std::shared_ptr<SceneNode> Pipeline::GetCurrentCamera() const
	{
		return m_CurrentCamera;
	}

	void Pipeline::SetCurrentCamera(const std::shared_ptr<SceneNode> &ptr)
	{
		m_CurrentCamera = ptr;
	}

	void Pipeline::SetRenderTarget(RenderTarget* target)
	{
		m_RenderTarget = target;
	}

	RenderTarget* Pipeline::GetRenderTarget() const
	{
		return m_RenderTarget;
	}

	void Pipeline::SetDebugViewTexture(const std::shared_ptr<Texture> &ptr)
	{
		m_DebugViewTexture = ptr;
	}

	std::shared_ptr<Texture> Pipeline::GetDebugViewTexture() const
	{
		return m_DebugViewTexture;
	}

	void Pipeline::FilterNodes(const Collidable &collider, std::vector<std::shared_ptr<SceneNode>> &possibles, std::vector<std::shared_ptr<SceneNode>> &collisions)
	{
		collisions.erase(collisions.begin(), collisions.end());

		for (auto possible : possibles)
		{
			if (collider.IsInsideFast(possible->GetWorldAABB()))
				collisions.push_back(possible);
		}
	}

	Matrix4 Pipeline::GetCropMatrix(Matrix4 lightMatrix, Frustum frustum, std::vector<std::shared_ptr<SceneNode>> &casters)
	{
		// limit z
		auto corners = frustum.GetCurrentCorners();
		auto pos = lightMatrix.Multiply(corners[0]);
		float minZ = pos.z, maxZ = pos.z;
		for (auto corner : corners)
		{
			pos = lightMatrix.Multiply(corner);
			if (pos.z > maxZ) maxZ = pos.z;
			if (pos.z < minZ) minZ = pos.z;
		}

		for (auto caster : casters)
		{
			auto casterCorners = caster->GetWorldAABB().GetCorners();
			for (auto corner : casterCorners)
			{
				pos = lightMatrix.Multiply(corner);
				if (pos.z > maxZ) maxZ = pos.z;
			}
		}

		Matrix4 projMatrix;
		projMatrix.OrthoOffCenter(-1.0f, 1.0f, -1.0f, 1.0f, maxZ, minZ);

		// limit xy
		float maxX = 0.0f, maxY = 0.0f;
		float minX = 0.0f, minY = 0.0f;

		auto mvp = projMatrix * lightMatrix;

		pos = mvp.Multiply(corners[0]);
		maxX = minX = pos.x / pos.w;
		maxY = minY = pos.y / pos.w;

		for (auto corner : corners)
		{
			pos = mvp.Multiply(corner);

			pos.x /= pos.w;
			pos.y /= pos.w;

			if (pos.x > maxX) maxX = pos.x;
			if (pos.x < minX) minX = pos.x;
			if (pos.y > maxY) maxY = pos.y;
			if (pos.y < minY) minY = pos.y;
		}

		// build crop matrix
		float scaleX = 2.0f / (maxX - minX);
		float scaleY = 2.0f / (maxY - minY);
		float offsetX = -0.5f * (maxX + minX) * scaleX;
		float offsetY = -0.5f * (maxY + minY) * scaleY;

		Matrix4 cropMatrix(
		{
			scaleX, 0.0f, 0.0f, 0.0f,
			0.0f, scaleY, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			offsetX, offsetY, 0.0f, 1.0f
		});

		return projMatrix * cropMatrix;
	}

	std::pair<std::shared_ptr<Texture>, std::vector<Matrix4>> Pipeline::DrawCascadedShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		(void)pass;
		const int numSplit = 4;

		// get pointers
		auto depth_shader = GetShaderByName("leagcy_depth_shader");
		auto depth_skin_shader = GetShaderByName("leagcy_depth_skin_shader");
		auto camera = m_CurrentCamera->GetComponent<Camera>();

		// map size from the scene's render settings (default 1024)
		int csmSize = 1024;
		if (Scene::Active && Scene::Active->GetRenderSettings())
			csmSize = Scene::Active->GetRenderSettings()->GetCsmMapSize();

		auto depth_buffer = Texture::GetTemporary(csmSize, csmSize, 4, TextureFormat::DEPTH24, TextureType::TEXTURE_2D_ARRAY);
		depth_buffer->SetBorderColor(Color::White);
		depth_buffer->SetWrapMode(WrapMode::CLAMP_TO_BORDER);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		Matrix4 lightMatrix;
		lightMatrix.Rotate(MathUtil::AxisRadToQuat(Vector4::XAxis, MathUtil::DegToRad * 90.0f));
		lightMatrix = lightMatrix * node->GetInvertWorldMatrix();

		// build frustums (splits from render settings: shadow far range +
		// linear/log blend; the light shader's cascade picker matches)
		std::array<Frustum, numSplit> frustums;
		float splits[numSplit];
		if (Scene::Active && Scene::Active->GetRenderSettings())
			Scene::Active->GetRenderSettings()->ComputeCsmSplits(camera->GetNear(), camera->GetFar(), splits);
		else
			for (int i = 0; i < numSplit; i++)
				splits[i] = camera->GetNear() + (camera->GetFar() - camera->GetNear()) * (i + 1) / numSplit;
		float curNear = camera->GetNear();
		for (int i = 0; i < numSplit; i++)
		{
			frustums[i] = camera->GetFrustum(curNear, splits[i]);
			curNear = splits[i];
		}

		// find shadow casters
		fury::SceneManager::SceneNodes casterAll;
		sceneManager->GetVisibleShadowCasters(camera->GetFrustum(), casterAll);

		std::array<fury::SceneManager::SceneNodes, numSplit> casterArrays;
		for (int i = 0; i < numSplit; i++)
		{
			auto &casters = casterArrays[i];
			auto &frustum = frustums[i];
			FilterNodes(frustum, casterAll, casters);
		}

		// use camera aabb to include more possible shadow casters to cast shadows.
		if (camera->GetShadowBounds(false).GetExtents().SquareLength() > 0)
			sceneManager->GetVisibleShadowCasters(camera->GetShadowBounds(), casterArrays[0], false);

		// build projection/crop matrices
		std::array<Matrix4, numSplit> projMatrices;
		for (int i = 0; i < numSplit; i++)
		{
			auto &matrix = projMatrices[i];
			auto &frustum = frustums[i];
			auto &casters = casterArrays[i];
			matrix = GetCropMatrix(lightMatrix, frustum, casters);
		}

		// draw casters to depth map, aka shadow map.
		{
			m_SharedPass->RemoveAllTextures();
			m_SharedPass->AddTexture(depth_buffer, false);

			m_SharedPass->SetBlendMode(BlendMode::REPLACE);
			m_SharedPass->SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);
			m_SharedPass->SetClearColor(Color::White);
			m_SharedPass->SetCompareMode(CompareMode::LESS);
			m_SharedPass->SetCullMode(CullMode::BACK);

			m_SharedPass->Bind();

			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(1.0f, 1024.0f);

			// Skinned casters use the skin depth shader (bone_matrices +
			// identity world_matrix, matching the gbuffer skin path) so
			// their shadows deform with the skeleton. Static casters use
			// the plain depth shader with the caster's world matrix.
			Matrix4 identityWorld;
			Shader* boundShader = nullptr;
			auto bindDepthShader = [&](const std::shared_ptr<Shader>& s) {
				if (!s || boundShader == s.get()) return;
				if (boundShader) boundShader->UnBind();
				s->Bind();
				s->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &lightMatrix.Raw[0]);
				boundShader = s.get();
			};

			for (int i = 0; i < numSplit; i++)
			{
				m_SharedPass->SetArrayTextureLayer(i);

				auto &casters = casterArrays[i];
				for (auto &caster : casters)
				{
					auto casterRender = caster->GetComponent<MeshRender>();
					auto casterMesh = casterRender->GetMesh();
					const bool skinned = casterMesh->IsSkinnedMesh() && depth_skin_shader;
					auto& shader = skinned ? depth_skin_shader : depth_shader;
					bindDepthShader(shader);
					shader->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrices[i].Raw[0]);
					shader->BindMesh(casterMesh);
					if (skinned)
						shader->BindMatrix(Matrix4::WORLD_MATRIX, &identityWorld.Raw[0]);
					else {
						Matrix4 w = caster->GetWorldMatrix();
						shader->BindMatrix(Matrix4::WORLD_MATRIX, &w.Raw[0]);
					}

					glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(casterMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
					RenderUtil::Instance()->IncreaseDrawCall();

					RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(casterMesh->Indices.Data.size()));
				}
			}

			glDisable(GL_POLYGON_OFFSET_FILL);
			if (boundShader) boundShader->UnBind();

			m_SharedPass->UnBind();
		}

		std::vector<Matrix4> matrices;
		for (int i = 0; i < numSplit; i++)
			matrices.push_back(m_OffsetMatrix * projMatrices[i] * lightMatrix * m_CurrentCamera->GetWorldMatrix());

		m_LastShadowTextures[node.get()] = depth_buffer;
		return std::make_pair(depth_buffer, matrices);
	}

	std::pair<std::shared_ptr<Texture>, Matrix4> Pipeline::DrawDirLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		(void)pass;
		// get pointers
		auto depth_shader = GetShaderByName("leagcy_depth_shader");
		auto depth_buffer = Texture::GetTemporary(1024, 1024, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D);
		depth_buffer->SetBorderColor(Color::White);
		depth_buffer->SetWrapMode(WrapMode::CLAMP_TO_BORDER);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		auto camera = m_CurrentCamera->GetComponent<Camera>();

		Matrix4 lightMatrix;
		lightMatrix.Rotate(MathUtil::AxisRadToQuat(Vector4::XAxis, MathUtil::DegToRad * 90.0f));
		lightMatrix = lightMatrix * node->GetInvertWorldMatrix();

		// gen camera frustum
		auto camFrustum = camera->GetFrustum(camera->GetNear(), camera->GetShadowFar());

		// find shadow casters
		fury::SceneManager::SceneNodes casters;
		sceneManager->GetVisibleShadowCasters(camFrustum, casters, false);

		// use camera aabb to include more possible shadow casters to cast shadows.
		if (camera->GetShadowBounds(false).GetExtents().SquareLength() > 0)
			sceneManager->GetVisibleShadowCasters(camera->GetShadowBounds(), casters, false);

		// gen projection matrix for light.
		Matrix4 projMatrix = GetCropMatrix(lightMatrix, camFrustum, casters);

		// draw casters to depth map, aka shadow map.
		{
			m_SharedPass->RemoveAllTextures();
			m_SharedPass->AddTexture(depth_buffer, false);

			m_SharedPass->SetBlendMode(BlendMode::REPLACE);
			m_SharedPass->SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);
			m_SharedPass->SetClearColor(Color::White);
			m_SharedPass->SetCompareMode(CompareMode::LESS);
			m_SharedPass->SetCullMode(CullMode::BACK);

			m_SharedPass->Bind();

			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(1.0f, 1024.0f);

			// Skinned casters use the skin depth shader (bone_matrices +
			// identity world_matrix) so their shadows deform; static
			// casters use the plain depth shader + caster world matrix.
			Matrix4 identityWorld;
			Shader* boundShader = nullptr;
			auto depth_skin_shader = GetShaderByName("leagcy_depth_skin_shader");
			auto bindDepthShader = [&](const std::shared_ptr<Shader>& s) {
				if (!s || boundShader == s.get()) return;
				if (boundShader) boundShader->UnBind();
				s->Bind();
				s->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &lightMatrix.Raw[0]);
				s->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrix.Raw[0]);
				boundShader = s.get();
			};

			for (auto &caster : casters)
			{
				auto casterRender = caster->GetComponent<MeshRender>();
				auto casterMesh = casterRender->GetMesh();
				const bool skinned = casterMesh->IsSkinnedMesh() && depth_skin_shader;
				auto& shader = skinned ? depth_skin_shader : depth_shader;
				bindDepthShader(shader);
				shader->BindMesh(casterMesh);
				if (skinned)
					shader->BindMatrix(Matrix4::WORLD_MATRIX, &identityWorld.Raw[0]);
				else {
					Matrix4 w = caster->GetWorldMatrix();
					shader->BindMatrix(Matrix4::WORLD_MATRIX, &w.Raw[0]);
				}

				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(casterMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
				RenderUtil::Instance()->IncreaseDrawCall();

				RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(casterMesh->Indices.Data.size()));
			}

			glDisable(GL_POLYGON_OFFSET_FILL);
			if (boundShader) boundShader->UnBind();

			m_SharedPass->UnBind();
		}

		m_LastShadowTextures[node.get()] = depth_buffer;
		return std::make_pair(depth_buffer, m_OffsetMatrix * projMatrix * lightMatrix * m_CurrentCamera->GetWorldMatrix());
	}

	std::pair<std::shared_ptr<Texture>, Matrix4> Pipeline::DrawPointLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		(void)pass;
		auto depth_shader = GetShaderByName("cube_depth_shader");
		auto depth_skin_shader = GetShaderByName("cube_depth_skin_shader");
		auto depth_buffer = Texture::GetTemporary(512, 512, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_CUBE_MAP);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		auto camera = m_CurrentCamera->GetComponent<Camera>();

		auto light = node->GetComponent<Light>();
		auto radius = light->GetEffectiveRadius();
		auto lightSphere = SphereBounds(node->GetWorldPosition(), radius);

		// TODO: filter casters for all six directions.
		fury::SceneManager::SceneNodes casters;
		sceneManager->GetVisibleShadowCasters(lightSphere, casters);

		float aspect = (float)depth_buffer->GetWidth() / depth_buffer->GetHeight();
		Matrix4 projMatrix;
		projMatrix.PerspectiveFov(MathUtil::DegToRad * 90.0f, aspect, 1.0f, radius);

		// dir matrices that points camera to all 6 directions.
		// right, left, top, bottom, back, front
		std::array<Matrix4, 6> dirMatrices;

		auto lightPos = node->GetWorldPosition();
		dirMatrices[0].LookAt(lightPos, lightPos + Vector4(1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
		dirMatrices[1].LookAt(lightPos, lightPos + Vector4(-1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
		dirMatrices[2].LookAt(lightPos, lightPos + Vector4(0.0f, 1.0f, 0.0f), Vector4(0.0f, 0.0f, 1.0f));
		dirMatrices[3].LookAt(lightPos, lightPos + Vector4(0.0f, -1.0f, 0.0f), Vector4(0.0f, 0.0f, -1.0f));
		dirMatrices[4].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, 1.0f), Vector4(0.0f, -1.0f, 0.0f));
		dirMatrices[5].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, -1.0f), Vector4(0.0f, -1.0f, 0.0f));

		// draw casters to depth map, aka shadow map.
		{
			m_SharedPass->RemoveAllTextures();
			m_SharedPass->AddTexture(depth_buffer, false);

			m_SharedPass->SetBlendMode(BlendMode::REPLACE);
			m_SharedPass->SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);
			m_SharedPass->SetClearColor(Color::White);
			m_SharedPass->SetCompareMode(CompareMode::LESS);
			m_SharedPass->SetCullMode(CullMode::BACK);

			m_SharedPass->Bind();

			/*glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(factor, units);*/

			depth_shader->Bind();
			depth_shader->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrix.Raw[0]);
			depth_shader->BindFloat("light_far", radius);
			depth_shader->BindFloat("light_pos", lightPos.x, lightPos.y, lightPos.z);

			Matrix4 identityWorld;
			Shader* boundShader = depth_shader.get();
			auto bindCubeShader = [&](const std::shared_ptr<Shader>& s) {
				if (!s || boundShader == s.get()) return;
				if (boundShader) boundShader->UnBind();
				s->Bind();
				s->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrix.Raw[0]);
				s->BindFloat("light_far", radius);
				s->BindFloat("light_pos", lightPos.x, lightPos.y, lightPos.z);
				boundShader = s.get();
			};

			for (int i = 0; i < 6; i++)
			{
				// TODO: test if it's necessary to clear after attach new cubemap face.
				m_SharedPass->SetCubeTextureIndex(i);
				m_SharedPass->Clear(m_SharedPass->GetClearMode(), m_SharedPass->GetClearColor());

				for (auto &caster : casters)
				{
					auto casterRender = caster->GetComponent<MeshRender>();
					auto casterMesh = casterRender->GetMesh();

					auto ivm = dirMatrices[i];

					const bool skinned = casterMesh->IsSkinnedMesh() && depth_skin_shader;
					auto& shader = skinned ? depth_skin_shader : depth_shader;
					bindCubeShader(shader);
					shader->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &ivm.Raw[0]);
					shader->BindMesh(casterMesh);
					if (skinned)
						shader->BindMatrix(Matrix4::WORLD_MATRIX, &identityWorld.Raw[0]);
					else {
						Matrix4 w = caster->GetWorldMatrix();
						shader->BindMatrix(Matrix4::WORLD_MATRIX, &w.Raw[0]);
					}

					glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(casterMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
					RenderUtil::Instance()->IncreaseDrawCall();

					RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(casterMesh->Indices.Data.size()));
				}
			}

			//glDisable(GL_POLYGON_OFFSET_FILL);
			depth_shader->UnBind();

			m_SharedPass->UnBind();
		}

		m_LastShadowTextures[node.get()] = depth_buffer;
		return std::make_pair(depth_buffer, m_CurrentCamera->GetWorldMatrix());
	}

	std::pair<std::shared_ptr<Texture>, Matrix4> Pipeline::DrawSpotLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		(void)pass;
		// get pointers
		auto depth_shader = GetShaderByName("leagcy_depth_shader");
		auto depth_buffer = Texture::GetTemporary(1024, 1024, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		depth_buffer->SetBorderColor(Color::White);
		depth_buffer->SetWrapMode(WrapMode::CLAMP_TO_BORDER);

		auto light = node->GetComponent<Light>();
		auto radius = light->GetEffectiveRadius();

		Matrix4 lightMatrix;
		lightMatrix.Rotate(MathUtil::AxisRadToQuat(Vector4::XAxis, MathUtil::DegToRad * 90.0f));
		lightMatrix = lightMatrix * node->GetInvertWorldMatrix();

		Frustum frustum;
		frustum.Setup(light->GetOutterAngle(), 1.0f, 1.0f, radius);
		frustum.Transform(lightMatrix.Inverse());

		// gen projection matrix for light.
		float aspect = (float)depth_buffer->GetWidth() / depth_buffer->GetHeight();
		Matrix4 projMatrix;
		projMatrix.PerspectiveFov(light->GetOutterAngle(), aspect, 1.0f, radius);

		// find shadow casters
		fury::SceneManager::SceneNodes casters;
		sceneManager->GetVisibleRenderables(frustum, casters);

		// draw casters to depth map, aka shadow map.
		{
			m_SharedPass->RemoveAllTextures();
			m_SharedPass->AddTexture(depth_buffer, false);

			m_SharedPass->SetBlendMode(BlendMode::REPLACE);
			m_SharedPass->SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);
			m_SharedPass->SetClearColor(Color::White);
			m_SharedPass->SetCompareMode(CompareMode::LESS);
			m_SharedPass->SetCullMode(CullMode::BACK);

			m_SharedPass->Bind();

			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(1.0f, 1024.0f);

			// Skinned casters use the skin depth shader (bone_matrices +
			// identity world_matrix) so their shadows deform; static
			// casters use the plain depth shader + caster world matrix.
			Matrix4 identityWorld;
			Shader* boundShader = nullptr;
			auto depth_skin_shader = GetShaderByName("leagcy_depth_skin_shader");
			auto bindDepthShader = [&](const std::shared_ptr<Shader>& s) {
				if (!s || boundShader == s.get()) return;
				if (boundShader) boundShader->UnBind();
				s->Bind();
				s->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &lightMatrix.Raw[0]);
				s->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrix.Raw[0]);
				boundShader = s.get();
			};

			for (auto &caster : casters)
			{
				auto casterRender = caster->GetComponent<MeshRender>();
				auto casterMesh = casterRender->GetMesh();
				const bool skinned = casterMesh->IsSkinnedMesh() && depth_skin_shader;
				auto& shader = skinned ? depth_skin_shader : depth_shader;
				bindDepthShader(shader);
				shader->BindMesh(casterMesh);
				if (skinned)
					shader->BindMatrix(Matrix4::WORLD_MATRIX, &identityWorld.Raw[0]);
				else {
					Matrix4 w = caster->GetWorldMatrix();
					shader->BindMatrix(Matrix4::WORLD_MATRIX, &w.Raw[0]);
				}

				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(casterMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
				RenderUtil::Instance()->IncreaseDrawCall();

				RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(casterMesh->Indices.Data.size()));
			}

			glDisable(GL_POLYGON_OFFSET_FILL);
			if (boundShader) boundShader->UnBind();

			m_SharedPass->UnBind();
		}

		m_LastShadowTextures[node.get()] = depth_buffer;
		return std::make_pair(depth_buffer, m_OffsetMatrix * projMatrix * lightMatrix * m_CurrentCamera->GetWorldMatrix());
	}

	void Pipeline::DrawDebug(const std::shared_ptr<RenderQuery> &query)
	{
		ASSERT_MSG(m_CurrentCamera != nullptr, "PrelightPipeline.m_CurrentCamera not found!");

		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		// Reference grid first: screen-space pass with no depth
		// interaction, so the bounds lines below composite over it.
		DrawEditorGrid();

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glDisable(GL_BLEND);

		auto meshBoundsOn = IsSwitchOn(PipelineSwitch::MESH_BOUNDS);
		auto customBoundsOn = IsSwitchOn(PipelineSwitch::CUSTOM_BOUNDS);
		auto lightBoundsOn = IsSwitchOn(PipelineSwitch::LIGHT_BOUNDS);

		auto renderUtil = RenderUtil::Instance();
		renderUtil->BeginDrawLines(m_CurrentCamera);

		if (meshBoundsOn)
		{
			for (auto node : query->renderableNodes)
				renderUtil->DrawBoxBounds(node->GetWorldAABB(), Color::White);
		}

		if (customBoundsOn)
		{
			for (const auto &bounds : m_DebugFrustum)
				renderUtil->DrawFrustum(bounds, Color::Green);

			for (const auto &bounds : m_DebugBoxBounds)
				renderUtil->DrawBoxBounds(bounds, Color::Green);
		}

		if (IsSwitchOn(PipelineSwitch::OCTREE_BOUNDS) && Scene::Active)
		{
			if (auto tree = std::dynamic_pointer_cast<OcTree>(Scene::Active->GetSceneManager()))
				tree->DrawDebugBounds(*renderUtil);
		}

		// Buoyancy float-point markers (component flag, works in play
		// sessions where no editor selection exists). Simulating bodies
		// color by last submersion (green dry -> red submerged).
		if (PhysicsWorld::Exists())
		{
			bool simulating = PhysicsWorld::Instance()->IsSimulationEnabled();
			for (const auto &weak : PhysicsWorld::Instance()->GetBuoyancies())
			{
				auto buoyancy = weak.lock();
				if (!buoyancy || !buoyancy->GetDebugDraw())
					continue;
				auto node = buoyancy->GetOwner();
				if (!node)
					continue;

				Matrix4 world = node->GetWorldMatrix();
				const auto &points = buoyancy->GetFloatPoints();
				for (unsigned int i = 0; i < points.size(); i++)
				{
					Vector4 center = world.Multiply(points[i].Offset);
					float r = points[i].Radius;
					Color color = Color(0.2f, 0.8f, 0.9f, 1.0f); // editor: cyan
					if (simulating)
					{
						float s = buoyancy->GetLastSubmersion(i);
						color = Color(0.2f + 0.75f * s, 0.9f - 0.65f * s, 0.2f, 1.0f);
					}
					float lines[18] = {
						center.x - r, center.y, center.z, center.x + r, center.y, center.z,
						center.x, center.y - r, center.z, center.x, center.y + r, center.z,
						center.x, center.y, center.z - r, center.x, center.y, center.z + r
					};
					renderUtil->DrawLines(lines, 18, color, LineMode::LINES);
				}
			}
		}

		renderUtil->EndDrawLines();

		renderUtil->BeginDrawMeshs(m_CurrentCamera);

		if (lightBoundsOn)
		{
			for (auto node : query->lightNodes)
			{
				auto light = node->GetComponent<Light>();
				if (light->GetType() == LightType::SPOT)
				{
					renderUtil->DrawMesh(light->GetMesh(), node->GetWorldMatrix(), light->GetColor());
				}
				else if (light->GetType() == LightType::POINT)
				{
					Matrix4 worldMatrix = node->GetWorldMatrix();
					worldMatrix.AppendScale(Vector4(light->GetRadius(), 0.0f));
					renderUtil->DrawMesh(light->GetMesh(), worldMatrix, light->GetColor());
				}
			}
		}

		renderUtil->EndDrawMeshes();

		glDisable(GL_DEPTH_TEST);
	}

	void Pipeline::DrawEditorGrid()
	{
		if (!IsSwitchOn(PipelineSwitch::EDITOR_GRID))
			return;

		// Needs the deferred depth texture to occlude grid lines
		// behind geometry; pipelines without one skip the grid.
		auto depth = GetTextureByName("gbuffer_depth");
		if (!depth || !m_CurrentCamera)
			return;

		static std::shared_ptr<Shader> gridShader;
		if (!gridShader)
		{
			// Screen-space reference grid on the XZ plane (engine
			// unit = 1 cm, so 100-unit minor / 1000-unit major
			// cells). The fragment reconstructs a world ray from the
			// inverse projection, intersects y=0, and emits a line
			// only where the plane point sits in front of the scene
			// depth (gbuffer stores -viewZ / camera_far).
			static const char* grid_vs =
				"#version 330\n"
				"in vec3 vertex_position;\n"
				"void main() {\n"
				"    gl_Position = vec4(vertex_position.xy, 0.0, 1.0);\n"
				"}\n";
			static const char* grid_fs =
				"#version 330\n"
				"out vec4 fragment_output;\n"
				"uniform sampler2D gbuffer_depth;\n"
				"uniform mat4 projection_matrix;\n"
				"uniform mat4 camera_world_matrix;\n"
				"uniform vec3 camera_pos;\n"
				"uniform float camera_far;\n"
				"uniform vec2 u_rt_size;\n"
				"float gridLine(vec2 p, float cell) {\n"
				"    vec2 q = p / cell;\n"
				"    vec2 g = abs(fract(q - 0.5) - 0.5) / fwidth(q);\n"
				"    return 1.0 - min(min(g.x, g.y), 1.0);\n"
				"}\n"
				"void main() {\n"
				"    vec2 uv = gl_FragCoord.xy / u_rt_size;\n"
				"    vec4 v = inverse(projection_matrix) * vec4(uv * 2.0 - 1.0, -1.0, 1.0);\n"
				"    vec3 view_dir = normalize(v.xyz / v.w);\n"
				"    float scene_z = texture(gbuffer_depth, uv).x * camera_far;\n"
				"    vec3 rd = normalize((camera_world_matrix * vec4(view_dir, 0.0)).xyz);\n"
				"    if (abs(rd.y) < 1e-6) discard;\n"
				"    float t = -camera_pos.y / rd.y;\n"
				"    if (t <= 0.0) discard;\n"
				"    vec3 pw = camera_pos + rd * t;\n"
				"    vec3 cam_fwd = -normalize(camera_world_matrix[2].xyz);\n"
				"    float grid_z = dot(pw - camera_pos, cam_fwd);\n"
				"    if (grid_z >= scene_z - 0.5) discard;\n"
				"    float minor = gridLine(pw.xz, 100.0);\n"
				"    float major = gridLine(pw.xz, 1000.0);\n"
				"    float fade = 1.0 - clamp(grid_z / camera_far, 0.0, 1.0);\n"
				"    fade *= fade;\n"
				"    float a = max(minor * 0.3, major * 0.6) * fade;\n"
				"    if (a < 0.004) discard;\n"
				"    fragment_output = vec4(vec3(0.6) + major * 0.2, a);\n"
				"}\n";

			gridShader = Shader::Create("EditorGridShader", ShaderType::OTHER);
			if (!gridShader->Compile(grid_vs, grid_fs, ""))
			{
				FURYE << "Failed to compile editor grid shader!";
				gridShader = nullptr;
				return;
			}
		}

		GLint vp[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_VIEWPORT, vp);

		auto quad = MeshUtil::GetUnitQuad();
		gridShader->Bind();
		gridShader->BindMesh(quad);
		gridShader->BindCamera(m_CurrentCamera);
		gridShader->BindTexture("gbuffer_depth", depth);
		gridShader->BindFloat("u_rt_size", (float)vp[2], (float)vp[3]);
		// NOTE: BindCamera's "invert_view_matrix" is the VIEW matrix
		// (world->view) in this engine's naming, not view->world -- the
		// world ray needs the camera's world matrix, bound explicitly.
		gridShader->BindMatrix("camera_world_matrix", m_CurrentCamera->GetWorldMatrix());

		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quad->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);

		gridShader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(quad->Indices.Data.size()));
	}
}