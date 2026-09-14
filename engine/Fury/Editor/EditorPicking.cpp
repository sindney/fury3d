#if WITH_EDITOR

#include "Fury/Editor/EditorPicking.hpp"
#include "Fury/Editor/Editor.h"

#include "Fury/Camera.h"
#include "Fury/EnumUtil.h"
#include "Fury/FramePacket.h"
#include "Fury/Joint.h"
#include "Fury/GLLoader.h"
#include "Fury/InputUtil.h"
#include "Fury/Log.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderQuery.h"
#include "Fury/RenderThread.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"

#include <atomic>
#include <memory>
#include <vector>

namespace fury
{
	namespace Editor
	{
		// Defined in Editor.cpp; we write through it when a pick resolves.
		// SetSelectedSceneNode (declared in Editor.h) emits the
		// OnSelectionChanged signal -- picking uses it so subscribers hear
		// about viewport-driven selection changes.
		extern SceneNode* g_SelectedSceneNode;

		// Defined in Editor.cpp; captured each frame by RenderViewportWindow.
		// The picking FBO is sized to the viewport content rect and pick
		// coordinates are content-rect-relative, so the sampled pixel lines
		// up with what the user sees inside the Viewport window.
		extern ImVec2 g_ViewportContentSize;
		extern bool g_ViewportVisible;

		namespace Picking
		{
			namespace
			{
				enum class State
				{
					Idle,
					RenderRequested,
					AwaitingReadback
				};

				State        g_State           = State::Idle;
				ImVec2       g_PendingClickPx  = ImVec2(0, 0);
				int          g_CapturedW       = 0;
				int          g_CapturedH       = 0;

				// Render-side pick results, posted by the overlay job and
				// consumed by TickPostRender on the game thread.
				std::atomic<unsigned int>   g_PickResultId{ 0 };
				std::atomic<std::uint64_t>  g_PickResultSeq{ 0 };
				std::uint64_t               g_ConsumedSeq = 0;

				GLuint                 g_PickFBO   = 0;
				std::shared_ptr<Texture> g_PickColor; // R32UI
				std::shared_ptr<Texture> g_PickDepth; // DEPTH24
				int                    g_PickW = 0;
				int                    g_PickH = 0;

				std::vector<std::weak_ptr<SceneNode>> g_IdTable;

				std::shared_ptr<Shader> g_IdShaderStatic;
				std::shared_ptr<Shader> g_IdShaderSkinned;

				// id_pass: static-mesh vertex shader. Mirrors the engine's
				// existing GBuffer attribute names so Shader::BindMesh wires
				// vertex_position automatically.
				const char* kIdVS =
					"#version 330 core\n"
					"in vec3 vertex_position;\n"
					"uniform mat4 invert_view_matrix;\n"
					"uniform mat4 projection_matrix;\n"
					"uniform mat4 world_matrix;\n"
					"void main() {\n"
					"    gl_Position = projection_matrix * invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);\n"
					"}\n";

				// id_pass_skinned: same outputs as the static path but with
				// the bone-weighted skinning the engine's standard skinned
				// shader uses (bone_ids ivec4, bone_weights vec3, the 35-bone
				// uniform array). The 4th weight is implied as 1 - sum(0..2).
				const char* kIdSkinnedVS =
					"#version 330 core\n"
					"in vec3 vertex_position;\n"
					"in ivec4 bone_ids;\n"
					"in vec3 bone_weights;\n"
					"uniform mat4 bone_matrices[35];\n"
					"uniform mat4 invert_view_matrix;\n"
					"uniform mat4 projection_matrix;\n"
					"uniform mat4 world_matrix;\n"
					"void main() {\n"
					"    mat4 bone_matrix = bone_matrices[bone_ids[0]] * bone_weights[0];\n"
					"    bone_matrix += bone_matrices[bone_ids[1]] * bone_weights[1];\n"
					"    bone_matrix += bone_matrices[bone_ids[2]] * bone_weights[2];\n"
					"    bone_matrix += bone_matrices[bone_ids[3]] * (1.0 - bone_weights[0] - bone_weights[1] - bone_weights[2]);\n"
					"    gl_Position = projection_matrix * invert_view_matrix * world_matrix * bone_matrix * vec4(vertex_position, 1.0);\n"
					"}\n";

				// Single uint-write fragment shader, shared between static
				// and skinned variants.
				const char* kIdFS =
					"#version 330 core\n"
					"uniform uint node_id;\n"
					"out uint fragment_output;\n"
					"void main() { fragment_output = node_id; }\n";

				void EnsureShaders()
				{
					if (!g_IdShaderStatic)
					{
						g_IdShaderStatic = Shader::Create("EditorPickIdStatic", ShaderType::OTHER);
						g_IdShaderStatic->Compile(kIdVS, kIdFS, "");
					}
					if (!g_IdShaderSkinned)
					{
						g_IdShaderSkinned = Shader::Create("EditorPickIdSkinned", ShaderType::SKINNED_MESH);
						g_IdShaderSkinned->Compile(kIdSkinnedVS, kIdFS, "");
					}
				}

				// Lazily allocate or resize the offscreen R32UI / DEPTH24
				// FBO. The Texture::CreateEmpty path takes care of the GL
				// internalformat mapping (R32UI -> GL_R32UI etc.).
				bool EnsureFBO(int w, int h)
				{
					if (w <= 0 || h <= 0) return false;

					if (g_PickFBO != 0 && w == g_PickW && h == g_PickH)
						return true;

					if (g_PickFBO != 0)
					{
						glDeleteFramebuffers(1, &g_PickFBO);
						g_PickFBO = 0;
					}
					g_PickColor.reset();
					g_PickDepth.reset();

					g_PickColor = Texture::Create("EditorPickColor");
					g_PickColor->SetFilterMode(FilterMode::NEAREST);
					g_PickColor->CreateEmpty(w, h, 0, TextureFormat::R32UI, TextureType::TEXTURE_2D, false);

					g_PickDepth = Texture::Create("EditorPickDepth");
					g_PickDepth->SetFilterMode(FilterMode::NEAREST);
					g_PickDepth->CreateEmpty(w, h, 0, TextureFormat::DEPTH24, TextureType::TEXTURE_2D, false);

					glGenFramebuffers(1, &g_PickFBO);
					glBindFramebuffer(GL_FRAMEBUFFER, g_PickFBO);
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D, g_PickColor->GetID(), 0);
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
						GL_TEXTURE_2D, g_PickDepth->GetID(), 0);
					GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
					glDrawBuffers(1, drawBufs);

					GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
					if (status != GL_FRAMEBUFFER_COMPLETE)
					{
						FURYE << "EditorPicking FBO incomplete, status=0x" << std::hex << status;
						glBindFramebuffer(GL_FRAMEBUFFER, 0);
						glDeleteFramebuffers(1, &g_PickFBO);
						g_PickFBO = 0;
						g_PickColor.reset();
						g_PickDepth.reset();
						return false;
					}

					glBindFramebuffer(GL_FRAMEBUFFER, 0);
					g_PickW = w;
					g_PickH = h;
					return true;
				}

				// Everything the render-side id pass needs, captured on the
				// game thread (no scene reads on the GL thread).
				struct IdPassUnit
				{
					std::shared_ptr<Mesh> mesh;

					Matrix4 world;

					std::vector<Matrix4> palette;

					unsigned int id = 0;
				};

				struct IdPassRequest
				{
					PacketCamera camera;

					int w = 0;

					int h = 0;

					int readX = 0;

					int readY = 0;

					std::vector<IdPassUnit> units;
				};

				// Render-thread side of the pick: draw every unit's mesh
				// with the id shader into the pick FBO, then read back the
				// single uint under the captured cursor and post it.
				void ExecuteIdPass(const IdPassRequest &req)
				{
					EnsureShaders();
					if (!EnsureFBO(req.w, req.h))
					{
						g_PickResultId.store(0);
						++g_PickResultSeq;
						return;
					}

					// Snapshot existing GL state so we don't disturb the
					// frame in flight more than necessary.
					GLint prev_fbo = 0;
					glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
					GLint prev_viewport[4] = { 0, 0, 0, 0 };
					glGetIntegerv(GL_VIEWPORT, prev_viewport);

					glBindFramebuffer(GL_FRAMEBUFFER, g_PickFBO);
					glViewport(0, 0, g_PickW, g_PickH);

					// 0 is the "no hit" sentinel. R32UI uses glClearBufferuiv.
					const GLuint clear_id[4] = { 0, 0, 0, 0 };
					glClearBufferuiv(GL_COLOR, 0, clear_id);
					glClear(GL_DEPTH_BUFFER_BIT);

					glEnable(GL_DEPTH_TEST);
					glDepthFunc(GL_LESS);
					glDisable(GL_BLEND);
					glDisable(GL_CULL_FACE);

					for (const auto &unit : req.units)
					{
						auto &mesh = unit.mesh;
						auto &shader = mesh->IsSkinnedMesh() ? g_IdShaderSkinned : g_IdShaderStatic;
						shader->Bind();
						shader->BindCameraData(req.camera);
						// Skinned vertices reach world space via Final = J_i W * ibm;
						// identity world_matrix so the mesh node transform isn't
						// double-applied (matches the gbuffer skin path).
						if (mesh->IsSkinnedMesh())
						{
							shader->BindMatrix(Matrix4::WORLD_MATRIX, Matrix4());
							shader->BindMesh(mesh, unit.palette.data(),
								static_cast<int>(unit.palette.size()));
						}
						else
						{
							shader->BindMatrix(Matrix4::WORLD_MATRIX, unit.world);
							shader->BindMesh(mesh);
						}
						shader->BindUInt("node_id", unit.id);

						if (mesh->GetSubMeshCount() > 0)
						{
							for (unsigned int s = 0; s < mesh->GetSubMeshCount(); ++s)
							{
								auto sub = mesh->GetSubMeshAt(s);
								shader->BindSubMesh(mesh, s);
								glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub->Indices.Data.size()),
									GL_UNSIGNED_INT, 0);
							}
						}
						else
						{
							glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()),
								GL_UNSIGNED_INT, 0);
						}

						shader->UnBind();
					}

					// Readback in the same job: the FBO holds this frame's
					// id pass right here.
					GLuint id = 0;
					glBindFramebuffer(GL_READ_FRAMEBUFFER, g_PickFBO);
					glReadBuffer(GL_COLOR_ATTACHMENT0);
					glReadPixels(req.readX, req.readY, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &id);

					glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
					glViewport(prev_viewport[0], prev_viewport[1],
						prev_viewport[2], prev_viewport[3]);

					g_PickResultId.store(id);
					++g_PickResultSeq;
				}

			} // namespace (anonymous)

			void RequestPickAt(ImVec2 viewport_px)
				{
					// Fold an in-flight request: replace it with the latest click
					// rather than queuing. The user's most recent click is what
					// they expect to hit.
					g_PendingClickPx = viewport_px;
					g_State = State::RenderRequested;
				}

				bool IsPickInFlight()
				{
					return g_State != State::Idle;
				}

				void TickPostRender()
				{
					if (g_State == State::Idle) return;

					// Discard the request if the runtime is not in a state where
					// the id-pass would be meaningful.
					if (Scene::Active == nullptr || Pipeline::Active == nullptr)
					{
						g_State = State::Idle;
						return;
					}

					auto cameraNode = Pipeline::Active->GetCurrentCamera();
					if (!cameraNode || !cameraNode->GetComponent<Camera>())
					{
						g_State = State::Idle;
						return;
					}

					if (g_State == State::RenderRequested)
					{
						// Size the picking FBO to the Viewport window's content
						// rect (not the full SFML window). When the viewport is
						// hidden / collapsed, discard the pick -- there's nothing
						// on screen to pick.
						if (!g_ViewportVisible)
						{
							g_State = State::Idle;
							return;
						}
						int w = static_cast<int>(g_ViewportContentSize.x);
						int h = static_cast<int>(g_ViewportContentSize.y);
						if (w <= 0 || h <= 0)
						{
							g_State = State::Idle;
							return;
						}

						auto &rt = RenderThread::Get();
						FramePacket *packet = rt.PeekStagedPacket();
						if (packet == nullptr)
						{
							// No staged frame to carry the job; retry next frame.
							return;
						}

						// Build the request on the game thread: the same
						// renderable set PrelightPipeline::Execute drew, so
						// the picked surfaces match what the user sees.
						auto sceneMgr = Scene::Active->GetSceneManager();
						auto camera = cameraNode->GetComponent<Camera>();
						RenderQuery::Ptr query = RenderQuery::Create();
						sceneMgr->GetRenderQuery(camera->GetFrustum(), query);

						IdPassRequest req;
						req.camera = BuildPacketCamera(cameraNode);
						req.w = w;
						req.h = h;
						// Y-flip from ImGui top-origin to GL bottom-origin.
						req.readX = static_cast<int>(g_PendingClickPx.x);
						req.readY = h - 1 - static_cast<int>(g_PendingClickPx.y);

						g_IdTable.clear();
						g_IdTable.reserve(query->renderableNodes.size());
						for (const auto &node : query->renderableNodes)
						{
							auto meshRender = node->GetComponent<MeshRender>();
							if (!meshRender) continue;
							auto mesh = meshRender->GetMesh();
							if (!mesh) continue;

							IdPassUnit unit;
							unit.mesh = mesh;
							unit.world = node->GetWorldMatrix();
							if (mesh->IsSkinnedMesh())
							{
								unsigned int jointCount = mesh->GetJointCount();
								unit.palette.reserve(jointCount);
								for (unsigned int i = 0; i < jointCount; ++i)
								{
									auto joint = mesh->GetJointAt(i);
									unit.palette.push_back(joint ? joint->GetFinalMatrix() : Matrix4());
								}
							}

							g_IdTable.push_back(node);
							unit.id = static_cast<unsigned int>(g_IdTable.size()); // 1-based
							req.units.push_back(std::move(unit));
						}

						if (req.readX < 0 || req.readY < 0 || req.readX >= w || req.readY >= h)
						{
							g_State = State::Idle;
							return;
						}

						g_ConsumedSeq = g_PickResultSeq.load();
						packet->overlayJobs.emplace_back([req]() { ExecuteIdPass(req); });
						g_State = State::AwaitingReadback;
						return;
					}

					if (g_State == State::AwaitingReadback)
					{
						// The overlay job posts (id, seq); consume when fresh.
						if (g_PickResultSeq.load() == g_ConsumedSeq)
							return;

						const unsigned int id = g_PickResultId.load();
						if (id == 0)
						{
							SetSelectedSceneNode(nullptr);
							g_State = State::Idle;
							return;
						}

						if (id - 1 >= g_IdTable.size())
						{
							g_State = State::Idle;
							return; // out of range -- id_table changed; leave selection alone
						}

						if (auto locked = g_IdTable[id - 1].lock())
						{
							SceneNode* picked = locked.get();
							SetSelectedSceneNode(picked);
							Editor::RevealInInspector(picked);
						}
						// else: node was destroyed between request and readback -- silently no-op.
						g_State = State::Idle;
						return;
					}
				}
		} // namespace Picking

	// Defined in EditorSelectionViz.cpp.
	void DrawSelectionOverlay();

		void TickPostRender()
		{
			Picking::TickPostRender();
			DrawSelectionOverlay();
		}
	}
}

#endif // WITH_EDITOR
