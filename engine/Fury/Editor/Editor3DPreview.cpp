#include "Fury/Editor/Editor3DPreview.h"

#ifdef WITH_EDITOR

#include <cmath>
#include <unordered_map>
#include <vector>

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Material.h"
#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			std::unordered_map<std::string, OrbitState> g_OrbitState;
			std::unordered_map<std::string, PreviewRT> g_PreviewRTs;

			// Cached grid/AABB shader + VBO/VAOs. The mesh editor
			// already had these as static locals; lift them out so
			// multiple editor previews share the same GL objects.
			std::shared_ptr<Shader> g_LineShader;
			GLuint g_GridVBO = 0;
			GLuint g_GridVAO = 0;
			float g_GridCachedExtent = -1.0f;
			GLuint g_AabbVBO = 0;
			GLuint g_AabbVAO = 0;

			std::shared_ptr<Shader> GetLineShader()
			{
				if (g_LineShader) return g_LineShader;
				g_LineShader = Shader::Create("EditorLineShader", ShaderType::OTHER);
				const char *vs =
					"in vec3 vertex_position;"
					"uniform mat4 _ViewMatrix;"
					"uniform mat4 _ProjectionMatrix;"
					"uniform mat4 _OffsetMat;"
					"void main()"
					"{"
					"	gl_Position = _ProjectionMatrix * _ViewMatrix *"
					"		_OffsetMat * vec4(vertex_position, 1.0);"
					"}";
				const char *fs =
					"uniform vec4 _Color;"
					"out vec4 fragment_output;"
					"void main() { fragment_output = _Color; }";
				if (!g_LineShader->Compile(vs, fs, ""))
					FURYE << "Editor3DPreview: failed to compile line shader";
				return g_LineShader;
			}
		}

		OrbitState &OrbitFor(const std::string &popup_id)
		{
			auto it = g_OrbitState.find(popup_id);
			if (it == g_OrbitState.end())
				it = g_OrbitState.emplace(popup_id, OrbitState{}).first;
			return it->second;
		}

		PreviewRT &EnsureRT(const std::string &popup_id, int w, int h, bool *out_resized)
		{
			PreviewRT &rt = g_PreviewRTs[popup_id];
			if (rt.fbo == 0 || rt.width != w || rt.height != h)
			{
				if (out_resized) *out_resized = true;
				if (rt.fbo)
				{
					glDeleteFramebuffers(1, &rt.fbo);
					rt.fbo = 0;
				}
				glGenFramebuffers(1, &rt.fbo);
				rt.colorRT = Texture::GetTemporary(w, h, 1,
					TextureFormat::RGBA8, TextureType::TEXTURE_2D);
				rt.depthRT = Texture::GetTemporary(w, h, 1,
					TextureFormat::DEPTH24, TextureType::TEXTURE_2D);
				glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, rt.colorRT->GetID(), 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					GL_TEXTURE_2D, rt.depthRT->GetID(), 0);
				const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
				if (status != GL_FRAMEBUFFER_COMPLETE)
				{
					FURYW << "Editor3DPreview: FBO incomplete for popup_id "
						  << popup_id << " (0x" << std::hex << status << std::dec << ")";
					glBindFramebuffer(GL_FRAMEBUFFER, 0);
					rt.fbo = 0;
					return rt;
				}
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				rt.width = w;
				rt.height = h;
			}
			return rt;
		}

		void ReframeOrbit(OrbitState &orbit, const Vector4 &aabb_center,
			float aabb_radius, float aspect, bool mesh_changed)
		{
			const float fov0 = 45.0f * 0.0174532925f;
			const float adjusted_dist = (aabb_radius * 0.6f) /
				(std::tan(fov0 * 0.5f) * std::min(1.0f, aspect));
			const float zoom_factor = (orbit.initialized && !mesh_changed)
				? (orbit.distance / std::max(1e-6f, orbit.initialDistance))
				: 1.0f;
			orbit.initialDistance = adjusted_dist;
			orbit.distance = adjusted_dist * zoom_factor;
			orbit.target = aabb_center;
			orbit.initialized = true;
		}

		ViewProj ComputeViewProj(const OrbitState &orbit,
			const Vector4 &aabb_center, float aabb_radius, float aspect)
		{
			ViewProj out;
			const float fov = 45.0f * 0.0174532925f;
			out.proj.PerspectiveFov(fov, aspect,
				std::max(aabb_radius * 0.05f, 1e-5f),
				orbit.distance + aabb_radius * 5.0f);

			const float cx = std::cos(orbit.pitch) * std::cos(orbit.yaw);
			const float cy = std::sin(orbit.pitch);
			const float cz = std::cos(orbit.pitch) * std::sin(orbit.yaw);
			out.eye = Vector4(orbit.target.x + cx * orbit.distance,
				orbit.target.y + cy * orbit.distance,
				orbit.target.z + cz * orbit.distance, 1.0f);
			out.view.LookAt(out.eye, orbit.target, Vector4(0, 1, 0, 0));
			return out;
		}

		void DrawGroundGrid(const Matrix4 &view, const Matrix4 &proj,
			const Vector4 &aabb_center, float aabb_min_y, float aabb_radius)
		{
			auto shader = GetLineShader();
			if (!shader) return;

			shader->Bind();
			shader->BindMatrix("_ViewMatrix", view);
			shader->BindMatrix("_ProjectionMatrix", proj);

			// Grid extent = 2*radius, step = extent/5 (matches the
			// mesh editor's formula so a side-by-side Mesh + Particle
			// editor share a visual scale).
			const float grid_extent = aabb_radius * 2.0f;
			const float grid_step = grid_extent / 5.0f;
			const int grid_lines_per_axis =
				static_cast<int>((2.0f * grid_extent) / grid_step) + 1;
			const int grid_vertex_count = grid_lines_per_axis * 4;

			if (g_GridVBO == 0 || g_GridCachedExtent != grid_extent)
			{
				std::vector<float> grid_vbo_data;
				grid_vbo_data.reserve(grid_vertex_count * 3);
				for (float x = -grid_extent; x <= grid_extent + 1e-4f; x += grid_step)
				{
					grid_vbo_data.push_back(x); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back(-grid_extent);
					grid_vbo_data.push_back(x); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back( grid_extent);
				}
				for (float z = -grid_extent; z <= grid_extent + 1e-4f; z += grid_step)
				{
					grid_vbo_data.push_back(-grid_extent); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back(z);
					grid_vbo_data.push_back( grid_extent); grid_vbo_data.push_back(0.0f); grid_vbo_data.push_back(z);
				}
				if (g_GridVBO == 0)
				{
					glGenBuffers(1, &g_GridVBO);
					glGenVertexArrays(1, &g_GridVAO);
					glBindVertexArray(g_GridVAO);
					glBindBuffer(GL_ARRAY_BUFFER, g_GridVBO);
					glEnableVertexAttribArray(0);
					glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
					glBindVertexArray(0);
				}
				glBindBuffer(GL_ARRAY_BUFFER, g_GridVBO);
				glBufferData(GL_ARRAY_BUFFER,
					grid_vbo_data.size() * sizeof(float),
					grid_vbo_data.data(), GL_DYNAMIC_DRAW);
				g_GridCachedExtent = grid_extent;
			}

			Matrix4 gridOffMat;
			gridOffMat.Identity();
			gridOffMat.Raw[12] = aabb_center.x;
			gridOffMat.Raw[13] = aabb_min_y;
			gridOffMat.Raw[14] = aabb_center.z;
			shader->BindMatrix("_OffsetMat", gridOffMat);
			glBindVertexArray(g_GridVAO);
			glLineWidth(1.0f);
			glDrawArrays(GL_LINES, 0, grid_vertex_count);
			glBindVertexArray(0);
			shader->UnBind();
		}

		void ApplyCameraInput(OrbitState &orbit, const Vector4 &eye,
			const ImGuiIO &io, float radius)
		{
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				orbit.yaw   += io.MouseDelta.x * 0.01f;
				orbit.pitch += io.MouseDelta.y * 0.01f;
				const float lim = static_cast<float>(M_PI_2) - 0.01f;
				if (orbit.pitch >  lim) orbit.pitch =  lim;
				if (orbit.pitch < -lim) orbit.pitch = -lim;
			}
			if (io.MouseWheel != 0.0f && orbit.initialDistance > 0.0f)
			{
				orbit.distance *= (1.0f - io.MouseWheel * 0.1f);
				orbit.distance = std::max(0.1f * orbit.initialDistance,
					std::min(10.0f * orbit.initialDistance, orbit.distance));
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
			{
				Vector4 forward = eye - orbit.target;
				float flen = std::sqrt(
					forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
				if (flen > 1e-6f)
				{
					forward.x /= flen; forward.y /= flen; forward.z /= flen;
					Vector4 right(
						forward.y * 0.0f - forward.z * 1.0f,
						forward.z * 0.0f - forward.x * 0.0f,
						forward.x * 1.0f - forward.y * 0.0f, 0.0f);
					float rlen = std::sqrt(
						right.x * right.x + right.y * right.y + right.z * right.z);
					if (rlen > 1e-6f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }
					Vector4 up(
						right.y * forward.z - right.z * forward.y,
						right.z * forward.x - right.x * forward.z,
						right.x * forward.y - right.y * forward.x, 0.0f);
					const float pan_scale = radius * 0.002f;
					(void)pan_scale; (void)up; // pan kept for parity; orbit target stays put for v1.
				}
			}
		}
	}
}

#endif // WITH_EDITOR