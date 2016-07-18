#include <algorithm>
#include <sstream>

#include "BoxBounds.h"
#include "Camera.h"
#include "Log.h"
#include "Light.h"
#include "EnumUtil.h"
#include "EntityManager.h"
#include "Scene.h"
#include "FileUtil.h"
#include "Frustum.h"
#include "GLLoader.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "MeshRender.h"
#include "Pipeline.h"
#include "Pass.h"
#include "RenderUtil.h"
#include "RenderQuery.h"
#include "SceneManager.h"
#include "SceneNode.h"
#include "Shader.h"
#include "SphereBounds.h"
#include "Texture.h"

namespace fury
{
	Pipeline::Ptr Pipeline::Active = nullptr;

	const std::string Pipeline::OPT_CASCADED_SHADOW_MAP = "cascaded_shadow_map";

	const std::string Pipeline::OPT_MESH_BOUNDS = "mesh_bounds";

	const std::string Pipeline::OPT_LIGHT_BOUNDS = "light_bounds";

	const std::string Pipeline::OPT_CUSTOM_BOUNDS = "custom_bounds";

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

		SetOption(OPT_CUSTOM_BOUNDS, false);
		SetOption(OPT_LIGHT_BOUNDS, false);
		SetOption(OPT_MESH_BOUNDS, false);

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

			m_TextureMap.emplace(texture->GetName(), texture);
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

			m_ShaderMap.emplace(shader->GetName(), shader);
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

			m_PassMap.emplace(str, pass);

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

		// TODO: maybe save to entityManager?
		SaveKey(wrapper, "textures");
		SaveArray<TextureMap>(wrapper, m_TextureMap, [&](TextureMap::const_iterator it)
		{
			it->second->Save(wrapper);
		});

		SaveKey(wrapper, "shaders");
		SaveArray<ShaderMap>(wrapper, m_ShaderMap, [&](ShaderMap::const_iterator it)
		{
			it->second->Save(wrapper);
		});

		SaveKey(wrapper, "passes");
		SaveArray<PassMap>(wrapper, m_PassMap, [&](PassMap::const_iterator it)
		{
			it->second->Save(wrapper);
		});

		if (object)
			EndObject(wrapper);
	}

	std::shared_ptr<EntityManager> Pipeline::GetEntityManager() const
	{
		return m_EntityManager;
	}

	void Pipeline::SortPassByIndex()
	{
		using DataPair = std::pair<unsigned int, std::string>;

		std::vector<DataPair> wrapper;
		wrapper.reserve(m_PassMap.size());

		for (auto pair : m_PassMap)
			wrapper.push_back(std::make_pair(pair.second->GetRenderIndex(), pair.first));

		std::sort(wrapper.begin(), wrapper.end(), [](const DataPair &a, const DataPair &b)
		{
			return a.first < b.first;
		});

		m_SortedPasses.erase(m_SortedPasses.begin(), m_SortedPasses.end());
		m_SortedPasses.reserve(wrapper.size());
		for (auto pair : wrapper)
			m_SortedPasses.push_back(pair.second);
	}

	void Pipeline::SetOption(const std::string &name, bool value)
	{
		m_Options[name].boolValue = value;
	}

	void Pipeline::SetOption(const std::string &name, float value)
	{
		m_Options[name].floatValue = value;
	}

	void Pipeline::SetOption(const std::string &name, int value)
	{
		m_Options[name].intValue = value;
	}

	std::pair<bool, PipelineOption> Pipeline::GetOption(const std::string &name)
	{
		auto it = m_Options.find(name);
		return std::make_pair(it != m_Options.end(), it->second);
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
		auto it = m_PassMap.find(name);
		if (it != m_PassMap.end())
			return it->second;

		return nullptr;
	}

	std::shared_ptr<Texture> Pipeline::GetTextureByName(const std::string &name)
	{
		auto it = m_TextureMap.find(name);
		if (it != m_TextureMap.end())
			return it->second;

		return nullptr;
	}

	std::shared_ptr<Shader> Pipeline::GetShaderByName(const std::string &name)
	{
		auto it = m_ShaderMap.find(name);
		if (it != m_ShaderMap.end())
			return it->second;

		return nullptr;
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
			auto corners = caster->GetWorldAABB().GetCorners();
			for (auto corner : corners)
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
		const int numSplit = 4;

		// get pointers
		auto depth_shader = GetShaderByName("leagcy_depth_shader");
		auto depth_buffer = Texture::Pool.Get(1024, 1024, 4, TextureFormat::DEPTH24, TextureType::TEXTURE_2D_ARRAY);
		depth_buffer->SetBorderColor(Color::White);
		depth_buffer->SetWrapMode(WrapMode::CLAMP_TO_BORDER);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		auto camNode = pass->GetCameraNode();
		auto camera = camNode->GetComponent<Camera>();

		Matrix4 lightMatrix;
		lightMatrix.Rotate(MathUtil::AxisRadToQuat(Vector4::XAxis, MathUtil::DegToRad * 90.0f));
		lightMatrix = lightMatrix * node->GetInvertWorldMatrix();

		// build frustums
		std::array<Frustum, numSplit> frustums;
		float average = (camera->GetFar() - camera->GetNear()) / (float)numSplit;
		float curNear = camera->GetNear();
		float curFar = curNear;
		for (int i = 0; i < numSplit; i++)
		{
			curFar += average;
			frustums[i] = camera->GetFrustum(curNear, curFar);
			curNear += average;
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

			depth_shader->Bind();
			depth_shader->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &lightMatrix.Raw[0]);

			for (int i = 0; i < numSplit; i++)
			{
				depth_shader->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrices[i].Raw[0]);

				m_SharedPass->SetArrayTextureLayer(i);

				auto &casters = casterArrays[i];
				for (auto &caster : casters)
				{
					auto casterRender = caster->GetComponent<MeshRender>();
					auto casterMesh = casterRender->GetMesh();

					depth_shader->BindMesh(casterMesh);
					depth_shader->BindMatrix(Matrix4::WORLD_MATRIX, &caster->GetWorldMatrix().Raw[0]);

					unsigned int subMeshCount = casterMesh->GetSubMeshCount();
					if (subMeshCount > 0)
					{
						for (unsigned int i = 0; i < subMeshCount; i++)
						{
							depth_shader->BindSubMesh(casterMesh, i);
							glDrawElements(GL_TRIANGLES, casterMesh->GetSubMeshAt(i)->Indices.Data.size(), GL_UNSIGNED_INT, 0);
							RenderUtil::Instance()->IncreaseDrawCall();
						}
					}
					else
					{
						depth_shader->BindMesh(casterMesh);
						glDrawElements(GL_TRIANGLES, casterMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
						RenderUtil::Instance()->IncreaseDrawCall();
					}

					RenderUtil::Instance()->IncreaseTriangleCount(casterMesh->Indices.Data.size());
				}
			}

			glDisable(GL_POLYGON_OFFSET_FILL);
			depth_shader->UnBind();

			m_SharedPass->UnBind();
		}

		std::vector<Matrix4> matrices;
		for (int i = 0; i < numSplit; i++)
			matrices.push_back(m_OffsetMatrix * projMatrices[i] * lightMatrix * m_CurrentCamera->GetWorldMatrix());

		return std::make_pair(depth_buffer, matrices);
	}

	std::pair<std::shared_ptr<Texture>, Matrix4> Pipeline::DrawDirLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		// get pointers
		auto depth_shader = GetShaderByName("leagcy_depth_shader");
		auto depth_buffer = Texture::Pool.Get(1024, 1024, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D);
		depth_buffer->SetBorderColor(Color::White);
		depth_buffer->SetWrapMode(WrapMode::CLAMP_TO_BORDER);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		auto camNode = pass->GetCameraNode();
		auto camera = camNode->GetComponent<Camera>();

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

			depth_shader->Bind();
			depth_shader->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &lightMatrix.Raw[0]);
			depth_shader->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrix.Raw[0]);

			for (auto &caster : casters)
			{
				auto casterRender = caster->GetComponent<MeshRender>();
				auto casterMesh = casterRender->GetMesh();

				depth_shader->BindMesh(casterMesh);
				depth_shader->BindMatrix(Matrix4::WORLD_MATRIX, &caster->GetWorldMatrix().Raw[0]);

				unsigned int subMeshCount = casterMesh->GetSubMeshCount();
				if (subMeshCount > 0)
				{
					for (unsigned int i = 0; i < subMeshCount; i++)
					{
						depth_shader->BindSubMesh(casterMesh, i);
						glDrawElements(GL_TRIANGLES, casterMesh->GetSubMeshAt(i)->Indices.Data.size(), GL_UNSIGNED_INT, 0);
						RenderUtil::Instance()->IncreaseDrawCall();
					}
				}
				else
				{
					depth_shader->BindMesh(casterMesh);
					glDrawElements(GL_TRIANGLES, casterMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
					RenderUtil::Instance()->IncreaseDrawCall();
				}

				RenderUtil::Instance()->IncreaseTriangleCount(casterMesh->Indices.Data.size());
			}

			glDisable(GL_POLYGON_OFFSET_FILL);
			depth_shader->UnBind();

			m_SharedPass->UnBind();
		}

		return std::make_pair(depth_buffer, m_OffsetMatrix * projMatrix * lightMatrix * m_CurrentCamera->GetWorldMatrix());
	}

	std::pair<std::shared_ptr<Texture>, Matrix4> Pipeline::DrawPointLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto depth_shader = GetShaderByName("cube_depth_shader");
		auto depth_buffer = Texture::Pool.Get(512, 512, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_CUBE_MAP);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		auto camNode = pass->GetCameraNode();
		auto camera = camNode->GetComponent<Camera>();

		auto light = node->GetComponent<Light>();
		auto radius = light->GetRadius();
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

					depth_shader->BindMesh(casterMesh);
					depth_shader->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &ivm.Raw[0]);
					depth_shader->BindMatrix(Matrix4::WORLD_MATRIX, &caster->GetWorldMatrix().Raw[0]);

					unsigned int subMeshCount = casterMesh->GetSubMeshCount();
					if (subMeshCount > 0)
					{
						for (unsigned int i = 0; i < subMeshCount; i++)
						{
							depth_shader->BindSubMesh(casterMesh, i);
							glDrawElements(GL_TRIANGLES, casterMesh->GetSubMeshAt(i)->Indices.Data.size(), GL_UNSIGNED_INT, 0);
							RenderUtil::Instance()->IncreaseDrawCall();
						}
					}
					else
					{
						depth_shader->BindMesh(casterMesh);
						glDrawElements(GL_TRIANGLES, casterMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
						RenderUtil::Instance()->IncreaseDrawCall();
					}

					RenderUtil::Instance()->IncreaseTriangleCount(casterMesh->Indices.Data.size());
				}
			}

			//glDisable(GL_POLYGON_OFFSET_FILL);
			depth_shader->UnBind();

			m_SharedPass->UnBind();
		}

		return std::make_pair(depth_buffer, m_CurrentCamera->GetWorldMatrix());
	}

	std::pair<std::shared_ptr<Texture>, Matrix4> Pipeline::DrawSpotLightShadowMap(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		// get pointers
		auto depth_shader = GetShaderByName("leagcy_depth_shader");
		auto depth_buffer = Texture::Pool.Get(1024, 1024, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D);

		// for debug
		Pipeline::Active->GetEntityManager()->Add(depth_buffer);

		depth_buffer->SetBorderColor(Color::White);
		depth_buffer->SetWrapMode(WrapMode::CLAMP_TO_BORDER);

		auto light = node->GetComponent<Light>();
		auto radius = light->GetRadius();

		Matrix4 lightMatrix;
		lightMatrix.Rotate(MathUtil::AxisRadToQuat(Vector4::XAxis, MathUtil::DegToRad * 90.0f));
		lightMatrix = lightMatrix * node->GetInvertWorldMatrix();

		Frustum frustum;
		frustum.Setup(light->GetOutterAngle(), 1.0f, 1.0f, light->GetRadius());
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

			depth_shader->Bind();
			depth_shader->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &lightMatrix.Raw[0]);
			depth_shader->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrix.Raw[0]);

			for (auto &caster : casters)
			{
				auto casterRender = caster->GetComponent<MeshRender>();
				auto casterMesh = casterRender->GetMesh();

				depth_shader->BindMesh(casterMesh);
				depth_shader->BindMatrix(Matrix4::WORLD_MATRIX, &caster->GetWorldMatrix().Raw[0]);

				unsigned int subMeshCount = casterMesh->GetSubMeshCount();
				if (subMeshCount > 0)
				{
					for (unsigned int i = 0; i < subMeshCount; i++)
					{
						depth_shader->BindSubMesh(casterMesh, i);
						glDrawElements(GL_TRIANGLES, casterMesh->GetSubMeshAt(i)->Indices.Data.size(), GL_UNSIGNED_INT, 0);
						RenderUtil::Instance()->IncreaseDrawCall();
					}
				}
				else
				{
					depth_shader->BindMesh(casterMesh);
					glDrawElements(GL_TRIANGLES, casterMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
					RenderUtil::Instance()->IncreaseDrawCall();
				}

				RenderUtil::Instance()->IncreaseTriangleCount(casterMesh->Indices.Data.size());
			}

			glDisable(GL_POLYGON_OFFSET_FILL);
			depth_shader->UnBind();

			m_SharedPass->UnBind();
		}

		return std::make_pair(depth_buffer, m_OffsetMatrix * projMatrix * lightMatrix * m_CurrentCamera->GetWorldMatrix());
	}

	void Pipeline::DrawDebug(std::unordered_map<std::string, std::shared_ptr<RenderQuery>> &queries)
	{
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glDisable(GL_BLEND);

		auto optMeshBounds = GetOption(OPT_MESH_BOUNDS);
		auto optCustomBounds = GetOption(OPT_CUSTOM_BOUNDS);
		auto optLightBounds = GetOption(OPT_LIGHT_BOUNDS);

		auto renderUtil = RenderUtil::Instance();

		for (auto &pair : m_PassMap)
		{
			auto pass = pair.second;
			auto camNode = pass->GetCameraNode();

			if (camNode == nullptr || pass->GetDrawMode() == DrawMode::QUAD)
				continue;

			auto it = queries.find(camNode->GetName());
			if (it == queries.end())
				continue;

			auto visibles = it->second;
			renderUtil->BeginDrawLines(camNode);

			if (optMeshBounds.second.boolValue)
			{
				for (auto node : visibles->renderableNodes)
					renderUtil->DrawBoxBounds(node->GetWorldAABB(), Color::White);
			}

			if (optCustomBounds.second.boolValue)
			{
				for (const auto &bounds : m_DebugFrustum)
					renderUtil->DrawFrustum(bounds, Color::Green);

				for (const auto &bounds : m_DebugBoxBounds)
					renderUtil->DrawBoxBounds(bounds, Color::Green);
			}

			renderUtil->EndDrawLines();

			renderUtil->BeginDrawMeshs(camNode);

			if (optLightBounds.second.boolValue)
			{
				for (auto node : visibles->lightNodes)
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
		}

		glDisable(GL_DEPTH_TEST);
	}
}