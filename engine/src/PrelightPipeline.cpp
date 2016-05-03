#include <cmath>
#include <unordered_map>

#include "Angle.h"
#include "Camera.h"
#include "Log.h"
#include "EnumUtil.h"
#include "EntityUtil.h"
#include "Frustum.h"
#include "GLLoader.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRender.h"
#include "MeshUtil.h"
#include "Pass.h"
#include "PrelightPipeline.h"
#include "RenderQuery.h"
#include "RenderUtil.h"
#include "SceneManager.h"
#include "SceneNode.h"
#include "Shader.h"
#include "SphereBounds.h"
#include "Texture.h"

namespace fury
{
	PrelightPipeline::Ptr PrelightPipeline::Create(const std::string &name)
	{
		return std::make_shared<PrelightPipeline>(name);
	}

	PrelightPipeline::PrelightPipeline(const std::string &name) : Pipeline(name)
	{
		m_TypeIndex = typeid(PrelightPipeline);

		m_DrawDepthPass = Pass::Create("DrawDepthPass");
		m_DrawDepthPass->SetBlendMode(BlendMode::REPLACE);
		m_DrawDepthPass->SetClearMode(ClearMode::DEPTH);
		m_DrawDepthPass->SetCompareMode(CompareMode::LESS);
		m_DrawDepthPass->SetCullMode(CullMode::BACK);

		m_DrawShadowPass = Pass::Create("DrawShadowPass");
		m_DrawShadowPass->SetBlendMode(BlendMode::MULTIPLY);
		m_DrawShadowPass->SetClearMode(ClearMode::NONE);
		m_DrawShadowPass->SetCompareMode(CompareMode::ALWAYS);
		m_DrawShadowPass->SetCullMode(CullMode::BACK);
	}

	void PrelightPipeline::Execute(const std::shared_ptr<SceneManager> &sceneManager)
	{
		// pre
		m_CurrentCamera = nullptr;
		m_CurrentShader = nullptr;
		SortPassByIndex();

		// find visible nodes, 1 cam 1 query
		std::unordered_map<std::string, RenderQuery::Ptr> queries;

		for (auto pair : m_PassMap)
		{
			auto pass = pair.second;
			auto camNode = pass->GetCameraNode();

			if (camNode == nullptr)
			{
				FURYW << "Camera for pass " + pass->GetName() + " not found!";
				continue;
			}

			auto it = queries.find(camNode->GetName());
			if (it != queries.end())
				continue;

			RenderQuery::Ptr query = RenderQuery::Create();

			sceneManager->GetRenderQuery(camNode->GetComponent<Camera>()->GetFrustum(), query);
			query->Sort(camNode->GetWorldPosition());

			queries.emplace(camNode->GetName(), query);
		}

		// draw passes

		for (unsigned int i = 0; i < m_SortedPasses.size(); i++)
		{
			auto passName = m_SortedPasses[i];
			auto pass = m_PassMap[passName];

			auto drawMode = pass->GetDrawMode();

			m_CurrentCamera = pass->GetCameraNode();
			m_CurrentShader = pass->GetFirstShader();

			if (m_CurrentCamera == nullptr)
				continue;

			auto query = queries[m_CurrentCamera->GetName()];

			if (drawMode == DrawMode::OPAQUE)
			{
				pass->Bind();
				for (const auto &unit : query->opaqueUnits)
					DrawUnit(pass, unit);
				pass->UnBind();
			}
			else if (drawMode == DrawMode::TRANSPARENT)
			{
				pass->Bind();
				for (const auto &unit : query->transparentUnits)
					DrawUnit(pass, unit);
				pass->UnBind();
			}
			else if (drawMode == DrawMode::QUAD)
			{
				pass->Bind();
				DrawQuad(pass);
				pass->UnBind();
			}
			else if (drawMode == DrawMode::LIGHT)
			{
				bool first = true;
				for (const auto &node : query->lightNodes)
				{
					pass->Bind(first);
					first = false;

					glDepthMask(GL_FALSE);

					DrawLight(pass, node);

					pass->UnBind();

					glDepthMask(GL_TRUE);

					DrawShadow(sceneManager, pass, node);
				}
			}
		}

		if (m_DrawLightBounds || m_DrawOpaqueBounds)
			DrawDebug(queries);

		// post
		m_CurrentCamera = nullptr;
		m_CurrentShader = nullptr;
	}

	bool PrelightPipeline::PointInCone(Vector4 coneCenter, Vector4 coneDir, float height, float theta, Vector4 point)
	{
		float cosTheta = std::cos(theta);
		float cosTheta2 = cosTheta * cosTheta;

		Vector4 dir = point - coneCenter;
		float dot = coneDir * dir;

		if (dot >= 0 && dot * dot >= cosTheta2 * (dir * dir) &&
			(dir.Project(coneDir).SquareLength() <= height * height))
			return true;

		return false;
	}

	void PrelightPipeline::DrawUnit(const std::shared_ptr<Pass> &pass, const RenderUnit &unit)
	{
		auto node = unit.node;
		auto mesh = unit.mesh;
		auto material = unit.material;

		auto shader = material->GetShaderForPass(pass->GetRenderIndex());

		if (shader == nullptr)
			shader = pass->GetShader(mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH,
			material->GetTextureFlags());

		if (shader == nullptr)
		{
			FURYW << "Failed to draw " << node->GetName() << ", shader not found!";
			return;
		}

		shader->Bind();
		shader->BindCamera(m_CurrentCamera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, node->GetWorldMatrix());

		shader->BindMaterial(material);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		if (mesh->GetSubMeshCount() > 0)
		{
			auto subMesh = mesh->GetSubMeshAt(unit.subMesh);
			shader->BindSubMesh(mesh, unit.subMesh);
			glDrawElements(GL_TRIANGLES, subMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(subMesh->Indices.Data.size());
		}
		else
		{
			shader->BindMesh(mesh);
			glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(mesh->Indices.Data.size());
		}

		shader->UnBind();

		// TODO: Maybe subMeshCount ? 
		if (mesh->IsSkinnedMesh())
			RenderUtil::Instance()->IncreaseSkinnedMeshCount();
		else
			RenderUtil::Instance()->IncreaseMeshCount();

		RenderUtil::Instance()->IncreaseDrawCall();
	}

	void PrelightPipeline::DrawLight(const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto light = node->GetComponent<Light>();
		auto camPtr = m_CurrentCamera->GetComponent<Camera>();
		auto camPos = m_CurrentCamera->GetWorldPosition();
		auto mesh = light->GetMesh();
		auto worldMatrix = node->GetWorldMatrix();

		Shader::Ptr shader = nullptr;

		if (light->GetType() == LightType::POINT)
		{
			float camNear = (camPtr->GetFrustum().GetCurrentCorners()[0] - camPos).Length();
			if (SphereBounds(node->GetWorldPosition(), light->GetRadius() + camNear).IsInsideFast(camPos))
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

			shader = pass->GetShader(ShaderType::POINT_LIGHT);
		}
		else if (light->GetType() == LightType::SPOT)
		{
			auto coneCenter = node->GetWorldPosition();
			auto coneDir = worldMatrix.Multiply(Vector4(0, -1, 0, 0)).Normalized();

			float camNear = (camPtr->GetFrustum().GetCurrentCorners()[0] - camPos).Length();
			float theta = light->GetOutterAngle() * 0.5f;
			float height = light->GetRadius();
			float extra = camNear / std::sin(theta);

			coneCenter = coneCenter - coneDir * extra;
			height += camNear + extra;

			if (PointInCone(coneCenter, coneDir, height, theta, camPos))
			{
				glDisable(GL_DEPTH_TEST);
				glCullFace(GL_FRONT);
			}
			else
			{
				glEnable(GL_DEPTH_TEST);
				glCullFace(GL_BACK);
			}

			shader = pass->GetShader(ShaderType::SPOT_LIGHT);
		}
		else
		{
			glEnable(GL_DEPTH_TEST);
			glCullFace(GL_BACK);

			shader = pass->GetShader(ShaderType::DIR_LIGHT);
		}

		if (shader == nullptr)
		{
			FURYW << "Shader for light " << node->GetName() << " not found!";
			return;
		}

		shader->Bind();

		shader->BindCamera(m_CurrentCamera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

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

	void PrelightPipeline::DrawShadow(const std::shared_ptr<SceneManager> &sceneManager, const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto light = node->GetComponent<Light>();
		if (light->GetType() != LightType::DIRECTIONAL)
			return;

		auto camNode = pass->GetCameraNode();
		auto camera = camNode->GetComponent<Camera>();

		Matrix4 lightMatrix = node->GetInvertWorldMatrix();
		lightMatrix.AppendRotation(Angle::AxisRadToQuat(Vector4::XAxis, Angle::DegToRad * 90.0f));

		// get camera's frustum's worldspace aabb.
		auto camAABB = camera->GetFrustum(camera->GetNear(), camera->GetShadowFar()).GetBoxBounds();

		// find shadow casters
		fury::SceneManager::SceneNodes casters;
		sceneManager->GetVisibleShadowCasters(camAABB, casters);

		// transform world space aabb to light space.
		camAABB = node->GetInvertWorldMatrix().Multiply(camAABB);
		// gen projection matrix for light.
		Matrix4 projMatrix;
		projMatrix.OrthoOffCenter(camAABB.GetMin().x, camAABB.GetMax().x,
			camAABB.GetMin().y, camAABB.GetMax().y,
			camAABB.GetMin().z, camAABB.GetMax().z);

		// draw casters to depth map, aka shadow map.
		{
			auto shaderIt = m_ShaderMap.find("draw_depth_shader");
			auto shadowBufferIt = m_TextureMap.find("shadow_buffer_0");

			if (shaderIt == m_ShaderMap.end() || shadowBufferIt == m_TextureMap.end())
			{
				FURYE << "draw_depth_shader/shadow_buffer_0 not found! Check you json pipeline!";
				return;
			}

			auto draw_depth_shader = shaderIt->second;
			auto shadow_buffer_0 = shadowBufferIt->second;

			if(!m_DrawDepthPass->GetTexture("shadow_buffer_0", false))
				m_DrawDepthPass->AddTexture(shadow_buffer_0, false);

			m_DrawDepthPass->Bind();

			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(2.5f, 10.0f);

			draw_depth_shader->Bind();
			draw_depth_shader->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, &lightMatrix.Raw[0]);
			draw_depth_shader->BindMatrix(Matrix4::PROJECTION_MATRIX, &projMatrix.Raw[0]);

			for (auto &caster : casters)
			{
				auto casterRender = caster->GetComponent<MeshRender>();
				auto casterMesh = casterRender->GetMesh();

				draw_depth_shader->BindMesh(casterMesh);
				draw_depth_shader->BindMatrix(Matrix4::WORLD_MATRIX, &caster->GetWorldMatrix().Raw[0]);

				unsigned int subMeshCount = casterMesh->GetSubMeshCount();
				if (subMeshCount > 0)
				{
					for (unsigned int i = 0; i < subMeshCount; i++)
					{
						draw_depth_shader->BindSubMesh(casterMesh, i);
						glDrawElements(GL_TRIANGLES, casterMesh->GetSubMeshAt(i)->Indices.Data.size(), GL_UNSIGNED_INT, 0);
					}
				}
				else
				{
					draw_depth_shader->BindMesh(casterMesh);
					glDrawElements(GL_TRIANGLES, casterMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
				}
			}

			glDisable(GL_POLYGON_OFFSET_FILL);
			draw_depth_shader->UnBind();

			m_DrawDepthPass->UnBind();
		}

		// bind our shadow map.
		// use gbuffer to test pixels's visiblities.
		// set invisible pixels to 0 in light_buffer, to disable lighting effect.
		{
			auto shaderIt = m_ShaderMap.find("draw_shadow_shader");
			auto depthIt = m_TextureMap.find("gbuffer_depth");
			auto lightIt = m_TextureMap.find("gbuffer_light");

			if (shaderIt == m_ShaderMap.end() || depthIt == m_TextureMap.end() || lightIt == m_TextureMap.end())
			{
				FURYE << "draw_shadow_shader/gbuffer_depth/gbuffer_light not found, check your json pipline!";
				return;
			}
			
			auto draw_shadow_shader = shaderIt->second;
			auto gubffer_depth = depthIt->second;
			auto gbuffer_light = lightIt->second;
			auto shadow_buffer_0 = m_TextureMap.find("shadow_buffer_0")->second;

			if (!m_DrawShadowPass->GetTexture(gbuffer_light->GetName(), false))
				m_DrawShadowPass->AddTexture(gbuffer_light, false);

			m_DrawShadowPass->Bind();

			draw_shadow_shader->Bind();

			Matrix4 biasMatrix({
				0.5, 0.0, 0.0, 0.0,
				0.0, 0.5, 0.0, 0.0,
				0.0, 0.0, 0.5, 0.0,
				0.5, 0.5, 0.5, 1.0
			});
			lightMatrix = biasMatrix * projMatrix * lightMatrix * m_CurrentCamera->GetWorldMatrix();

			draw_shadow_shader->BindCamera(m_CurrentCamera);
			draw_shadow_shader->BindMatrix("light_matrix", &lightMatrix.Raw[0]);

			draw_shadow_shader->BindTexture("gbuffer_depth", gubffer_depth);
			draw_shadow_shader->BindTexture("shadow_buffer", shadow_buffer_0);

			draw_shadow_shader->BindMesh(MeshUtil::GetUnitQuad());

			glDrawElements(GL_TRIANGLES, MeshUtil::GetUnitQuad()->Indices.Data.size(), GL_UNSIGNED_INT, 0);

			draw_shadow_shader->UnBind();

			m_DrawShadowPass->UnBind();
		}
	}

	void PrelightPipeline::DrawDebug(std::unordered_map<std::string, RenderQuery::Ptr> &queries)
	{
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glDisable(GL_BLEND);

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

			if (m_DrawOpaqueBounds)
			{
				for (auto node : visibles->renderableNodes)
					renderUtil->DrawBoxBounds(node->GetWorldAABB(), Color::White);
			}

			renderUtil->EndDrawLines();

			renderUtil->BeginDrawMeshs(camNode);

			if (m_DrawLightBounds)
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