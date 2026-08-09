#include "Fury/Editor/EditorBodySetupWindow.h"

#if WITH_EDITOR

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "Fury/BodySetup.h"
#include "Fury/BoxBounds.h"
#include "Fury/Color.h"
#include "Fury/EntityManager.h"
#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshSimplifier.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Vector4.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Editor/Editor3DPreview.h"
#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			// Open windows keyed by node UUID; weak refs so a deleted node
			// closes its window instead of dangling.
			std::unordered_map<std::string, std::weak_ptr<SceneNode>> g_OpenBodySetupEditors;

			// Simple line shader (same program the mesh editor builds
			// inline): _ViewMatrix/_ProjectionMatrix/_OffsetMat/_Color.
			std::shared_ptr<Shader> g_LineShader;

			std::shared_ptr<Shader> EnsureLineShader()
			{
				if (!g_LineShader)
				{
					g_LineShader = Shader::Create("BodySetupEditorLineShader", ShaderType::OTHER);
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
					g_LineShader->Compile(vs, fs, "");
				}
				return g_LineShader;
			}

			// Scratch VAO/VBO for box / sphere wireframes (rebuilt per draw).
			GLuint g_LineVao = 0, g_LineVbo = 0;

			void DrawLineSegments(const std::vector<float> &verts, const Color &color)
			{
				if (verts.empty()) return;

				if (g_LineVao == 0)
				{
					glGenVertexArrays(1, &g_LineVao);
					glGenBuffers(1, &g_LineVbo);
					glBindVertexArray(g_LineVao);
					glBindBuffer(GL_ARRAY_BUFFER, g_LineVbo);
					glEnableVertexAttribArray(0);
					glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
					glBindVertexArray(0);
				}

				glBindBuffer(GL_ARRAY_BUFFER, g_LineVbo);
				glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);

				auto shader = EnsureLineShader();
				shader->BindFloat("_Color", color.r, color.g, color.b, color.a);
				glBindVertexArray(g_LineVao);
				glLineWidth(1.5f);
				glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 3));
				glBindVertexArray(0);
			}

			void PushBoxLines(std::vector<float> &out, const Vector4 &half)
			{
				const float x = half.x, y = half.y, z = half.z;
				const float c[8][3] = {
					{-x, -y, -z}, { x, -y, -z}, { x, -y,  z}, {-x, -y,  z},
					{-x,  y, -z}, { x,  y, -z}, { x,  y,  z}, {-x,  y,  z},
				};
				const int e[12][2] = {
					{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7},
				};
				for (const auto &edge : e)
				{
					out.insert(out.end(), c[edge[0]], c[edge[0]] + 3);
					out.insert(out.end(), c[edge[1]], c[edge[1]] + 3);
				}
			}

			void PushSphereLines(std::vector<float> &out, float radius)
			{
				const int segs = 48;
				auto pushCircle = [&](int axis)
				{
					for (int i = 0; i < segs; ++i)
					{
						const float a0 = (static_cast<float>(i) / segs) * 2.0f * 3.14159265f;
						const float a1 = (static_cast<float>(i + 1) / segs) * 2.0f * 3.14159265f;
						float p0[3] = { 0, 0, 0 }, p1[3] = { 0, 0, 0 };
						p0[axis] = 0.0f;
						p1[axis] = 0.0f;
						p0[(axis + 1) % 3] = std::cos(a0) * radius;
						p0[(axis + 2) % 3] = std::sin(a0) * radius;
						p1[(axis + 1) % 3] = std::cos(a1) * radius;
						p1[(axis + 2) % 3] = std::sin(a1) * radius;
						out.insert(out.end(), p0, p0 + 3);
						out.insert(out.end(), p1, p1 + 3);
					}
				};
				pushCircle(0);
				pushCircle(1);
				pushCircle(2);
			}

			const Color kShapeColor(1.0f, 0.85f, 0.1f, 1.0f);

			// 3D pane: render mesh solid + effective collision shape
			// wireframe + ground grid, framed on the render mesh's AABB.
			void RenderBodySetupPreview(const std::shared_ptr<SceneNode> &node,
				BodySetup *body, const std::string &popup_id, const ImVec2 &size)
			{
				std::shared_ptr<Mesh> renderMesh;
				if (auto mr = node->GetComponent<MeshRender>())
					renderMesh = mr->GetMesh();

				std::shared_ptr<Mesh> collisionMesh = body ? body->ResolveCollisionMesh() : nullptr;

				std::shared_ptr<Mesh> frameMesh = renderMesh ? renderMesh : collisionMesh;
				if (!frameMesh)
				{
					ImGui::BeginChild("preview", size, true, ImGuiWindowFlags_NoScrollbar);
					ImGui::TextDisabled("(node has no mesh)");
					ImGui::EndChild();
					return;
				}

				const ImVec2 pad(8.0f, 8.0f);
				const int w = std::max(32, static_cast<int>(size.x - 2.0f * pad.x + 0.5f));
				const int h = std::max(32, static_cast<int>(size.y - 2.0f * pad.y + 0.5f));
				const float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.0f;

				auto &rt = EnsureRT(popup_id, w, h);
				if (rt.fbo == 0)
				{
					ImGui::BeginChild("preview", size, false, ImGuiWindowFlags_NoScrollbar);
					ImGui::TextDisabled("(3D preview - FBO incomplete)");
					ImGui::EndChild();
					return;
				}

				const BoxBounds aabb = frameMesh->GetAABB();
				const Vector4 mn = aabb.GetMin(), mx = aabb.GetMax();
				const Vector4 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f, 1.0f);
				const Vector4 aabbSize(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 0.0f);
				float radius = 0.5f * std::sqrt(aabbSize.x * aabbSize.x +
					aabbSize.y * aabbSize.y + aabbSize.z * aabbSize.z);
				if (radius < 1e-6f) radius = 0.5f;

				OrbitState &os = OrbitFor(popup_id);
				const bool mesh_changed = (os.framed_mesh != frameMesh.get());
				if (!os.initialized || mesh_changed)
					ReframeOrbit(os, center, radius, aspect, mesh_changed);
				os.framed_mesh = frameMesh.get();

				auto vp = ComputeViewProj(os, center, radius, aspect);

				glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
				glViewport(0, 0, w, h);
				glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				glEnable(GL_DEPTH_TEST);

				// Solid render mesh (bind pose for skinned - fine for
				// collision authoring).
				if (renderMesh)
				{
					if (renderMesh->GetDirty())
						renderMesh->UpdateBuffer();

					auto shader = GetSimpleLambertShader();
					shader->Bind();
					Matrix4 world;
					world.Identity();
					shader->BindMatrix("_WorldMatrix", world);
					shader->BindMatrix("_ViewMatrix", vp.view);
					shader->BindMatrix("_ProjectionMatrix", vp.proj);

					const unsigned int subCount = renderMesh->GetSubMeshCount();
					shader->BindMesh(renderMesh);
					if (subCount == 0)
					{
						glDrawElements(GL_TRIANGLES,
							static_cast<GLsizei>(renderMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
					}
					else
					{
						for (unsigned int i = 0; i < subCount; ++i)
						{
							auto sm = renderMesh->GetSubMeshAt(i);
							if (!sm) continue;
							shader->BindSubMesh(renderMesh, i);
							glDrawElements(GL_TRIANGLES,
								static_cast<GLsizei>(sm->Indices.Data.size()), GL_UNSIGNED_INT, 0);
						}
					}
				}

				// Collision wireframe overlay.
				if (body)
				{
					auto lineShader = EnsureLineShader();
					lineShader->Bind();
					lineShader->BindMatrix("_ViewMatrix", vp.view);
					lineShader->BindMatrix("_ProjectionMatrix", vp.proj);
					Matrix4 identity;
					identity.Identity();
					lineShader->BindMatrix("_OffsetMat", identity);

					if (body->GetShapeType() == BodySetup::ShapeType::Mesh)
					{
						if (collisionMesh)
						{
							if (collisionMesh->GetDirty())
								collisionMesh->UpdateBuffer();
							lineShader->BindFloat("_Color", kShapeColor.r, kShapeColor.g, kShapeColor.b, kShapeColor.a);
							glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
							lineShader->BindMesh(collisionMesh);
							const unsigned int subCount = collisionMesh->GetSubMeshCount();
							if (subCount == 0)
							{
								glDrawElements(GL_TRIANGLES,
									static_cast<GLsizei>(collisionMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);
							}
							else
							{
								for (unsigned int i = 0; i < subCount; ++i)
								{
									auto sm = collisionMesh->GetSubMeshAt(i);
									if (!sm) continue;
									lineShader->BindSubMesh(collisionMesh, i);
									glDrawElements(GL_TRIANGLES,
										static_cast<GLsizei>(sm->Indices.Data.size()), GL_UNSIGNED_INT, 0);
								}
							}
							glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
						}
					}
					else
					{
						std::vector<float> verts;
						if (body->GetShapeType() == BodySetup::ShapeType::Box)
							PushBoxLines(verts, body->GetHalfExtents());
						else
							PushSphereLines(verts, body->GetRadius());
						DrawLineSegments(verts, kShapeColor);
					}
				}

				DrawGroundGrid(vp.view, vp.proj, center, mn.y, radius);

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDisable(GL_DEPTH_TEST);

				ImGui::BeginChild("preview", size, false, ImGuiWindowFlags_NoScrollbar);
				ImGui::SetCursorPos(pad);
				const ImVec2 img_size(size.x - 2.0f * pad.x, size.y - 2.0f * pad.y);
				ImGui::Image((ImTextureID)(intptr_t)rt.colorRT->GetID(),
					img_size, ImVec2(0, 1), ImVec2(1, 0));

				if (ImGui::IsItemHovered() || ImGui::IsWindowHovered())
				{
					const ImGuiIO &io = ImGui::GetIO();
					ApplyCameraInput(os, vp.eye, io, radius);
				}
				ImGui::EndChild();
			}

			void RenderSimplifyModal(const std::shared_ptr<SceneNode> &node, BodySetup *body)
			{
				if (!ImGui::BeginPopupModal("Simplify Collision Mesh", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize))
					return;

				auto source = body->ResolveCollisionMesh();
				if (!source)
				{
					ImGui::TextDisabled("(no source mesh - node has no render mesh and no collision mesh set)");
					if (ImGui::Button("Close"))
						ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return;
				}

				size_t triCount = source->Indices.Data.size() / 3;
				for (unsigned int i = 0; i < source->GetSubMeshCount(); ++i)
					if (auto sm = source->GetSubMeshAt(i))
						triCount += sm->Indices.Data.size() / 3;

				static float s_Ratio = 0.25f;
				ImGui::Text("Source: %s (%zu triangles)", source->GetName().c_str(), triCount);
				ImGui::SliderFloat("Target fraction", &s_Ratio, 0.05f, 0.95f, "%.2f");
				ImGui::Text("Target triangles: ~%zu", static_cast<size_t>(triCount * s_Ratio));

				if (ImGui::Button("Simplify"))
				{
					MeshSimplifyOptions opts;
					opts.lod_count = 1;
					opts.reduction_ratio = s_Ratio;
					auto result = SimplifyMesh(source, opts);

					if (!result.lod_meshes.empty() && Scene::Active)
					{
						auto collisionMesh = result.lod_meshes[0];

						// Unique asset name (asset-unique-naming rules):
						// <source>_collision, then _1, _2, ...
						const std::string base = source->GetName() + "_collision";
						std::string name = base;
						for (int i = 1; Scene::Manager()->Get<Mesh>(name); ++i)
							name = base + "_" + std::to_string(i);

						collisionMesh->SetName(name);
						Scene::Active->GetEntityManager()->Add(collisionMesh);
						body->SetCollisionMeshName(name);
						Editor::MarkSceneDirty();
						FURYI << "BodySetup editor: created collision mesh '" << name << "' ("
							<< triCount << " -> ~" << static_cast<size_t>(triCount * s_Ratio) << " tris).";
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}

			void RenderBodySetupInspector(const std::shared_ptr<SceneNode> &node, BodySetup *body)
			{
				static const char *kShapeNames[] = { "Mesh", "Box", "Sphere" };
				int shapeIdx = static_cast<int>(body->GetShapeType());
				if (ImGui::Combo("Shape", &shapeIdx, kShapeNames, 3))
				{
					body->SetShapeType(static_cast<BodySetup::ShapeType>(shapeIdx));
					Editor::MarkSceneDirty();
				}

				static const char *kMotionNames[] = { "Static", "Dynamic" };
				int motionIdx = static_cast<int>(body->GetMotionType());
				if (ImGui::Combo("Motion", &motionIdx, kMotionNames, 2))
				{
					body->SetMotionType(static_cast<BodySetup::MotionType>(motionIdx));
					Editor::MarkSceneDirty();
				}

				ImGui::Separator();

				if (body->GetShapeType() == BodySetup::ShapeType::Mesh)
				{
					ImGui::TextUnformatted("Collision Mesh:");
					ImGui::SameLine();
					if (body->GetCollisionMeshName().empty())
						ImGui::TextDisabled("(render mesh)");
					else
						ImGui::TextUnformatted(body->GetCollisionMeshName().c_str());
					if (ImGui::Button("Simplify Collision Mesh..."))
						ImGui::OpenPopup("Simplify Collision Mesh");
				}
				else if (body->GetShapeType() == BodySetup::ShapeType::Box)
				{
					Vector4 he = body->GetHalfExtents();
					float xyz[3] = { he.x, he.y, he.z };
					if (ImGui::DragFloat3("Half Extents", xyz, 0.5f, 0.5f, 100000.0f))
					{
						body->SetHalfExtents(Vector4(xyz[0], xyz[1], xyz[2], 0.0f));
						Editor::MarkSceneDirty();
					}
					if (ImGui::Button("Auto-Fit From Mesh"))
					{
						body->AutoFitFromMesh();
						Editor::MarkSceneDirty();
					}
				}
				else
				{
					float radius = body->GetRadius();
					if (ImGui::DragFloat("Radius", &radius, 0.5f, 0.5f, 100000.0f))
					{
						body->SetRadius(radius);
						Editor::MarkSceneDirty();
					}
					if (ImGui::Button("Auto-Fit From Mesh"))
					{
						body->AutoFitFromMesh();
						Editor::MarkSceneDirty();
					}
				}

				ImGui::Separator();

				if (body->GetMotionType() == BodySetup::MotionType::Dynamic)
				{
					float mass = body->GetMass();
					if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.001f, 100000.0f))
					{
						body->SetMass(mass);
						Editor::MarkSceneDirty();
					}
				}
				float friction = body->GetFriction();
				if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f))
				{
					body->SetFriction(friction);
					Editor::MarkSceneDirty();
				}
				float restitution = body->GetRestitution();
				if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f))
				{
					body->SetRestitution(restitution);
					Editor::MarkSceneDirty();
				}

				RenderSimplifyModal(node, body);
			}
		}

		void OpenBodySetupEditor(const std::shared_ptr<SceneNode> &node)
		{
			if (!node || !node->GetComponent<BodySetup>())
				return;
			g_OpenBodySetupEditors[node->GetUUID()] = node;
		}

		void RenderAllOpenBodySetupEditors()
		{
			for (auto it = g_OpenBodySetupEditors.begin(); it != g_OpenBodySetupEditors.end();)
			{
				auto node = it->second.lock();
				bool open = (node != nullptr);
				if (open)
				{
					ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);

					const std::string title = "Body Setup: " + node->GetName() + "##" + it->first;
					if (ImGui::Begin(title.c_str(), &open))
					{
						auto body = node->GetComponent<BodySetup>();
						if (!body)
						{
							ImGui::TextDisabled("(BodySetup component removed)");
						}
						else
						{
							const std::string popup_id = "BodySetupEditor:" + it->first;

							ImVec2 avail = ImGui::GetContentRegionAvail();
							const float inspector_w = std::max(260.0f, avail.x * 0.32f);
							const float viewer_w = std::max(120.0f, avail.x - inspector_w - 8.0f);

							RenderBodySetupPreview(node, body.get(), popup_id,
								ImVec2(viewer_w, avail.y));
							ImGui::SameLine();
							ImGui::BeginChild("inspector", ImVec2(inspector_w, avail.y), true);
							RenderBodySetupInspector(node, body.get());
							ImGui::EndChild();
						}
					}
					ImGui::End();
				}

				if (!open)
					it = g_OpenBodySetupEditors.erase(it);
				else
					++it;
			}
		}
	}
}

#endif // WITH_EDITOR
