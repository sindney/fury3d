#include <SFML/System/Time.hpp>

#include "RenderUtil.h"
#include "GLLoader.h"
#include "Log.h"
#include "Vector4.h"
#include "Shader.h"
#include "SceneNode.h"
#include "Frustum.h"
#include "Mesh.h"
#include "MeshUtil.h"
#include "Texture.h"

namespace fury
{
	RenderUtil::RenderUtil()
	{
		const char *debug_vs =
			"#version 330\n"
			"in vec3 vertex_position;\n"
			"uniform mat4 projection_matrix;\n"
			"uniform mat4 invert_view_matrix;\n"
			"uniform mat4 world_matrix;\n"
			"void main() {\n"
			"    gl_Position = projection_matrix * invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);\n"
			"}\n";

		const char *debug_fs =
			"#version 330\n"
			"uniform vec3 color;\n"
			"out vec4 fragment_output;\n"
			"void main() {\n"
			"    fragment_output = vec4(color, 1.0);\n"
			"}\n";

		const char *blur_vs =
			"in vec3 vertex_position;"
			"out vec2 out_uv;"
			"void main()"
			"{"
			"	out_uv = vertex_position.xy * 0.5 + 0.5;"
			"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);"
			"}";

		const char *blur_fs =
			"uniform vec2 scaleU;"
			"uniform sampler2D textureSrc;"
			"in vec2 out_uv;"
			"out vec4 fragment_output;"
			"void main()"
			"{"
			"	vec4 color = vec4(0.0);"
			"	color += texture2D(textureSrc, out_uv + vec2(-3.0 * scaleU.x, -3.0*scaleU.y)) * 0.015625;"
			"	color += texture2D(textureSrc, out_uv + vec2(-2.0 * scaleU.x, -2.0*scaleU.y)) * 0.09375;"
			"	color += texture2D(textureSrc, out_uv + vec2(-1.0 * scaleU.x, -1.0*scaleU.y)) * 0.234375;"
			"	color += texture2D(textureSrc, out_uv + vec2(0.0, 0.0)) * 0.3125;"
			"	color += texture2D(textureSrc, out_uv + vec2(1.0 * scaleU.x,  1.0*scaleU.y)) * 0.234375;"
			"	color += texture2D(textureSrc, out_uv + vec2(2.0 * scaleU.x,  2.0*scaleU.y)) * 0.09375;"
			"	color += texture2D(textureSrc, out_uv + vec2(3.0 * scaleU.x, -3.0*scaleU.y)) * 0.015625;"
			"	fragment_output = color;"
			"}";

		m_DebugShader = Shader::Create("DebugShader", ShaderType::OTHER);
		if (!m_DebugShader->Compile(debug_vs, debug_fs, ""))
			FURYE << "Failed to compile line shader!";

		m_BlurShader = Shader::Create("BlurShader", ShaderType::OTHER);
		if (!m_BlurShader->Compile(debug_vs, debug_fs, ""))
			FURYE << "Failed to compile blur shader!";

		m_DebugShader->Bind();

		auto shaderId = m_DebugShader->GetProgram();

		glBindAttribLocation(shaderId, 0, "vertex_position");

		glGenVertexArrays(1, &m_LineVAO);
		glGenBuffers(1, &m_LineVBO);

		glBindVertexArray(m_LineVAO);

		glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
		glEnableVertexAttribArray(0);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		m_DebugShader->UnBind();

		m_BlitPass = Pass::Create("BlitPass");
		m_BlitPass->SetBlendMode(BlendMode::REPLACE);
		m_BlitPass->SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);
		m_BlitPass->SetCompareMode(CompareMode::LESS);
		m_BlitPass->SetCullMode(CullMode::NONE);
	}

	RenderUtil::~RenderUtil()
	{
		if (m_LineVAO != 0)
			glDeleteVertexArrays(1, &m_LineVAO);

		if (m_LineVBO != 0)
			glDeleteBuffers(1, &m_LineVBO);
	}

	void RenderUtil::Blit(const std::shared_ptr<Texture> &src, const std::shared_ptr<Texture> &dest, 
		const std::shared_ptr<Shader> &shader, ClearMode clearMode, BlendMode blendMode)
	{
		m_BlitPass->AddTexture(dest, false);
		m_BlitPass->SetClearMode(clearMode);
		m_BlitPass->SetBlendMode(blendMode);

		m_BlitPass->Bind();

		shader->Bind();

		shader->BindTexture(src);
		shader->BindMesh(MeshUtil::GetUnitQuad());

		glDrawElements(GL_TRIANGLES, MeshUtil::GetUnitQuad()->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		m_BlitPass->UnBind();

		m_BlitPass->RemoveAllTextures();

		m_TriangleCount += 2;
		m_DrawCall++;
	}

	void RenderUtil::Blur(const std::shared_ptr<Texture> &src, const std::shared_ptr<Texture> &dest, float coef)
	{
		m_BlurShader->Bind();
		m_BlurShader->BindFloat("scaleU", 1.0f / (src->GetWidth() * coef), 0.0f);
		RenderUtil::Instance()->Blit(src, dest, m_BlurShader);

		m_BlurShader->Bind();
		m_BlurShader->BindFloat("scaleU", 0.0f, 1.0f / (src->GetHeight() * coef));
		RenderUtil::Instance()->Blit(dest, src, m_BlurShader);
	}

	void RenderUtil::BeginDrawLines(const std::shared_ptr<SceneNode> &camera)
	{
		if (m_DrawingLine || m_LineVAO == 0 || m_LineVBO == 0 || m_DebugShader->GetDirty())
			return;

		m_DrawingLine = true;

		m_DebugShader->Bind();
		m_DebugShader->BindCamera(camera);
		m_DebugShader->BindMatrix(Matrix4::WORLD_MATRIX, Matrix4());

		glBindVertexArray(m_LineVAO);
		glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
	}

	void RenderUtil::DrawLines(const float* positions, unsigned int size, Color color, LineMode lineMode)
	{
		if (!m_DrawingLine)
		{
			FURYE << "Call BeginDrawLines(camera) before DrawLines(xxx)!";
			return;
		}

		auto dataSize = sizeof(float) * size;
		glBufferData(GL_ARRAY_BUFFER, dataSize, 0, GL_STREAM_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, positions);

		m_DebugShader->BindFloat("color", color.r, color.g, color.b);
		
		glDrawArrays(EnumUtil::LineModeToUnit(lineMode), 0, size / 3);

		m_DrawCall++;
	}

	void RenderUtil::DrawBoxBounds(const BoxBounds &aabb, Color color)
	{
		Vector4 min = aabb.GetMin();
		Vector4 max = aabb.GetMax();

		Vector4 cornors[] = {
			min,
			Vector4(max.x, min.y, min.z, 1.0f),
			Vector4(min.x, max.y, min.z, 1.0f),
			Vector4(max.x, max.y, min.z, 1.0f),
			Vector4(min.x, min.y, max.z, 1.0f),
			Vector4(max.x, min.y, max.z, 1.0f),
			Vector4(min.x, max.y, max.z, 1.0f),
			max
		};

		unsigned int indices[] = { 0, 1, 2, 3, 6, 7, 4, 5, 6, 2, 7, 3, 5, 1, 4, 0, 6, 4, 7, 5, 3, 1, 2, 0 };

		std::vector<float> lines;
		for (unsigned int i : indices)
		{
			Vector4 cornor = cornors[i];
			lines.push_back(cornor.x);
			lines.push_back(cornor.y);
			lines.push_back(cornor.z);
		}

		DrawLines(&lines[0], lines.size(), color);
	}

	void RenderUtil::DrawFrustum(const Frustum &frustum, Color color)
	{
		auto corners = frustum.GetCurrentCorners();

		unsigned int indices[] = { 0, 4, 1, 5, 3, 7, 2, 6, 0, 2, 2, 3, 3, 1, 1, 0, 4, 6, 6, 7, 7, 5, 5, 4 };

		std::vector<float> lines;
		for (unsigned int i : indices)
		{
			Vector4 cornor = corners[i];
			lines.push_back(cornor.x);
			lines.push_back(cornor.y);
			lines.push_back(cornor.z);
		}

		DrawLines(&lines[0], lines.size(), color);
	}

	void RenderUtil::EndDrawLines()
	{
		m_DrawingLine = false;

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		m_DebugShader->UnBind();
	}

	void RenderUtil::BeginDrawMeshs(const std::shared_ptr<SceneNode> &camera)
	{
		if (m_DebugShader->GetDirty() || m_DrawingMesh)
			return;

		m_DrawingMesh = true;

		m_DebugShader->Bind();
		m_DebugShader->BindCamera(camera);

		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}

	void RenderUtil::DrawMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 &worldMatrix, Color color)
	{
		if (!m_DrawingMesh || mesh->IsSkinnedMesh() || mesh->GetSubMeshCount() > 0)
			return;

		m_DebugShader->BindFloat("color", color.r, color.g, color.b);
		m_DebugShader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);
		m_DebugShader->BindMesh(mesh);

		glDrawElements(GL_TRIANGLES, mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);

		m_DrawCall++;
	}

	void RenderUtil::EndDrawMeshes()
	{
		m_DrawingMesh = false;
		m_DebugShader->UnBind();

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	void RenderUtil::BeginFrame()
	{
		m_DrawCall = 0;
		m_MeshCount = 0;
		m_TriangleCount = 0;
		m_SkinnedMeshCount = 0;
		m_LightCount = 0;

		m_FrameClock.restart();

		OnBeginFrame.Emit();
	}

	void RenderUtil::EndFrame()
	{
		auto frameTime = m_FrameClock.restart().asMilliseconds();
		OnEndFrame.Emit(std::move(frameTime));
	}

	void RenderUtil::IncreaseDrawCall(unsigned int count)
	{
		m_DrawCall += count;
	}

	unsigned int RenderUtil::GetDrawCall()
	{
		return m_DrawCall;
	}

	void RenderUtil::IncreaseMeshCount(unsigned int count)
	{
		m_MeshCount += count;
	}

	unsigned int RenderUtil::GetMeshCount()
	{
		return m_MeshCount;
	}

	void RenderUtil::IncreaseTriangleCount(unsigned int count)
	{
		m_TriangleCount += count;
	}

	unsigned int RenderUtil::GetTriangleCount()
	{
		return m_TriangleCount;
	}

	void RenderUtil::IncreaseSkinnedMeshCount(unsigned int count)
	{
		m_SkinnedMeshCount += count;
	}

	unsigned int RenderUtil::GetSkinnedMeshCount()
	{
		return m_SkinnedMeshCount;
	}

	void RenderUtil::IncreaseLightCount(unsigned int count)
	{
		m_LightCount += count;
	}

	unsigned int RenderUtil::GetLightCount()
	{
		return m_LightCount;
	}
}