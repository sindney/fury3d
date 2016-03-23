#include <unordered_map>

#include "Angle.h"
#include "Camera.h"
#include "Debug.h"
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
	}

	void PrelightPipeline::Execute(const std::shared_ptr<SceneManager> &sceneManager)
	{
		// pre
		m_DrawCall = 0;
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
				LOGW << "Camera for pass " + pass->GetName() + " not found!";
				continue;
			}

			auto it = queries.find(camNode->GetName());
			if (it != queries.end())
				continue;

			RenderQuery::Ptr query = RenderQuery::Create();

			sceneManager->GetRenderQuery(camNode->GetComponent<Camera>()->GetFrustum(), query);
			query->Sort(camNode->GetWorldPosition(), false);

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

			pass->Bind();

			if (drawMode == DrawMode::RENDERABLE)
			{
				for (const auto &node : query->OpaqueNodes)
					DrawRenderable(pass, node);
			}
			else if (drawMode == DrawMode::LIGHT)
			{
				glDepthMask(GL_FALSE);

				for (const auto &node : query->LightNodes)
					DrawLight(pass, node);

				glDepthMask(GL_TRUE);
				glEnable(GL_DEPTH_TEST);
				glCullFace(GL_BACK);
			}
			else 
			{
				DrawQuad(pass);
			}

			pass->UnBind();
		}

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

	void PrelightPipeline::DrawRenderable(const std::shared_ptr<Pass> &pass, const std::shared_ptr<SceneNode> &node)
	{
		auto render = node->GetComponent<MeshRender>();
		auto mesh = render->GetMesh();

		unsigned int subMeshCount = mesh->GetSubMeshCount();
		if (subMeshCount > 0)
		{
			// with subMesh
			for (unsigned int i = 0; i < subMeshCount; i++)
			{
				if (auto subMesh = mesh->GetSubMeshAt(i))
				{
					auto material = render->GetMaterial(i);
					auto shader = material->GetShaderForPass(pass->GetRenderIndex());

					if (shader == nullptr)
						shader = pass->GetShader(mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH, 
						material->GetTextureFlags());

					if (shader == nullptr)
					{
						LOGW << "Failed to draw " << node->GetName() << ", shader not found!";
						return;
					}

					shader->Bind();
					shader->BindCamera(m_CurrentCamera);
					shader->BindMatrix(Matrix4::WORLD_MATRIX, node->GetWorldMatrix());

					shader->BindSubMesh(mesh, i);
					shader->BindMaterial(material);

					for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
					{
						auto ptr = pass->GetTextureAt(i, true);
						shader->BindTexture(ptr->GetName(), ptr);
					}

					glDrawElements(GL_TRIANGLES, subMesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

					shader->UnBind();

					m_DrawCall++;
				}
			}
		}
		else
		{
			// no subMesh
			auto material = render->GetMaterial();
			auto shader = material->GetShaderForPass(pass->GetRenderIndex());

			if (shader == nullptr)
				shader = pass->GetShader(mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH, 
				material->GetTextureFlags());

			if (shader == nullptr)
			{
				LOGW << "Failed to draw " << node->GetName() << ", shader not found!";
				return;
			}

			shader->Bind();
			shader->BindCamera(m_CurrentCamera);
			shader->BindMatrix(Matrix4::WORLD_MATRIX, node->GetWorldMatrix());

			shader->BindMesh(mesh);
			shader->BindMaterial(material);

			for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
			{
				auto ptr = pass->GetTextureAt(i, true);
				shader->BindTexture(ptr->GetName(), ptr);
			}

			glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

			shader->UnBind();

			m_DrawCall++;
		}
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
			float camNear = (camPtr->GetFrustum().GetWorldSpaceCorners()[0] - camPos).Length();
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

			float camNear = (camPtr->GetFrustum().GetWorldSpaceCorners()[0] - camPos).Length();
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
			LOGW << "Shader for light " << node->GetName() << " not found!";
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

		m_DrawCall++;
	}

	void PrelightPipeline::DrawQuad(const std::shared_ptr<Pass> &pass)
	{
		auto shader = m_CurrentShader;
		auto mesh = MeshUtil::Instance()->GetUnitQuad();

		if (shader == nullptr)
		{
			LOGW << "Failed to draw full screen quad, shader not found!";
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

		m_DrawCall++;
	}
}